#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define LOG_MODULE "volumemon"
#include "log.h"

#include "sinit.h"

#define SOKOL_GLES3
#define SOKOL_IMPL
#include "sokol_gfx.h"
#include "sokol_gl.h"
#include "sokol_gp.h"

void slog_func(const char *tag, uint32_t log_level, uint32_t log_item,
               const char *message, uint32_t line_nr, const char *filename,
               void *user_data) {
  (void)user_data;
  (void)log_item;

  static const enum log_class level_map[] = {
      LOG_CLASS_ERROR,   // Panic
      LOG_CLASS_ERROR,   // Error
      LOG_CLASS_WARNING, // Warning
      LOG_CLASS_INFO,    // Info
  };

  log_msg(level_map[log_level], "sokol", filename, line_nr, "[%s] %s", tag,
          message);
}

void frame(sinit_surface *surf, int width, int height, uint32_t time,
           void *userdata) {
  (void)userdata;
  sinit_surface_request_frame(surf);

  static bool initialized = false;
  static sg_swapchain swapchain = {0};

  if (!initialized) {
    sg_desc desc = {0};
    desc.logger.func = slog_func;
    sg_setup(&desc);

    sgp_desc sgpdesc = {0};
    sgp_setup(&sgpdesc);
    if (!sgp_is_valid())
      LOG_FATAL("failed to create Sokol GP context: %s",
                sgp_get_error_message(sgp_get_last_error()));

    if (swapchain.gl.framebuffer == 0) {
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint *)&swapchain.gl.framebuffer);
      LOG_DBG("framebuffer %d", swapchain.gl.framebuffer);
    }

    initialized = true;
  }

  if (width <= 0 || height <= 0)
    return;

  // Sync swapchain buffer size.
  swapchain.width = width;
  swapchain.height = height;

  float ratio = width / (float)height;

  // Begin recording draw commands for a frame buffer of size (width, height).
  sgp_begin(width, height);
  // Set frame buffer drawing region to (0,0,width,height).
  sgp_viewport(0, 0, width, height);
  // Set drawing coordinate space to (left=-ratio, right=ratio, top=1,
  // bottom=-1).
  sgp_project(-ratio, ratio, 1.0f, -1.0f);

  // Clear the frame buffer.
  sgp_set_color(0.0f, 0.0f, 0.0f, 0.0f);
  sgp_clear();

  float t = time;

  // Draw an animated rectangle that rotates and changes its colors.
  float r = sinf(t) * 0.5 + 0.5, g = cosf(t) * 0.5 + 0.5;
  sgp_set_color(r, g, 0.3f, 1.0f);
  sgp_rotate_at(t * M_PI / 10000, 0.0f, 0.0f);
  sgp_draw_filled_rect(-0.5f, -0.5f, 1.0f, 1.0f);

  // Render.
  sg_pass pass = {.swapchain = swapchain};
  sg_begin_pass(&pass);
  sgp_flush();
  sgp_end();
  sg_end_pass();
  sg_commit();
}

int main(void) {
  // Setup log.
  log_init(LOG_COLORIZE_AUTO, false, LOG_FACILITY_USER, LOG_CLASS_DEBUG);
  LOG_DBG("log initialized");

  sinit_init("dev.negrel.desk.volumemon");

  sinit_surface surf = {0};
  sinit_xdg_surface_init(&surf, 460, 350, false, frame, &surf);

  while (!sinit_surface_closed(&surf)) {
    if (sinit_run() == -1)
      LOG_FATAL("event loop error");
  }
  LOG_DBG("event loop done, exiting...");

  sgp_shutdown();
  sg_shutdown();

  sinit_xdg_surface_deinit(&surf);
  sinit_deinit();

  return EXIT_SUCCESS;
}
