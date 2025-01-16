#include "sinit.h"

#ifndef LOG_MODULE
#define LOG_MODULE "sinit"
#endif
#include "log.h"

#include "stable/presentation-time/presentation-time.h"
#include "stable/xdg-shell/xdg-shell.h"
#include "wlr/unstable/wlr-layer-shell-unstable-v1.h"

struct sinit_surface_config {
  int width;
  int height;
};

enum sinit_surface_type {
  SINIT_RAW_SURFACE,
  SINIT_XDG_TOP_LEVEL_SURFACE,
  SINIT_LAYER_SHELL_SURFACE,
};

struct sinit_base_surface {
  enum sinit_surface_type type;
  struct wl_surface *wl_surface;
  struct wl_region *region;
  struct wl_callback *wl_callback;
  struct wl_buffer *wl_buffer;
  int factor;
  void *buffer;

  // Config.
  struct sinit_surface_config config;

  sinit_render_fn render;
  uint32_t prev_render;
  void *userdata;
  bool closed;
};

/**
 * A window or XDG shell surface in Wayland terms.
 */
struct sinit_xdg_toplevel_surface {
  struct sinit_base_surface base;
  struct xdg_toplevel *xdg_toplevel;
  struct xdg_surface *xdg_surface;

  // Config.
  struct sinit_surface_config pending_config;
};

/**
 * A layer shell surface.
 */
struct sinit_layer_surface {
  struct sinit_base_surface base;
  struct zwlr_layer_surface_v1 *layer_surface;
};

/**
 * A generic surface type.
 */
typedef union sinit_surface {
  struct sinit_base_surface base;
  struct sinit_xdg_toplevel_surface xdg;
  struct sinit_layer_surface layer;
} sinit_surface;

struct sinit_state {
  // Wayland.
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  uint32_t compositor_name;
  struct wl_shm *shm;
  uint32_t shm_name;
  struct xdg_wm_base *shell;
  uint32_t shell_name;
  struct wp_presentation *presentation;
  uint32_t presentation_name;
  struct zwlr_layer_shell_v1 *layer_shell;
  uint32_t layer_shell_name;

  // General data.
  const char *app_id;
};

/**
 * A surface initialization library that provides easy ways to create rendering
 * surfaces (layer shell, XDG shell / window). Current implementation is
 * Wayland only.
 */

union sinit_surface;
typedef void (*sinit_render_fn)(union sinit_surface *surf, void *buf, int width,
                                int height, int scale, uint32_t time,
                                void *userdata);

struct sinit_surface_config {
  int width;
  int height;
};

static struct sinit_state state = {0};

/* Wayland boilerplate */

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *shell,
                             uint32_t serial) {
  (void)data;
  xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = &xdg_wm_base_ping,
};

static void handle_global(void *data, struct wl_registry *registry,
                          uint32_t name, const char *interface,
                          uint32_t version) {
  (void)version;

  LOG_DBG("wayland global %d added", name);

  struct sinit_state *state = data;

  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    state->compositor =
        wl_registry_bind(registry, name, &wl_compositor_interface, 6);
    state->compositor_name = name;
  } else if (strcmp(interface, wl_shm_interface.name) == 0) {
    state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    state->shm_name = name;
  } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
    state->shell = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
    xdg_wm_base_add_listener(state->shell, &xdg_wm_base_listener, state);
    state->shell_name = name;
  } else if (strcmp(interface, wp_presentation_interface.name) == 0) {
    state->presentation =
        wl_registry_bind(registry, name, &wp_presentation_interface, 1);
    state->presentation_name = name;
  } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
    state->layer_shell =
        wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
    state->layer_shell_name = name;
  }
}

static void handle_global_remove(void *data, struct wl_registry *registry,
                                 uint32_t name) {
  (void)registry;

  struct sinit_state *state = data;
  if (name == state->compositor_name) {
    LOG_FATAL("global wayland compositor removed");
  } else if (name == state->shm_name) {
    LOG_FATAL("global wayland shm removed");
  } else if (name == state->shell_name) {
    LOG_FATAL("global wayland shell removed");
  } else if (name == state->presentation_name) {
    LOG_FATAL("global wayland presentation removed");
  } else if (name == state->layer_shell_name) {
    LOG_FATAL("global wayland layer shell removed");
  } else {
    LOG_DBG("global %d removed", name);
  }
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

static void create_wl_buffer(struct wl_shm *shm, int width, int height,
                             struct wl_buffer **wl_buf, void **buf);
static void resize_surface(sinit_surface *surf, int width, int height,
                           int factor);
static void sinit_surface_request_frame(sinit_surface *surf);
static void sinit_surface_render(sinit_surface *surf, uint32_t time);
static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
                                  uint32_t serial) {
  (void)xdg_surface;

  sinit_surface *surf = data;

  LOG_DBG("xdg surface configure serial=%d", serial);

  xdg_surface_ack_configure(surf->xdg.xdg_surface, serial);

  bool resized = surf->base.config.width != surf->xdg.pending_config.width ||
                 surf->base.config.height != surf->xdg.pending_config.height;

  if (resized || surf->base.prev_render == 0) {
    int width = surf->xdg.pending_config.width;
    int height = surf->xdg.pending_config.height;

    LOG_DBG("surface resized from w=%d h=%d to w=%d h=%d",
            surf->base.config.width, surf->base.config.height, width, height);

    // Resize buffer.
    resize_surface(surf, surf->xdg.pending_config.width,
                   surf->xdg.pending_config.height, surf->base.factor);

    // Update opaque region.
    if (surf->base.region != NULL) {
      wl_region_subtract(surf->base.region, 0, 0, surf->base.config.width,
                         surf->base.config.height);
      wl_region_add(surf->base.region, 0, 0, width, height);
      wl_surface_set_opaque_region(surf->base.wl_surface, surf->base.region);
    }
  }

  surf->base.config = surf->xdg.pending_config;

  // Render first frame or schedule a new frame.
  if (surf->base.prev_render == 0)
    sinit_surface_render(surf, 0);
  else
    sinit_surface_request_frame(surf);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = &xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data,
                                   struct xdg_toplevel *xdg_toplevel,
                                   int32_t width, int32_t height,
                                   struct wl_array *states) {
  (void)xdg_toplevel;
  (void)states;

  sinit_surface *surf = data;

  LOG_DBG("xdg toplevel configure xdg_toplevel=%p width=%d height=%d",
          (void *)xdg_toplevel, width, height);

  if (width > 0)
    surf->xdg.pending_config.width = width;
  if (height > 0)
    surf->xdg.pending_config.height = height;
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
  (void)data;
  (void)xdg_toplevel;
  LOG_DBG("xdg toplevel closed xdg_toplevel=%p", (void *)xdg_toplevel);

  sinit_surface *surf = data;
  surf->base.closed = true;
}

static void xdg_toplevel_configure_bounds(void *data,
                                          struct xdg_toplevel *xdg_toplevel,
                                          int32_t width, int32_t height) {
  (void)data;
  (void)xdg_toplevel;
  (void)width;
  (void)height;

  LOG_DBG("xdg toplevel configure bounds xdg_toplevel=%p width=%d height=%d",
          (void *)xdg_toplevel, width, height);
}

static void xdg_toplevel_wm_capabilities(void *data,
                                         struct xdg_toplevel *xdg_toplevel,
                                         struct wl_array *caps) {
  (void)data;
  (void)xdg_toplevel;
  (void)caps;

  LOG_DBG("xdg toplevel wm capabilities xdg_toplevel=%p ",
          (void *)xdg_toplevel);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = &xdg_toplevel_configure,
    .close = &xdg_toplevel_close,
    .configure_bounds = &xdg_toplevel_configure_bounds,
    .wm_capabilities = xdg_toplevel_wm_capabilities,
};

static void frame_done(void *data, struct wl_callback *callback,
                       uint32_t time) {
  (void)time;
  sinit_surface *surf = data;

  wl_callback_destroy(callback);
  surf->base.wl_callback = NULL;

  sinit_surface_render(surf, time);
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_done,
};

static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *layer_surface,
                                    uint32_t serial, uint32_t width,
                                    uint32_t height) {
  sinit_surface *surf = data;

  LOG_DBG("layer surface configure width=%d height=%d serial=%d", width, height,
          serial);

  zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

  bool resized = false;

  if (surf->layer.base.config.width == 0) {
    surf->layer.base.config.width = width;
    resized = true;
  }
  if (surf->layer.base.config.height == 0) {
    surf->layer.base.config.height = height;
    resized = true;
  }

  if (resized || surf->base.prev_render == 0)
    resize_surface(surf, surf->layer.base.config.width,
                   surf->layer.base.config.height, surf->base.factor);

  // Render first frame or schedule a new frame.
  if (surf->base.prev_render == 0) {
    sinit_surface_render(surf, 0);
  } else
    sinit_surface_request_frame(surf);
}

static void layer_surface_closed(void *data,
                                 struct zwlr_layer_surface_v1 *layer_surface) {
  (void)layer_surface;
  sinit_surface *surf = data;
  surf->base.closed = true;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void surface_enter(void *data, struct wl_surface *wl_surface,
                          struct wl_output *output) {
  (void)data;
  (void)wl_surface;
  (void)output;
}

static void surface_leave(void *data, struct wl_surface *wl_surface,
                          struct wl_output *output) {
  (void)data;
  (void)wl_surface;
  (void)output;
}

static void surface_scale(void *data, struct wl_surface *surface,
                          int32_t factor) {
  (void)surface;

  sinit_surface *surf = data;

  LOG_DBG("surface scale surface=%p factor=%d", (void *)surf, factor);

  surf->base.factor = factor;
  sinit_surface_request_frame(surf);
}

static void surface_buffer_transform(void *data, struct wl_surface *wl_surface,
                                     uint32_t transform) {
  (void)data;
  (void)wl_surface;
  (void)transform;
}

static const struct wl_surface_listener surface_listener = {
    .enter = surface_enter,
    .leave = surface_leave,
    .preferred_buffer_scale = surface_scale,
    .preferred_buffer_transform = surface_buffer_transform,
};

/* Private helper function */

static int create_shm_file(size_t size) {
  char template[] = "/tmp/wayland-shm-XXXXXX";
  int fd = mkstemp(template);
  if (fd < 0)
    return -1;

  unlink(template);

  if (ftruncate(fd, size) < 0) {
    close(fd);
    return -1;
  }

  return fd;
}

static void create_wl_buffer(struct wl_shm *shm, int width, int height,
                             struct wl_buffer **wl_buf, void **buf) {
  int stride = 4 * width;
  int size = stride * height;

  int fd = create_shm_file(size);
  if (fd < 0)
    LOG_FATAL("failed to create shm file: %m (size=%d)", size);

  // Map memory
  void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    close(fd);
    LOG_FATAL("failed to mmap file: %m (size=%d)", size);
  }

  struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
  struct wl_buffer *buffer = wl_shm_pool_create_buffer(
      pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);

  close(fd);

  *buf = data;
  *wl_buf = buffer;
}

static void resize_surface(sinit_surface *surf, int width, int height,
                           int factor) {
  if (surf->base.wl_buffer != NULL) {
    wl_buffer_destroy(surf->base.wl_buffer);
    munmap(surf->base.buffer, surf->base.factor * surf->base.config.width *
                                  surf->base.config.height * 4);
  }
  create_wl_buffer(state.shm, width * factor, height * factor,
                   &surf->base.wl_buffer, &surf->base.buffer);
  wl_surface_attach(surf->base.wl_surface, surf->base.wl_buffer, 0, 0);
  wl_surface_set_buffer_scale(surf->base.wl_surface, factor);
}

static void init_wayland(struct sinit_state *s) {
  s->display = wl_display_connect(NULL);
  if (s->display == NULL)
    LOG_FATAL("failed to connect to wayland display");

  s->registry = wl_display_get_registry(s->display);
  if (s->display == NULL)
    LOG_FATAL("failed to get wayland registry");

  wl_registry_add_listener(s->registry, &registry_listener, s);
  if (wl_display_roundtrip(s->display) < 0)
    LOG_FATAL("wl_display_roundtrip() failed");

  if (s->compositor == NULL)
    LOG_FATAL("wl_compositor is missing");
  if (s->shell == NULL)
    LOG_FATAL("no XDG shell interface");
  if (s->shm == NULL)
    LOG_FATAL("compositor doesn't support wl_shm");
  if (s->presentation == NULL)
    LOG_FATAL("compositor doesn't support presentation time protocol");
  if (s->layer_shell == NULL)
    LOG_FATAL("compositor doesn't support wlr layer shell protocol");
}

static void deinit_wayland(struct sinit_state *s) {
  zwlr_layer_shell_v1_destroy(s->layer_shell);
  xdg_wm_base_destroy(s->shell);
  wl_shm_destroy(s->shm);
  wl_registry_destroy(s->registry);
  wl_compositor_destroy(s->compositor);
  wl_display_disconnect(s->display);
}

/* Surface initialization public API */

/**
 * Initialize library state and connect to display server. This function panics
 * if it failed to do so.
 */
void sinit_init(const char *app_id) {
  state.app_id = app_id;
  init_wayland(&state);
}

/**
 * Process all incoming events and returns a non-negative integer on success and
 * -1 on error.
 */
int sinit_run() { return wl_display_dispatch(state.display); }

/**
 * Returns file descriptor to poll on to detect new event.
 */
int sinit_fd() { return wl_display_get_fd(state.display); }

/**
 * Deinitialize library state and free associated resources.
 */
void sinit_deinit() { deinit_wayland(&state); }

/* Surface methods */

bool sinit_surface_closed(sinit_surface *surf) { return surf->base.closed; }

void sinit_surface_request_frame(sinit_surface *surf) {
  if (surf->base.wl_callback == NULL) {
    surf->base.wl_callback = wl_surface_frame(surf->base.wl_surface);
    wl_callback_add_listener(surf->base.wl_callback, &frame_listener, surf);
  }
}

static void sinit_surface_render(sinit_surface *surf, uint32_t time) {
  if (surf->base.closed)
    return;

  surf->base.render(surf, surf->base.buffer, surf->base.config.width,
                    surf->base.config.height, surf->base.factor, time,
                    surf->base.userdata);

  wl_surface_commit(surf->base.wl_surface);

  surf->base.prev_render = time;
}

/* XDG Shell surface methods */

void sinit_xdg_surface_init(sinit_surface *surf, int width, int height,
                            bool opaque, sinit_render_fn render,
                            void *userdata) {
  surf->base.type = SINIT_XDG_TOP_LEVEL_SURFACE;
  surf->base.factor = 1;
  surf->base.render = render;
  surf->base.userdata = userdata;
  surf->base.closed = false;
  surf->xdg.pending_config.width = width;
  surf->xdg.pending_config.height = height;

  // Wayland surface
  surf->base.wl_surface = wl_compositor_create_surface(state.compositor);
  wl_surface_add_listener(surf->base.wl_surface, &surface_listener, surf);
  surf->xdg.xdg_surface =
      xdg_wm_base_get_xdg_surface(state.shell, surf->base.wl_surface);
  xdg_surface_add_listener(surf->xdg.xdg_surface, &xdg_surface_listener, surf);
  surf->xdg.xdg_toplevel = xdg_surface_get_toplevel(surf->xdg.xdg_surface);
  xdg_toplevel_add_listener(surf->xdg.xdg_toplevel, &xdg_toplevel_listener,
                            surf);

  xdg_toplevel_set_app_id(surf->xdg.xdg_toplevel, state.app_id);

  // Wayland region.
  if (opaque) {
    surf->base.region = wl_compositor_create_region(state.compositor);
    wl_region_add(surf->base.region, 0, 0, width, height);
    wl_surface_set_opaque_region(surf->base.wl_surface, surf->base.region);
  }

  // Commit surface.
  wl_surface_commit(surf->base.wl_surface);
}

void sinit_xdg_toplevel_surface_deinit(sinit_surface *surf) {
  // Destroy Wayland resources.
  if (surf->base.wl_callback != NULL)
    wl_callback_destroy(surf->base.wl_callback);
  if (surf->base.wl_buffer != NULL) {
    wl_buffer_destroy(surf->base.wl_buffer);
    munmap(surf->base.buffer,
           surf->base.config.width * surf->base.config.height * 4);
  }
  if (surf->xdg.xdg_toplevel != NULL)
    xdg_toplevel_destroy(surf->xdg.xdg_toplevel);
  if (surf->xdg.xdg_surface != NULL)
    xdg_surface_destroy(surf->xdg.xdg_surface);
  if (surf->base.region != NULL)
    wl_region_destroy(surf->base.region);
  if (surf->base.wl_surface != NULL)
    wl_surface_destroy(surf->base.wl_surface);

  surf->base.type = SINIT_RAW_SURFACE;
}

/* Layer shell surface */

void sinit_layer_surface_init(sinit_surface *surf, enum sinit_layer layer,
                              enum sinit_anchor anchors, int exclusive,
                              int width, int height, bool opaque,
                              sinit_render_fn render, void *userdata) {
  surf->base.type = SINIT_LAYER_SHELL_SURFACE;
  surf->base.factor = 1;
  surf->base.render = render;
  surf->base.userdata = userdata;
  surf->base.closed = false;
  surf->base.config.width = width;
  surf->base.config.height = height;

  // Wayland surface.
  surf->base.wl_surface = wl_compositor_create_surface(state.compositor);
  wl_surface_add_listener(surf->base.wl_surface, &surface_listener, surf);
  surf->layer.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
      state.layer_shell, surf->base.wl_surface, NULL, layer, "");
  if (anchors != 0)
    zwlr_layer_surface_v1_set_anchor(surf->layer.layer_surface,
                                     (anchors & 0xF) | (anchors >> 4));
  if (anchors >= SINIT_ANCHOR_TOP_EXCLUSIVE)
    zwlr_layer_surface_v1_set_exclusive_zone(surf->layer.layer_surface,
                                             anchors >> 4);
  if (exclusive)
    zwlr_layer_surface_v1_set_exclusive_zone(surf->layer.layer_surface,
                                             exclusive);
  zwlr_layer_surface_v1_set_size(surf->layer.layer_surface, width, height);
  zwlr_layer_surface_v1_add_listener(surf->layer.layer_surface,
                                     &layer_surface_listener, surf);

  if (opaque) {
    surf->base.region = wl_compositor_create_region(state.compositor);
    wl_region_add(surf->base.region, 0, 0, width, height);
    wl_surface_set_opaque_region(surf->base.wl_surface, surf->base.region);
  }

  // Commit surface.
  wl_surface_commit(surf->base.wl_surface);
}

void sinit_layer_surface_margin(sinit_surface *surf, int top, int right,
                                int bottom, int left) {
  zwlr_layer_surface_v1_set_margin(surf->layer.layer_surface, top, right,
                                   bottom, left);
}

void sinit_layer_surface_deinit(sinit_surface *surf) {
  // Destroy Wayland resources.
  if (surf->base.wl_callback != NULL)
    wl_callback_destroy(surf->base.wl_callback);
  if (surf->base.wl_buffer != NULL) {
    wl_buffer_destroy(surf->base.wl_buffer);
    munmap(surf->base.buffer,
           surf->base.config.width * surf->base.config.height * 4);
  }
  if (surf->layer.layer_surface != NULL)
    zwlr_layer_surface_v1_destroy(surf->layer.layer_surface);
  if (surf->base.region != NULL)
    wl_region_destroy(surf->base.region);
  if (surf->base.wl_surface != NULL)
    wl_surface_destroy(surf->base.wl_surface);

  surf->base.type = SINIT_RAW_SURFACE;
}
