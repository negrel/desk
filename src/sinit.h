#ifndef SINIT_H_INCLUDE
#define SINIT_H_INCLUDE

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-egl-core.h>
#include <wayland-egl.h>

#ifndef LOG_MODULE
#define LOG_MODULE "sinit"
#endif
#include "log.h"

#include "stable/presentation-time/presentation-time.h"
#include "stable/xdg-shell/xdg-shell.h"

/**
 * A surface initialization library that provides easy ways to create rendering
 * surfaces (layer shell, XDG shell / window). Current implementation is
 * EGL + Wayland only.
 */

union sinit_surface;
typedef void (*sinit_render_fn)(union sinit_surface *surf, int width,
                                int height, uint32_t time, void *userdata);

struct sinit_surface_config {
  int width;
  int height;
};

struct sinit_base_surface {
  int type;
  struct wl_surface *wl_surface;
  struct wl_region *region;
  struct wl_egl_window *egl_window;
  struct wl_egl_surface *egl_surface;
  struct wl_callback *wl_callback;

  // Config.
  struct sinit_surface_config pending_config;
  struct sinit_surface_config active_config;

  sinit_render_fn render;
  uint32_t prev_render;
  void *userdata;
  bool closed;
};

/**
 * A window or XDG shell surface in Wayland terms.
 */
struct sinit_xdg_surface {
  struct sinit_base_surface base;
  struct xdg_toplevel *xdg_toplevel;
  struct xdg_surface *xdg_surface;
  bool closed;
};

/**
 * A generic surface type.
 */
typedef union sinit_surface {
  struct sinit_base_surface base;
  struct sinit_xdg_surface xdg;
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

  // EGL
  EGLDisplay egl_display;
  EGLConfig egl_conf;
  EGLSurface egl_surface;
  EGLContext egl_context;

  // EGL + Wayland
  struct wl_egl_window *egl_window;

  // General data.
  const char *app_id;
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
        wl_registry_bind(registry, name, &wl_compositor_interface, 5);
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
  } else {
    LOG_DBG("global %d removed", name);
  }
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

static void sinit_surface_request_frame(sinit_surface *surf);
static void sinit_surface_render(sinit_surface *surf, uint32_t time);
static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
                                  uint32_t serial) {
  (void)xdg_surface;

  sinit_surface *surf = data;

  LOG_DBG("xdg surface configure serial=%d", serial);

  xdg_surface_ack_configure(surf->xdg.xdg_surface, serial);

  bool resized =
      surf->base.active_config.width != surf->base.pending_config.width ||
      surf->base.active_config.height != surf->base.pending_config.height;

  if (resized) {
    LOG_DBG("surface resized from w=%d h=%d to w=%d h=%d",
            surf->base.active_config.width, surf->base.active_config.height,
            surf->base.pending_config.width, surf->base.pending_config.height);

    // Update opaque region.
    if (surf->base.region != NULL) {
      wl_region_subtract(surf->base.region, 0, 0,
                         surf->base.active_config.width,
                         surf->base.active_config.height);
      wl_region_add(surf->base.region, 0, 0, surf->base.pending_config.width,
                    surf->base.pending_config.height);
      wl_surface_set_opaque_region(surf->base.wl_surface, surf->base.region);
    }

    wl_egl_window_resize(surf->base.egl_window, surf->base.pending_config.width,
                         surf->base.pending_config.height, 0, 0);
  }

  surf->base.active_config = surf->base.pending_config;

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

  surf->base.pending_config = (struct sinit_surface_config){
      .width = width,
      .height = height,
  };
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

  if (!surf->base.closed)
    sinit_surface_render(surf, time);
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_done,
};

/* Private helper function */

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
}

static void deinit_wayland(struct sinit_state *s) {
  xdg_wm_base_destroy(s->shell);
  wl_shm_destroy(s->shm);
  wl_registry_destroy(s->registry);
  wl_compositor_destroy(s->compositor);
  wl_display_disconnect(s->display);
}

static void init_egl(struct sinit_state *s) {
  EGLint major, minor, count, n, size;
  EGLConfig *configs;
  int i;
  EGLint config_attribs[] = {EGL_SURFACE_TYPE,
                             EGL_WINDOW_BIT,
                             EGL_RED_SIZE,
                             8,
                             EGL_GREEN_SIZE,
                             8,
                             EGL_BLUE_SIZE,
                             8,
                             EGL_ALPHA_SIZE,
                             8,
                             EGL_RENDERABLE_TYPE,
                             EGL_OPENGL_ES2_BIT,
                             EGL_NONE};

  static const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                           EGL_NONE};

  s->egl_display = eglGetDisplay((EGLNativeDisplayType)s->display);
  if (s->egl_display == EGL_NO_DISPLAY)
    LOG_FATAL("failed to create EGL display");
  else
    LOG_DBG("EGL display created");

  if (eglInitialize(s->egl_display, &major, &minor) != EGL_TRUE)
    LOG_FATAL("failed to initialize EGL display");

  LOG_DBG("EGL major: %d, minor %d", major, minor);

  eglGetConfigs(s->egl_display, NULL, 0, &count);
  LOG_DBG("EGL has %d configs", count);

  configs = calloc(count, sizeof(*configs));

  eglChooseConfig(s->egl_display, config_attribs, configs, count, &n);

  for (i = 0; i < n; i++) {
    eglGetConfigAttrib(s->egl_display, configs[i], EGL_BUFFER_SIZE, &size);
    LOG_DBG("buffer size for EGL config %d is %d", i, size);
    eglGetConfigAttrib(s->egl_display, configs[i], EGL_RED_SIZE, &size);
    LOG_DBG("red size for EGL config %d is %d", i, size);

    s->egl_conf = configs[i];
    break;
  }

  free(configs);

  s->egl_context = eglCreateContext(s->egl_display, s->egl_conf, EGL_NO_CONTEXT,
                                    context_attribs);
  if (s->egl_context == EGL_NO_CONTEXT)
    LOG_FATAL("failed to create egl context");
}

static void deinit_egl(struct sinit_state *s) {
  if (s->egl_context != EGL_NO_CONTEXT) {
    eglDestroyContext(s->egl_display, s->egl_conf);
    s->egl_context = EGL_NO_CONTEXT;
  }

  if (s->egl_display != EGL_NO_DISPLAY) {
    eglTerminate(s->egl_display);
    s->egl_display = EGL_NO_DISPLAY;
  }

  eglReleaseThread();
}

/* Surface initialization public API */

/**
 * Initialize library state and connect to display server. This function panics
 * if it failed to do so.
 */
void sinit_init(const char *app_id) {
  state.app_id = app_id;
  init_wayland(&state);
  init_egl(&state);
}

/**
 * Process all incoming events and returns a non-negative integer on success and
 * -1 on error.
 */
int sinit_run() { return wl_display_dispatch(state.display); }

/**
 * Deinitialize library state and free associated resources.
 */
void sinit_deinit() {
  deinit_egl(&state);
  deinit_wayland(&state);
}

/* Surface methods */

bool sinit_surface_closed(sinit_surface *surf) { return surf->base.closed; }

void sinit_surface_request_frame(sinit_surface *surf) {
  if (surf->base.wl_callback == NULL) {
    surf->base.wl_callback = wl_surface_frame(surf->base.wl_surface);
    wl_callback_add_listener(surf->base.wl_callback, &frame_listener, surf);
  }
}

static void sinit_surface_render(sinit_surface *surf, uint32_t time) {
  if (surf->base.egl_surface == NULL)
    return;

  eglMakeCurrent(state.egl_display, surf->base.egl_surface,
                 surf->base.egl_surface, state.egl_context);

  surf->base.render(surf, surf->base.active_config.width,
                    surf->base.active_config.height, time, surf->base.userdata);

  eglSwapBuffers(state.egl_display, surf->base.egl_surface);
  wl_surface_commit(surf->base.wl_surface);

  surf->base.prev_render = time;
}

/* XDG Shell surface methods */

void sinit_xdg_surface_init(sinit_surface *surf, int width, int height,
                            bool opaque, sinit_render_fn render,
                            void *userdata) {
  surf->base.type = 1;
  surf->base.render = render;
  surf->base.userdata = userdata;

  // Wayland surface
  surf->base.wl_surface = wl_compositor_create_surface(state.compositor);
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

  // EGL surface.
  surf->base.egl_window =
      wl_egl_window_create(surf->base.wl_surface, width, height);
  if (surf->base.egl_window == EGL_NO_SURFACE)
    LOG_FATAL("failed to create EGL window");
  else
    LOG_DBG("EGL window created");

  surf->base.egl_surface =
      eglCreateWindowSurface(state.egl_display, state.egl_conf,
                             (EGLNativeWindowType)surf->base.egl_window, NULL);
  if (surf->base.egl_surface == EGL_NO_SURFACE)
    LOG_FATAL("failed to create EGL surface");
  else
    LOG_DBG("EGL surface created");

  // Commit surface.
  wl_surface_commit(surf->base.wl_surface);
}

void sinit_xdg_surface_deinit(sinit_surface *surf) {
  // Destroy EGL resources.
  if (surf->base.egl_surface != EGL_NO_SURFACE) {
    eglDestroySurface(state.egl_display, surf->base.egl_surface);
    surf->base.egl_surface = EGL_NO_SURFACE;
  }
  if (surf->base.egl_window != NULL)
    wl_egl_window_destroy(surf->base.egl_window);

  // Destroy Wayland resources.
  if (surf->xdg.xdg_toplevel != NULL)
    xdg_toplevel_destroy(surf->xdg.xdg_toplevel);
  if (surf->xdg.xdg_surface != NULL)
    xdg_surface_destroy(surf->xdg.xdg_surface);
  if (surf->base.region != NULL)
    wl_region_destroy(surf->base.region);
  if (surf->base.wl_surface != NULL)
    wl_surface_destroy(surf->base.wl_surface);
}

#endif
