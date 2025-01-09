#ifndef NOTIFY_H_INCLUDE
#define NOTIFY_H_INCLUDE

#include <stdint.h>
#include <systemd/sd-bus.h>

/**
 * Notification hint function that adds hint to the given message.
 */
typedef int (*notification_hint)(sd_bus_message *m);

/**
 * Parameters of a notification.
 */
typedef struct {
  const char *app;
  const char *title;
  const char *body;
  const char *body_markup;
  const char *icon;
  uint32_t replace_id;
  uint32_t timeout;
  notification_hint *hints;
  size_t n_hints;
} notification_t;

/**
 * Send a notification on the D-Bus and returns sd-bus errno.
 */
int notify(sd_bus *bus, notification_t *notif, uint32_t *notif_id) {
  int r = 0;
  sd_bus_message *reply = NULL;
  sd_bus_error error = SD_BUS_ERROR_NULL;

  r = sd_bus_call_method(
      bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
      "org.freedesktop.Notifications", "Notify", &error, &reply,
      "susssasa{sv}i", notif->app ? notif->app : "", notif->replace_id,
      notif->icon ? notif->icon : "", notif->title ? notif->title : "",
      notif->body ? notif->body : "", 0, 0, notif->timeout, NULL);
  if (r < 0) {
    fprintf(stderr, "Failed to send notification: %s\n", error.message);
    goto cleanup;
  }

  // Read notification id.
  if (notif->replace_id == 0 && notif_id != NULL)
    if (sd_bus_message_read(reply, "u", notif_id) < 0)
      goto cleanup;

cleanup:
  if (reply != NULL)
    sd_bus_message_unref(reply);
  return r;
}

#endif
