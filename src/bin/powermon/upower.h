// UPower data types and helper functions.

#include <systemd/sd-bus.h>

#include "error.h"

#ifndef LOG_MODULE
#define LOG_MODULE "upower"
#endif
#include "log.h"

// UPower battery state.
// https://upower.freedesktop.org/docs/Device.html#id-1.2.4.8.83
enum upower_state {
  UPOWER_STATE_UNKNOWN = 0,
  UPOWER_STATE_CHARGING,
  UPOWER_STATE_DISCHARGING,
  UPOWER_STATE_EMPTY,
  UPOWER_STATE_FULLY_CHARGED,
  UPOWER_STATE_PENDING_CHARGE,
  UPOWER_STATE_PENDING_DISCHARGE,
};

// UPower device type.
// https://upower.freedesktop.org/docs/Device.html#id-1.2.4.8.17
enum upower_device_type {
  UPOWER_DEVICE_UNKNOWN = 0,
  UPOWER_DEVICE_LINE_POWER,
  UPOWER_DEVICE_BATTERY,
  UPOWER_DEVICE_UPS,
  UPOWER_DEVICE_MONITOR,
  UPOWER_DEVICE_MOUSE,
  UPOWER_DEVICE_KEYBOARD,
  UPOWER_DEVICE_PDA,
  UPOWER_DEVICE_PHONE,
  UPOWER_DEVICE_MEDIA_PLAYER,
  UPOWER_DEVICE_TABLET,
  UPOWER_DEVICE_COMPUTER,
  UPOWER_DEVICE_GAMING_INPUT,
  UPOWER_DEVICE_PEN,
  UPOWER_DEVICE_TOUCHPAD,
  UPOWER_DEVICE_MODEM,
  UPOWER_DEVICE_NETWORK,
  UPOWER_DEVICE_HEADSET,
  UPOWER_DEVICE_SPEAKERS,
  UPOWER_DEVICE_HEADPHONES,
  UPOWER_DEVICE_VIDEO,
  UPOWER_DEVICE_OTHER_AUDIO,
  UPOWER_DEVICE_REMOTE_CONTROL,
  UPOWER_DEVICE_PRINTER,
  UPOWER_DEVICE_SCANNER,
  UPOWER_DEVICE_CAMERA,
  UPOWER_DEVICE_WEARABLE,
  UPOWER_DEVICE_TOY,
  UPOWER_DEVICE_BLUETOOTH_GENERIC,
};

// UPower battery level.
// https://upower.freedesktop.org/docs/Device.html#id-1.2.4.8.105
enum upower_battery_level {
  UPOWER_BATTERY_LEVEL_UNKNOWN = 0,
  UPOWER_BATTERY_LEVEL_NONE,
  UPOWER_BATTERY_LEVEL_LOW,
  UPOWER_BATTERY_LEVEL_CRITICAL,
  UPOWER_BATTERY_LEVEL_NORMAL,
  UPOWER_BATTERY_LEVEL_HIGH,
  UPOWER_BATTERY_LEVEL_FULL,
};

void for_all_batteries(sd_bus *bus, void *data,
                       void (*cb)(sd_bus *bus, const char *path, void *data)) {
  sd_bus_message *reply = NULL;

  SDBUS_PANIC(sd_bus_call_method(bus, "org.freedesktop.UPower",
                                 "/org/freedesktop/UPower",
                                 "org.freedesktop.UPower", "EnumerateDevices",
                                 NULL, &reply, ""),
              "failed to call UPower method to enumerate devices");

  SDBUS_PANIC(sd_bus_message_enter_container(reply, 'a', "o"),
              "failed to enter UPower.EnumerateDevices array");

  const char *path;
  while (sd_bus_message_read(reply, "o", &path) > 0) {
    LOG_DBG("UPower device path: %s", path);

    sd_bus_message *type_reply = NULL;
    uint32_t type = 0;
    SDBUS_PANIC(sd_bus_get_property(bus, "org.freedesktop.UPower", path,
                                    "org.freedesktop.UPower.Device", "Type",
                                    NULL, &type_reply, "u"),
                "failed to get type of UPower power device");
    SDBUS_PANIC(sd_bus_message_read(type_reply, "u", &type),
                "failed to read UPower device 'Type' property");

    if (type == UPOWER_DEVICE_BATTERY) {
      LOG_DBG("UPower device '%s' is a battery", path);
      cb(bus, path, data);
    }
    sd_bus_message_unref(type_reply);
  }

  sd_bus_message_unref(reply);
}
