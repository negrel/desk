#ifndef SINIT_H_INCLUDE
#define SINIT_H_INCLUDE

#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include "stable/presentation-time/presentation-time.h"
#include "stable/xdg-shell/xdg-shell.h"
#include "wlr/unstable/wlr-layer-shell-unstable-v1.h"

/**
 * A surface initialization library that provides easy ways to create rendering
 * surfaces (layer shell, XDG shell / window). Current implementation is
 * Wayland only.
 */

typedef struct sinit_surface sinit_surface;

typedef void (*sinit_render_fn)(sinit_surface *surf, void *buf, int width,
                                int height, int scale, uint32_t time,
                                void *userdata);

/**
 * Layers at which a layer shell surface can be rendered in. They are ordered by
 * z-depth, bottom-most first.
 */
enum sinit_layer {
  SINIT_LAYER_BACKGROUND = 0,
  SINIT_LAYER_BOTTOM = 1,
  SINIT_LAYER_TOP = 2,
  SINIT_LAYER_OVERLAY = 3,
};

/**
 * Anchor bit flags to anchor layer shell surface to border of an output/screen.
 * You can apply multiple anchor my using bitwise OR:
 * enum sinit_anchor anchors = SINIT_ANCHOR_LEFT | SINIT_ANCHOR_RIGHT;
 */
enum sinit_anchor {
  SINIT_ANCHOR_NONE = 0,
  SINIT_ANCHOR_TOP = 1,
  SINIT_ANCHOR_BOTTOM = 2,
  SINIT_ANCHOR_LEFT = 4,
  SINIT_ANCHOR_RIGHT = 8,
  SINIT_ANCHOR_TOP_EXCLUSIVE = 16,
  SINIT_ANCHOR_BOTTOM_EXCLUSIVE = 32,
  SINIT_ANCHOR_LEFT_EXCLUSIVE = 64,
  SINIT_ANCHOR_RIGHT_EXCLUSIVE = 128,
};

/* Surface initialization public API */

/**
 * Initialize library state and connect to display server. This function panics
 * if it failed to do so.
 */
void sinit_init(const char *app_id);

/**
 * Process all incoming events and returns a non-negative integer on success and
 * -1 on error.
 */
int sinit_run();

/**
 * Returns file descriptor to poll on to detect new event.
 */
int sinit_fd();

/**
 * Deinitialize library state and free associated resources.
 */
void sinit_deinit();

/* Surface methods */

bool sinit_surface_closed(sinit_surface *surf);

void sinit_surface_request_frame(sinit_surface *surf);

/* XDG Shell surface methods */

/**
 * Initializes a surface as an XDG shell surface (e.g. a window).
 */
void sinit_xdg_surface_init(sinit_surface *surf, int width, int height,
                            bool opaque, sinit_render_fn render,
                            void *userdata);

/**
 * Deinitializes an XDG shell surface.
 */
void sinit_xdg_toplevel_surface_deinit(sinit_surface *surf);

/* Layer shell surface */

/**
 * Initializes a layer shell surface (e.g. not a window).
 */
void sinit_layer_surface_init(sinit_surface *surf, enum sinit_layer layer,
                              enum sinit_anchor anchors, int exclusive,
                              int width, int height, bool opaque,
                              sinit_render_fn render, void *userdata);

/**
 * Requests that the surface be placed some distance away from anchor point on
 * the output, in surface-local coordinates. Setting this value for edges you
 * are not anchored has no effect. The exclusive zone includes the margin.
 */
void sinit_layer_surface_margin(sinit_surface *surf, int top, int right,
                                int bottom, int left);

/**
 * Deinitializes a layer shell surface.
 */
void sinit_layer_surface_deinit(sinit_surface *surf);

#endif
