/**
 * USound is an abstraction for enumerating sound devices, listening to device
 * events. Any application or service on the system can access the
 * dev.negrel.desk.USound service via user message bus.
 *
 * You can think of it as the sound version of UPower.
 *
 * USound is based on PipeWire.
 *
 * Note that USound is not robust and panic on every error, it is recommended
 * to run it with a restart on failure policy.
 */

#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pipewire/loop.h>
#include <pipewire/main-loop.h>
#include <pipewire/node.h>
#include <pipewire/pipewire.h>
#include <pipewire/proxy.h>
#include <spa/param/audio/raw.h>
#include <spa/param/param.h>
#include <spa/param/props.h>
#include <spa/pod/iter.h>
#include <spa/utils/list.h>
#include <systemd/sd-bus-vtable.h>
#include <systemd/sd-bus.h>

#define LOG_IMPLEMENTATION
#define LOG_MODULE "main"
#include "log.h"

#include "error.h"

#define VERSION "v0.1.0"

#define DBUS_SERVICE "dev.negrel.desk.USound"
#define DBUS_IFACE DBUS_SERVICE
#define DBUS_PATH "/dev/negrel/desk/USound"
#define DBUS_DEVICE_IFACE "dev.negrel.desk.USound.Device"

/**
 * Sound device kind. Currently, we only monitor input (source) and output
 * devices (sink).
 */
enum device_kind {
  DEVICE_KIND_UNKNOWN,
  DEVICE_KIND_SOURCE,
  DEVICE_KIND_SINK,
};

/**
 * USound internal state.
 */
typedef struct {
  // PipeWire.
  struct pw_main_loop *loop;
  struct pw_context *context;
  struct pw_core *core;
  struct pw_registry *registry;
  struct spa_hook registry_listener;
  struct spa_list devices;

  // D-Bus.
  sd_bus *bus;
  struct spa_source *bus_source;

  // Signal.
  struct spa_source *signal;
} usound_data;

/**
 * Tracked sound device data.
 */
typedef struct {
  // PipeWire.
  struct spa_list link;
  usound_data *usound;
  uint32_t id;
  const char *name;
  const char *desc;
  struct pw_proxy *proxy;
  struct spa_hook proxy_listener;
  struct pw_node *node;
  struct spa_hook node_listener;

  // D-Bus.
  sd_bus_slot *slot;
  const char *obj_path;

  // Properties.
  float volume;
  bool muted;
  enum device_kind kind;
} device_data;

static void device_free(device_data *node) {
  if (node == NULL)
    return;

  spa_list_remove(&node->link);
  spa_hook_remove(&node->proxy_listener);
  spa_hook_remove(&node->node_listener);
  if (node->proxy != NULL)
    pw_proxy_destroy(node->proxy);
  free((char *)node->name);
  free((char *)node->desc);
  sd_bus_slot_unref(node->slot);
  free((char *)node->obj_path);
  free(node);
}

static void handle_node_param(void *data, int seq, uint32_t id, uint32_t index,
                              uint32_t next, const struct spa_pod *param) {
  device_data *node = data;
  LOG_DBG("param changed name=%s seq=%d id=%u index=%u next=%u param=%p",
          node->name, seq, id, index, next, param);

  if (id != SPA_PARAM_Props || !param)
    return;

  // +1 for sentinel NULL.
  const char *changed[2 + 1] = {0};
  uint32_t n_changed = 0;

  float channels[SPA_AUDIO_MAX_CHANNELS] = {0};
  uint32_t n_channels = 0;

  struct spa_pod_prop *prop;
  SPA_POD_OBJECT_FOREACH((struct spa_pod_object *)param, prop) {
    switch (prop->key) {
      // This seems to always report 1.0 no matter the device.
#if 0
    case SPA_PROP_volume:
      spa_pod_get_float(&prop->value, &node->volume);
      LOG_DBG("volume %f", node->volume);
      break;
#endif

    case SPA_PROP_channelVolumes: {
      n_channels = spa_pod_copy_array(&prop->value, SPA_TYPE_Float, channels,
                                      SPA_AUDIO_MAX_CHANNELS);
      node->volume = 0.0;
      for (uint32_t i = 0; i < n_channels; i++)
        node->volume += cbrt(channels[i]) * 100.0;
      node->volume /= (double)n_channels;
      changed[n_changed++] = "Percentage";
      break;
    }

    case SPA_PROP_mute: {
      spa_pod_get_bool(&prop->value, &node->muted);
      changed[n_changed++] = "Muted";
      break;
    }
    }
  }

  int r = sd_bus_emit_properties_changed_strv(
      node->usound->bus, node->obj_path, DBUS_DEVICE_IFACE, (char **)changed);
  if (r < 0)
    LOG_ERR("failed to emit properties changed signal: %s", strerror(-r));

  LOG_INFO("node '%s' updated: volume %.0f%% %s", node->name, node->volume,
           node->muted ? "(muted)" : "");
}

static void on_info_changed(void *data, const struct pw_node_info *info) {
  device_data *node = data;
  LOG_DBG("info changed node=%s info=%p change-mask=%lu state=%s error=%s",
          node->name, (void *)info, info->change_mask,
          pw_node_state_as_string(info->state), info->error);

  if ((info->change_mask & PW_NODE_CHANGE_MASK_PARAMS) != 0)
    pw_node_enum_params(node->node, 0, SPA_PARAM_Props, 0, -1, NULL);
}

static const struct pw_node_events node_events = {
    .version = PW_VERSION_NODE_EVENTS,
    .param = handle_node_param,
    .info = on_info_changed,
};

static void proxy_removed(void *data) {
  device_data *dev = data;
  LOG_INFO("audio device proxy removed: %s %d", dev->name, dev->id);
  int r = sd_bus_emit_object_removed(dev->usound->bus, dev->obj_path);
  if (r < 0)
    LOG_ERR("failed to emit D-Bus signals after adding an object: %s",
            strerror(-r));

  device_free(dev);
}

static const struct pw_proxy_events proxy_events = {
    .version = PW_VERSION_PROXY_EVENTS,
    .removed = proxy_removed,
};

static int dbus_get_Version(struct sd_bus *bus, const char *path,
                            const char *interface, const char *property,
                            sd_bus_message *reply, void *userdata,
                            sd_bus_error *error) {
  (void)bus;
  (void)path;
  (void)interface;
  (void)property;
  (void)userdata;
  (void)error;
  return sd_bus_message_append(reply, "s", VERSION);
}

static int dbus_enumerate_devices(sd_bus_message *m, void *userdata,
                                  sd_bus_error *ret_error) {

  (void)ret_error;
  usound_data *usound = userdata;

  sd_bus_message *reply = NULL;
  SDBUS_PANIC(sd_bus_message_new_method_return(m, &reply),
              "failed to create sd-bus method reply message");

  SDBUS_PANIC(sd_bus_message_open_container(reply, 'a', "o"),
              "failed to open method reply array");

  device_data *dev = NULL;
  spa_list_for_each(dev, &usound->devices, link) {
    sd_bus_message_append(reply, "o", dev->obj_path);
  }

  SDBUS_PANIC(sd_bus_message_close_container(reply),
              "failed to close method reply array");

  return sd_bus_message_send(reply);
}

static const sd_bus_vtable usound_vtable[] = {
    SD_BUS_VTABLE_START(SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("Version", "s", dbus_get_Version, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("EnumerateDevices", "", "ao", dbus_enumerate_devices,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

#define DBUS_DEVICE_GETTER(prop, type, expr)                                   \
  static int dbus_device_get_##prop(                                           \
      struct sd_bus *bus, const char *path, const char *interface,             \
      const char *property, sd_bus_message *reply, void *userdata,             \
      sd_bus_error *error) {                                                   \
    (void)bus;                                                                 \
    (void)path;                                                                \
    (void)property;                                                            \
    (void)error;                                                               \
                                                                               \
    LOG_DBG("getter %s.%s on %s", interface, property, path);                  \
                                                                               \
    device_data *device = userdata;                                            \
    return sd_bus_message_append(reply, type, (expr));                         \
  }

DBUS_DEVICE_GETTER(Name, "s", device->name);
DBUS_DEVICE_GETTER(Description, "s", device->desc);
DBUS_DEVICE_GETTER(Muted, "b", device->muted);
DBUS_DEVICE_GETTER(Percentage, "d", (double)device->volume);

/**
 * D-Bus device object virtual table.
 */
static const sd_bus_vtable device_vtable[] = {
    SD_BUS_VTABLE_START(SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("Name", "s", dbus_device_get_Name, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Description", "s", dbus_device_get_Description, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Percentage", "d", dbus_device_get_Percentage, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Muted", "b", dbus_device_get_Muted, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_VTABLE_END,
};

static void encode_object_path(char *path) {
  while (*path != '\0') {
    char c = *path;
    if (c != '_' && !(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') &&
        !(c >= '0' && c <= '9') && c != '/') {
      *path = '_';
    }
    path++;
  }
}

static void registry_event_global(void *data, uint32_t id, uint32_t permissions,
                                  const char *type, uint32_t version,
                                  const struct spa_dict *props) {
  LOG_DBG("registy event global id=%u permissions=%u type=%s "
          "version=%u props=%p",
          id, permissions, type, version, (void *)props);

  if (props == NULL || props->n_items == 0)
    return;

  usound_data *d = data;
  device_data *dev = NULL;
  enum device_kind device_kind = DEVICE_KIND_UNKNOWN;

  // Check that device is a sink or a source.
  const char *media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
  if (media_class == NULL)
    return;
  if (strcmp(media_class, "Audio/Sink") == 0)
    device_kind = DEVICE_KIND_SINK;
  if (strcmp(media_class, "Audio/Source") == 0)
    device_kind = DEVICE_KIND_SOURCE;

  if (device_kind == DEVICE_KIND_UNKNOWN)
    return;

  // Create device.
  dev = calloc(1, sizeof(*dev));
  if (dev == NULL)
    LOG_FATAL("failed to allocate new device");

  // Bind it to pipewire.
  dev->proxy = pw_registry_bind(d->registry, id, type, version, sizeof(*dev));
  ERRNO_PANIC(dev->proxy == NULL ? -1 : 0,
              "failed to bind device to piperwire registry");

  dev->usound = d;
  dev->id = id;
  dev->kind = device_kind;
  dev->name = strdup(spa_dict_lookup(props, PW_KEY_NODE_NAME));
  dev->desc = strdup(spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION));

  dev->node = (struct pw_node *)dev->proxy;
  pw_proxy_add_listener(dev->proxy, &dev->proxy_listener, &proxy_events, dev);
  pw_node_add_listener(dev->node, &dev->node_listener, &node_events, dev);
  pw_node_enum_params(dev->node, 0, SPA_PARAM_Props, 0, -1, NULL);

  // Adding device object to D-Bus.
  int r = asprintf((char **)&dev->obj_path, DBUS_PATH "/devices/%s", dev->name);
  ERRNO_PANIC(r, "failed to allocate D-Bus object path");
  encode_object_path((char *)dev->obj_path);
  SDBUS_PANIC(sd_bus_add_object_vtable(d->bus, &dev->slot, dev->obj_path,
                                       DBUS_DEVICE_IFACE, device_vtable, dev),
              "failed to add device object to D-Bus");

  SDBUS_PANIC(sd_bus_add_object_manager(d->bus, NULL, dev->obj_path),
              "failed to add D-Bus object manager to device");

  SDBUS_PANIC(sd_bus_emit_object_added(d->bus, dev->obj_path),
              "failed to emit D-Bus signals after adding an object");

  spa_list_append(&d->devices, &dev->link);
  LOG_INFO("audio device added: %s %d", dev->name, dev->id);
  return;
}

static const struct pw_registry_events registry_events = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = registry_event_global,
};

// Callback called by pipewire event loop on D-Bus event.
static void bus_dispatch_cb(void *data, int fd, unsigned int mask) {
  (void)fd;
  (void)mask;
  usound_data *d = data;

  int r;
  while ((r = sd_bus_process(d->bus, NULL)) > 0)
    ; // Process all pending messages

  if (r < 0)
    LOG_ERR("failed to process pending D-Bus messages: %s", strerror(-r));
}

static void print_usage(char *prog_name) {
  static const char header[] = "usound " VERSION "\n"
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

static void signal_handler(void *data, int signo) {
  if (signo == SIGINT) {
    LOG_INFO("Received SIGINT. Cleaning up...");
    struct pw_main_loop *loop = data;
    pw_main_loop_quit(loop);
  }
}

int main(int argc, char *argv[]) {
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

    int c = getopt_long(argc, argv, "hl:", long_options, NULL);
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

  usound_data data = {0};
  spa_list_init(&data.devices);

  // Setup D-Bus & request name.
  SDBUS_PANIC(sd_bus_default_user(&data.bus),
              "failed to connect to user D-Bus");
  SDBUS_PANIC(sd_bus_request_name(data.bus, DBUS_SERVICE, 0),
              "failed to acquire bus name");

  // Add USound D-Bus object.
  SDBUS_PANIC(sd_bus_add_object_vtable(data.bus, NULL, DBUS_PATH, DBUS_IFACE,
                                       usound_vtable, &data),
              "failed to add USound object to D-Bus");

  // Setup PipeWire.
  pw_init(&argc, &argv);

  data.loop = pw_main_loop_new(NULL);
  ERRNO_PANIC(data.loop == NULL ? -1 : 0,
              "failed to create PipeWire main loop");

  struct pw_loop *loop = pw_main_loop_get_loop(data.loop);
  data.context = pw_context_new(loop, NULL, 0);
  ERRNO_PANIC(data.context == NULL ? -1 : 0,
              "failed to create PipeWire context");

  data.core = pw_context_connect(data.context, NULL, 0);
  ERRNO_PANIC(data.core == NULL ? -1 : 0, "failed to connect to PipeWire");

  data.registry = pw_core_get_registry(data.core, PW_VERSION_REGISTRY, 0);
  ERRNO_PANIC(data.registry == NULL ? -1 : 0,
              "failed to retrieve PipeWire registry");

  pw_registry_add_listener(data.registry, &data.registry_listener,
                           &registry_events, &data);

  // Integrate D-Bus with PipeWire event loop.
  data.bus_source =
      pw_loop_add_io(loop, sd_bus_get_fd(data.bus), SPA_IO_IN | SPA_IO_ERR,
                     false, bus_dispatch_cb, &data);
  ERRNO_PANIC(data.bus_source == NULL ? -1 : 0,
              "failed to add sd-bus I/O handle to PipeWire event loop");

  // Remove default SIGINT handler.
  sigset_t ss = {0};
  sigemptyset(&ss);
  sigaddset(&ss, SIGINT);
  ERRNO_PANIC(sigprocmask(SIG_BLOCK, &ss, NULL), "failed to set signal mask");

  // Setup signal handler.
  data.signal = pw_loop_add_signal(loop, SIGINT, signal_handler, data.loop);
  ERRNO_PANIC(data.signal == NULL ? -1 : 0,
              "failed to retrieve add signal handler to PipeWire event loop");

  LOG_INFO("starting event loop...");
  ERRNO_PANIC(pw_main_loop_run(data.loop), "PipeWire main loop failed");

  pw_proxy_destroy((struct pw_proxy *)data.registry);
  pw_core_disconnect(data.core);
  pw_context_destroy(data.context);
  pw_loop_destroy_source(loop, data.signal);
  pw_loop_destroy_source(loop, data.bus_source);
  pw_main_loop_destroy(data.loop);
  sd_bus_unref(data.bus);
  log_deinit();

  return EXIT_SUCCESS;
}
