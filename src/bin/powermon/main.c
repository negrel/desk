/**
 * powermon is a power monitoring daemon. Currently it only monitor batteries
 * and send a desktop notification on low battery. powermon depends on UPower
 * D-Bus service.
 *
 * Adding/removing batteries is not supported for the moment.
 *
 * Note that powermon is not robust and panic on every error, it is recommended
 * to run it with a restart on failure policy.
 */

#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>

#define LOG_IMPLEMENTATION
#define LOG_MODULE "main"
#include "log.h"

#include "error.h"
#include "notify.h"
#include "tllist.h"

#include "upower.h"

/**
 * Battery data.
 */
typedef struct {
  struct powermon_data *powermon;
  sd_bus_slot *slot;
  double percentage;
  uint32_t level;
  uint32_t state;
} battery_data;

/**
 * powermon state.
 */
typedef struct powermon_data {
  sd_event *loop;
  sd_bus *system_bus;
  sd_bus *user_bus;
  tll(battery_data *) batteries;
  uint32_t notif_id;
} powermon_data;

// Print CLI usage.
static void print_usage(char *prog_name) {
  static const char header[] = "powermon v0.1.0\n"
                               "Alexandre Negrel <alexandre@negrel.dev>\n\n"
                               ""
                               "";

  static const char options[] =
      "Options:\n"
      "  -d, --daemon                             Run as a daemon"
      "  -h, --help                               Print this message and "
      "exit\n"
      "  -l, --log-level                          Set log level (one of "
      "'debug', 'info', 'warning', 'error', 'none')\n"
      "";

  puts(header);
  printf("Usage: %s [OPTIONS...]\n", prog_name);
  puts(options);
}

// Battery property changed event handler.
static int on_battery_changed(sd_bus_message *m, void *userdata,
                              sd_bus_error *ret_error) {
  (void)ret_error;

  const char *interface = NULL;
  battery_data *battery = userdata;
  notification notif = {0};

  // Read interface.
  SDBUS_PANIC(sd_bus_message_read(m, "s", &interface),
              "failed to read interface of PropertiesChanged signal");
  LOG_DBG("signal on interface %s", interface);

  // Not UPower device.
  if (strcmp(interface, "org.freedesktop.UPower.Device") != 0)
    return 0;

  SDBUS_PANIC(sd_bus_message_enter_container(m, 'a', "{sv}"),
              "failed to enter battery changed container");

  // Sync changed properties.
  while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
    const char *key;
    SDBUS_PANIC(sd_bus_message_read(m, "s", &key),
                "failed to read PropertiesChanged dict key");

    LOG_DBG("battery property '%s' changed", key);

    if (strcmp(key, "State") == 0) {
      SDBUS_PANIC(sd_bus_message_enter_container(m, 'v', "u"),
                  "failed to enter 'State' variant");

      SDBUS_PANIC(sd_bus_message_read(m, "u", &battery->state),
                  "failed to read changed property 'State' of UPower battery");

      sd_bus_message_exit_container(m);
    } else if (strcmp(key, "BatteryLevel") == 0) {
      SDBUS_PANIC(sd_bus_message_enter_container(m, 'v', "u"),
                  "failed to enter 'BatteryLevel' variant");

      SDBUS_PANIC(
          sd_bus_message_read(m, "u", &battery->level),
          "failed to read changed property 'BatteryLevel' of UPower battery");

      sd_bus_message_exit_container(m);
    } else if (strcmp(key, "Percentage") == 0) {
      SDBUS_PANIC(sd_bus_message_enter_container(m, 'v', "d"),
                  "failed to enter 'Percentage' variant");

      SDBUS_PANIC(
          sd_bus_message_read(m, "d", &battery->percentage),
          "failed to read changed property 'Percentage' of UPower battery");

      sd_bus_message_exit_container(m);
    } else {
      SDBUS_PANIC(sd_bus_message_skip(m, NULL),
                  "failed to skip PropertiesChanged dict entry");
    }

    sd_bus_message_exit_container(m);
  }

  if (battery->state == UPOWER_STATE_DISCHARGING) {
    if (battery->level == UPOWER_BATTERY_LEVEL_LOW ||
        battery->percentage < 20.0) {
      LOG_INFO("Low battery, sending notification");

      notif.app = "dev.negrel.desk.powermon";
      notif.title = "Low battery";
      notif.timeout = 0;
      notif.hints = &notification_urgency_high;
      notif.n_hints = 1;
      notif.replace_id = battery->powermon->notif_id;

      ERRNO_PANIC(asprintf((char **)&notif.body,
                           "Please charge now, %.0f%% remaining.",
                           battery->percentage),
                  "failed to allocated notification body");

      SDBUS_PANIC(notify(battery->powermon->user_bus, &notif,
                         &battery->powermon->notif_id, NULL),
                  "failed to send 'Low battery' notification");
    }
  } else {
    if (battery->powermon->notif_id != 0) {
      SDBUS_PANIC(notification_close(battery->powermon->user_bus,
                                     battery->powermon->notif_id, NULL),
                  "failed to close 'Low battery' notification");
    }
  }

  if (notif.body != NULL)
    free((void *)notif.body);

  return 0;
}

// Watch battery.
static void watch_battery(sd_bus *bus, const char *path, void *data) {
  powermon_data *powermon = data;
  battery_data *battery = calloc(1, sizeof(*battery));
  if (battery == NULL)
    LOG_FATAL("failed to allocated battery data");
  tll_push_back(powermon->batteries, battery);
  battery->powermon = powermon;

  sd_bus_message *reply = NULL;

  // Retrieve percentage.
  SDBUS_PANIC(sd_bus_get_property(bus, "org.freedesktop.UPower", path,
                                  "org.freedesktop.UPower.Device", "Percentage",
                                  NULL, &reply, "d"),
              "failed to get 'Percentage' property of UPower battery");
  SDBUS_PANIC(sd_bus_message_read(reply, "d", &battery->percentage),
              "failed to read 'Percentage' property of UPower battery");
  sd_bus_message_unref(reply);

  // Retrieve battery level.
  SDBUS_PANIC(sd_bus_get_property(bus, "org.freedesktop.UPower", path,
                                  "org.freedesktop.UPower.Device",
                                  "BatteryLevel", NULL, &reply, "u"),
              "failed to get 'BatteryLevel' property of UPower battery");
  SDBUS_PANIC(sd_bus_message_read(reply, "u", &battery->level),
              "failed to read 'BatteryLevel' property of UPower battery");

  sd_bus_message_unref(reply);

  // Retrieve state.
  SDBUS_PANIC(sd_bus_get_property(bus, "org.freedesktop.UPower", path,
                                  "org.freedesktop.UPower.Device", "State",
                                  NULL, &reply, "u"),
              "failed to get 'State' property of UPower battery");
  SDBUS_PANIC(sd_bus_message_read(reply, "u", &battery->state),
              "failed to read 'State' property of UPower battery");
  sd_bus_message_unref(reply);

  // Watch for changes.
  SDBUS_PANIC(sd_bus_match_signal(bus, &battery->slot, "org.freedesktop.UPower",
                                  path, "org.freedesktop.DBus.Properties",
                                  "PropertiesChanged", on_battery_changed,
                                  battery),
              "failed to watch UPower battery for property change");

  LOG_INFO("watching battery '%s' percentage=%f level=%d state=%d", path,
           battery->percentage, battery->level, battery->state);
}

static int on_signal(sd_event_source *s, const struct signalfd_siginfo *si,
                     void *userdata) {
  (void)s;
  (void)si;

  sd_event *event = userdata;
  LOG_DBG("Received SIGINT. Exiting...");

  // Exit the event loop
  sd_event_exit(event, 0);
  return 0;
}

int main(int argc, char **argv) {
  // Parse args.
  char *prog_name = argv[0];
  enum log_class log_level = LOG_CLASS_INFO;
  bool daemonize = false;
  while (1) {
    static struct option long_options[] = {
        {"daemon", no_argument, 0, 'd'},
        {"help", no_argument, 0, 'h'},
        {"log-level", required_argument, 0, 'l'},
        {0, 0, 0, 0},
    };

    int c = getopt_long(argc, argv, "dhl:", long_options, NULL);
    if (c == -1)
      break;

    switch (c) {
    case 'd':
      daemonize = true;
      break;

    case 'h':
      print_usage(prog_name);
      return EXIT_SUCCESS;

    case 'l':
      log_level = log_level_from_string(optarg);
      if ((int)log_level == -1) {
        fprintf(stderr, "invalid log level\n");
        print_usage(prog_name);
        return EXIT_FAILURE;
      }
      break;

    default:
      BUG("unhandled option -%c", c);
    }
  }

  // Setup log.
  log_init(LOG_COLORIZE_AUTO, daemonize,
           daemonize ? LOG_FACILITY_DAEMON : LOG_FACILITY_USER, log_level);
  LOG_DBG("log initialized");

  // Run as daemon.
  if (daemonize)
    ERRNO_PANIC(daemon(0, 0), "failed to daemonize process");

  powermon_data powermon = {0};
  sd_event_source *signal_source = NULL;

  // Initialize event loop.
  SDEV_PANIC(sd_event_default(&powermon.loop),
             "failed to initialize to event loop");

  // Setup SIGINT handler.
  {
    // Remove default SIGINT handler.
    sigset_t ss = {0};
    sigemptyset(&ss);
    sigaddset(&ss, SIGINT);
    if (sigprocmask(SIG_BLOCK, &ss, NULL) < 0)
      LOG_FATAL("failed to set signal mask: %m");

    // Add SIGINT handler to event loop.
    SDEV_PANIC(sd_event_add_signal(powermon.loop, &signal_source, SIGINT,
                                   on_signal, powermon.loop),
               "failed to add SIGINT handler");
  }

  // Connect to system and user D-Bus.
  SDBUS_PANIC(sd_bus_default_system(&powermon.system_bus),
              "failed to connect to system bus");
  SDBUS_PANIC(sd_bus_default_user(&powermon.user_bus),
              "failed to connect to user bus");

  // Attach system & user D-Bus to event loop.
  SDBUS_PANIC(sd_bus_attach_event(powermon.system_bus, powermon.loop,
                                  SD_EVENT_PRIORITY_NORMAL),
              "failed to attach system bus handle to event loop");
  SDBUS_PANIC(sd_bus_attach_event(powermon.user_bus, powermon.loop,
                                  SD_EVENT_PRIORITY_NORMAL),
              "failed to attach user bus handle to event loop");

  // Setup watch on all batteries.
  for_all_batteries(powermon.system_bus, &powermon, watch_battery);

  // Run event loop.
  SDEV_PANIC(sd_event_loop(powermon.loop), "event loop failed");

  // Clean up.
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
  return EXIT_SUCCESS;
}
