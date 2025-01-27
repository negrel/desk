// Single header file notification library based on sd-bus.

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
  notification_hint hints;
  size_t n_hints;
} notification;

/**
 * Send a notification on the D-Bus and returns sd-bus errno.
 */
int notify(sd_bus *bus, notification *notif, uint32_t *notif_id,
           sd_bus_error *error) {
  int r = 0;
  sd_bus_message *reply = NULL;
  sd_bus_message *m = NULL;

  r = sd_bus_message_new_method_call(bus, &m, "org.freedesktop.Notifications",
                                     "/org/freedesktop/Notifications",
                                     "org.freedesktop.Notifications", "Notify");
  if (r < 0)
    goto cleanup;

  // TODO: supports actions.
  r = sd_bus_message_append(m, "susssas", notif->app ? notif->app : "",
                            notif->replace_id, notif->icon ? notif->icon : "",
                            notif->title ? notif->title : "",
                            notif->body ? notif->body : "", 0, NULL);
  if (r < 0)
    goto cleanup;

  r = sd_bus_message_open_container(m, 'a', "{sv}");
  if (r < 0)
    goto cleanup;

  r = sd_bus_message_open_container(m, 'e', "sv");
  if (r < 0)
    goto cleanup;

  // Apply hints.
  for (size_t i = 0; i < notif->n_hints; i++) {
    r = (*notif->hints)(m);
    if (r < 0)
      goto cleanup;
  }

  r = sd_bus_message_close_container(m);
  if (r < 0)
    goto cleanup;

  r = sd_bus_message_close_container(m);
  if (r < 0)
    goto cleanup;

  r = sd_bus_message_append(m, "i", notif->timeout, NULL);
  if (r < 0)
    goto cleanup;

  r = sd_bus_call(bus, m, -1, error, &reply);
  if (r < 0)
    goto cleanup;

  // Read notification id.
  if (notif_id != NULL) {
    r = sd_bus_message_read(reply, "u", notif_id, NULL);
    if (r < 0)
      goto cleanup;
  }

cleanup:
  if (m != NULL)
    sd_bus_message_unref(m);
  return r;
}

/**
 * Notification urgency levels hint.
 *
 * https://specifications.freedesktop.org/notification-spec/latest/urgency-levels.html
 */
static enum {
  NOTIFY_URGENCY_LOW = 0,
  NOTIFY_URGENCY_NORMAL = 1,
  NOTIFY_URGENCY_HIGH = 2,
} notification_urgency;

/**
 * Notification low urgency level hints.
 */
int notification_urgency_low(sd_bus_message *m) {
  int r = sd_bus_message_append(m, "s", "urgency", NULL);
  if (r < 0)
    return r;
  return sd_bus_message_append(m, "v", "u", NOTIFY_URGENCY_LOW, NULL);
}

/**
 * Notification normal urgency level hints.
 */
int notification_urgency_normal(sd_bus_message *m) {
  int r = sd_bus_message_append(m, "s", "urgency", NULL);
  if (r < 0)
    return r;
  return sd_bus_message_append(m, "v", "u", NOTIFY_URGENCY_NORMAL, NULL);
}

/**
 * Notification high urgency level hints.
 */
int notification_urgency_high(sd_bus_message *m) {
  int r = sd_bus_message_append(m, "s", "urgency", NULL);
  if (r < 0)
    return r;
  return sd_bus_message_append(m, "v", "u", NOTIFY_URGENCY_HIGH, NULL);
}

/**
 * Forcefully close a notification and removed from user's view.
 */
int notification_close(sd_bus *bus, uint32_t notif_id, sd_bus_error *error) {
  return sd_bus_call_method(
      bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
      "org.freedesktop.Notifications", "CloseNotification", error, NULL, "u",
      notif_id, NULL);
}

#endif
