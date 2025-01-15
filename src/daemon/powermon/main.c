#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <getopt.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>

#define LOG_MODULE "main"
#include "log.h"

#include "debug.h"
#include "notify.h"
#include "tllist.h"

#define UP_STATE_UNKNOWN 0
#define UP_STATE_CHARGING 1
#define UP_STATE_DISCHARGING 2
#define UP_STATE_EMPTY 3
#define UP_STATE_FULLY_CHARGED 4
#define UP_STATE_PENDING_CHARGE 5
#define UP_STATE_PENDING_DISCHARGE 6

#define UP_DEVICE_TYPE_BATTERY 2

#define UP_STATE_BATTERY_LEVEL_UNKNOWN 0
#define UP_STATE_BATTERY_LEVEL_NONE 1
#define UP_STATE_BATTERY_LEVEL_LOW 2
#define UP_STATE_BATTERY_LEVEL_CRITICAL 3
#define UP_STATE_BATTERY_LEVEL_NORMAL 4
#define UP_STATE_BATTERY_LEVEL_HIGH 5
#define UP_STATE_BATTERY_LEVEL_FULL 6

#define SD_TRY(err, fmt, ...)                                                  \
  if (err < 0) {                                                               \
    LOG_ERR(fmt ": %s", ##__VA_ARGS__, strerror(-err));                        \
    return err;                                                                \
  }

#define SD_TRY_GOTO(err, label, fmt, ...)                                      \
  if (err < 0) {                                                               \
    LOG_ERR(fmt ": %s", ##__VA_ARGS__, strerror(-err));                        \
    goto label;                                                                \
  }

struct powermon_data;
typedef struct {
  struct powermon_data *powermon;
  sd_bus_slot *slot;
  double percentage;
  uint32_t level;
  uint32_t state;
} battery_data;

typedef struct powermon_data {
  sd_event *loop;
  sd_bus *system_bus;
  sd_bus *user_bus;
  tll(battery_data *) batteries;
  uint32_t notif_id;
} powermon_data;

static void print_usage(char *prog_name);
static int for_all_batteries(sd_bus *bus, void *data,
                             int (*cb)(sd_bus *bus, const char *path,
                                       void *data));
static int watch_battery(sd_bus *bus, const char *path, void *slots);
static int on_battery_changed(sd_bus_message *m, void *userdata,
                              sd_bus_error *ret_error);

static int signal_handler(sd_event_source *s, const struct signalfd_siginfo *si,
                          void *userdata);

int main(int argc, char **argv) {
  // Parse args.
  char *prog_name = argv[0];
  enum log_class log_level = LOG_CLASS_INFO;
  while (1) {
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"log-level", required_argument, 0, 'l'},
        {0, 0, 0, 0},
    };

    int c = getopt_long(argc, argv, "hl:", long_options, NULL);
    if (c == -1)
      break;

    switch (c) {
    case 'l':
      log_level = log_level_from_string(optarg);
      break;

    case 'h':
      print_usage(prog_name);
      return EXIT_SUCCESS;

    default:
      BUG("unhandled option -%c", c);
    }
  }

  // Setup log.
  log_init(LOG_COLORIZE_AUTO, true, LOG_FACILITY_USER, log_level);
  LOG_DBG("log initialized");

  powermon_data powermon = {0};
  sd_event_source *signal_source = NULL;
  int r = 0;

  // Initialize event loop.
  r = sd_event_default(&powermon.loop);
  SD_TRY_GOTO(r, cleanup, "failed to initialize to event loop");

  // Remove default SIGINT handler.
  sigset_t ss = {0};
  sigemptyset(&ss);
  sigaddset(&ss, SIGINT);
  if (sigprocmask(SIG_BLOCK, &ss, NULL) < 0)
    LOG_FATAL("failed to set signal mask: %m");

  // Add SIGINT handler
  r = sd_event_add_signal(powermon.loop, &signal_source, SIGINT, signal_handler,
                          powermon.loop);
  SD_TRY_GOTO(r, cleanup, "failed to add SIGINT handler");

  // Connect to system D-Bus.
  r = sd_bus_default_system(&powermon.system_bus);
  SD_TRY_GOTO(r, cleanup, "failed to connect to system bus");
  r = sd_bus_default_user(&powermon.user_bus);
  SD_TRY_GOTO(r, cleanup, "failed to connect to user bus");

  // Attach system & user D-Bus to event loop.
  sd_bus_attach_event(powermon.system_bus, powermon.loop,
                      SD_EVENT_PRIORITY_NORMAL);
  sd_bus_attach_event(powermon.user_bus, powermon.loop,
                      SD_EVENT_PRIORITY_NORMAL);

  // Setup watch on all batteries.
  r = for_all_batteries(powermon.system_bus, &powermon, watch_battery);
  SD_TRY_GOTO(r, cleanup, "failed to add signal match for all batteries");

  SD_TRY_GOTO(sd_event_loop(powermon.loop), cleanup, "event loop failed");

cleanup:
  tll_foreach(powermon.batteries, it) {
    sd_bus_slot_unref(it->item->slot);
    free(it->item);
    tll_remove(powermon.batteries, it);
  }
  if (powermon.system_bus)
    sd_bus_unref(powermon.system_bus);
  if (powermon.user_bus)
    sd_bus_unref(powermon.user_bus);
  sd_event_unref(powermon.loop);
  sd_event_source_unref(signal_source);
  log_deinit();
  return r < 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void print_usage(char *prog_name) {
  static const char header[] = "powermon v0.1.0\n"
                               "Alexandre Negrel <alexandre@negrel.dev>\n\n"
                               ""
                               "";

  static const char options[] =
      "Options:\n"
      "  -h, --help                               Print this message and "
      "exit\n"
      "  -l, --log-level                          Set log level (one of "
      "'debug', 'info', 'warning', 'error', 'none')\n"
      "";

  puts(header);
  printf("Usage: %s [OPTIONS...]\n", prog_name);
  puts(options);
}

static int for_all_batteries(sd_bus *bus, void *data,
                             int (*cb)(sd_bus *bus, const char *path,
                                       void *data)) {
  sd_bus_message *reply = NULL;

  SD_TRY(sd_bus_call_method(bus, "org.freedesktop.UPower",
                            "/org/freedesktop/UPower", "org.freedesktop.UPower",
                            "EnumerateDevices", NULL, &reply, ""),
         "failed to call method to enumerate devices");

  SD_TRY(sd_bus_message_enter_container(reply, 'a', "o"),
         "failed to enter reply array");

  const char *path;
  while (sd_bus_message_read(reply, "o", &path) > 0) {
    LOG_DBG("device path: %s", path);
    sd_bus_message *type_reply = NULL;
    uint32_t type = 0;
    SD_TRY(sd_bus_get_property(bus, "org.freedesktop.UPower", path,
                               "org.freedesktop.UPower.Device", "Type", NULL,
                               &type_reply, "u"),
           "failed to get type of power device");
    SD_TRY(sd_bus_message_read(type_reply, "u", &type),
           "failed to read reply of 'Type' property get");

    if (type == UP_DEVICE_TYPE_BATTERY) {
      LOG_DBG("device '%s' is a battery", path);
      SD_TRY(cb(bus, path, data), "failed to execute callback on battery %s",
             path);
    }
    sd_bus_message_unref(type_reply);
  }

  sd_bus_message_unref(reply);

  return 0;
}

static int watch_battery(sd_bus *bus, const char *path, void *data) {
  powermon_data *powermon = data;
  battery_data *battery = calloc(1, sizeof(*battery));
  if (battery == NULL)
    LOG_FATAL("failed to allocated battery data");
  tll_push_back(powermon->batteries, battery);
  battery->powermon = powermon;

  int r = 0;
  sd_bus_message *reply = NULL;

  // Retrieve percentage.
  r = sd_bus_get_property(bus, "org.freedesktop.UPower", path,
                          "org.freedesktop.UPower.Device", "Percentage", NULL,
                          &reply, "d");
  SD_TRY_GOTO(r, cleanup, "failed to get percentage of power device");
  r = sd_bus_message_read(reply, "d", &battery->percentage);
  SD_TRY_GOTO(r, cleanup, "failed to read reply of 'Percentage' property get");
  sd_bus_message_unref(reply);

  // Retrieve battery level.
  r = sd_bus_get_property(bus, "org.freedesktop.UPower", path,
                          "org.freedesktop.UPower.Device", "BatteryLevel", NULL,
                          &reply, "u");
  SD_TRY_GOTO(r, cleanup, "failed to get batter level of power device");
  r = sd_bus_message_read(reply, "u", &battery->level);
  SD_TRY_GOTO(r, cleanup,
              "failed to read reply of 'BatteryLevel' property get");
  sd_bus_message_unref(reply);

  // Retrieve state.
  r = sd_bus_get_property(bus, "org.freedesktop.UPower", path,
                          "org.freedesktop.UPower.Device", "State", NULL,
                          &reply, "u");
  SD_TRY_GOTO(r, cleanup, "failed to get state of power device");
  r = sd_bus_message_read(reply, "u", &battery->state);
  SD_TRY_GOTO(r, cleanup, "failed to read reply of 'State' property get");
  sd_bus_message_unref(reply);

  // Watch for changes.
  r = sd_bus_match_signal(bus, &battery->slot, "org.freedesktop.UPower", path,
                          "org.freedesktop.DBus.Properties",
                          "PropertiesChanged", on_battery_changed, battery);
  SD_TRY_GOTO(r, ret_err, "failed to match signal for device %s", path);

  LOG_INFO("watching battery '%s' percentage=%f level=%d state=%d", path,
           battery->percentage, battery->level, battery->state);

  return 0;

cleanup:
  sd_bus_message_unref(reply);
ret_err:
  return r;
}

static int on_battery_changed(sd_bus_message *m, void *userdata,
                              sd_bus_error *ret_error) {
  (void)ret_error;

  int r = 0;
  const char *interface = NULL;
  battery_data *battery = userdata;
  notification notif = {0};

  // Read interface.
  SD_TRY(sd_bus_message_read(m, "s", &interface),
         "failed to read interface of properties changed signal");
  LOG_DBG("signal on interface %s", interface);
  if (strcmp(interface, "org.freedesktop.UPower.Device") != 0)
    return 0;

  SD_TRY(sd_bus_message_enter_container(m, 'a', "{sv}"),
         "failed to enter battery changed container");

  while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {

    const char *key;
    r = sd_bus_message_read(m, "s", &key);
    SD_TRY_GOTO(r, cleanup, "failed to read PropertiesChanged dict key");

    LOG_DBG("battery property '%s' changed", key);

    if (strcmp(key, "State") == 0) {
      SD_TRY(sd_bus_message_enter_container(m, 'v', "u"),
             "failed to enter variant");
      r = sd_bus_message_read(m, "u", &battery->state);
      SD_TRY_GOTO(r, cleanup, "failed to read property 'State' change");

      sd_bus_message_exit_container(m);
      sd_bus_message_exit_container(m);
    } else if (strcmp(key, "BatteryLevel") == 0) {
      SD_TRY(sd_bus_message_enter_container(m, 'v', "u"),
             "failed to enter variant");
      r = sd_bus_message_read(m, "u", &battery->level);
      SD_TRY_GOTO(r, cleanup, "failed to read property 'BatteryLevel' change");

      sd_bus_message_exit_container(m);
      sd_bus_message_exit_container(m);
    } else if (strcmp(key, "Percentage") == 0) {
      SD_TRY(sd_bus_message_enter_container(m, 'v', "d"),
             "failed to enter variant");
      r = sd_bus_message_read(m, "d", &battery->percentage);
      SD_TRY_GOTO(r, cleanup, "failed to read property 'Percentage' change");

      sd_bus_message_exit_container(m);
      sd_bus_message_exit_container(m);
    } else {
      SD_TRY_GOTO(sd_bus_message_skip(m, NULL), cleanup,
                  "failed to skip PropertiesChanged dict entry");
    }

    sd_bus_message_exit_container(m);
  }

  if (battery->state == UP_STATE_DISCHARGING) {
    if (battery->level == UP_STATE_BATTERY_LEVEL_LOW ||
        battery->percentage < 20.0) {
      LOG_INFO("Low battery, sending notification");

      notif.app = "DESK Power Monitor";
      notif.title = "Low battery";
      notif.timeout = 0;
      notif.hints = &notification_urgency_high;
      notif.n_hints = 1;
      notif.replace_id = battery->powermon->notif_id;

      r = asprintf((char **)&notif.body, "Please charge now, %.0f%% remaining.",
                   battery->percentage);
      if (r == -1)
        LOG_FATAL("failed to allocated notification body");

      SD_TRY_GOTO(notify(battery->powermon->user_bus, &notif,
                         &battery->powermon->notif_id, NULL),
                  cleanup, "failed to send notification");
    }
  } else {
    if (battery->powermon->notif_id != 0) {
      SD_TRY_GOTO(notification_close(battery->powermon->user_bus,
                                     battery->powermon->notif_id, NULL),
                  cleanup, "failed to close notification");
    }
  }

cleanup:
  if (notif.body != NULL)
    free((void *)notif.body);
  return r;
}

static int signal_handler(sd_event_source *s, const struct signalfd_siginfo *si,
                          void *userdata) {
  (void)s;
  (void)si;

  sd_event *event = userdata;
  LOG_DBG("Received SIGINT. Exiting...");

  // Exit the event loop
  sd_event_exit(event, 0);
  return 0;
}
