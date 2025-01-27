// Single-file header library for logging.

#ifndef LOG_H_INCLUDE
#define LOG_H_INCLUDE

#include <stdarg.h>
#include <stdbool.h>

#include "macros.h"

/**
 * Whether to color logs or not. LOG_COLORIZE_AUTO will colorize output if
 * stderr is a terminal.
 */
enum log_colorize {
  LOG_COLORIZE_NEVER,
  LOG_COLORIZE_ALWAYS,
  LOG_COLORIZE_AUTO
};

/**
 * Syslog facility. See man 3 openlog.
 */
enum log_facility { LOG_FACILITY_USER, LOG_FACILITY_DAEMON };

/**
 * Logging levels in order of decreasing importance (most critical to least
 * critical).
 */
enum log_class {
  LOG_CLASS_NONE,
  LOG_CLASS_ERROR,
  LOG_CLASS_WARNING,
  LOG_CLASS_INFO,
  LOG_CLASS_DEBUG,
  LOG_CLASS_COUNT,
};

/**
 * Initializes and configure log library.
 */
void log_init(enum log_colorize colorize, bool do_syslog,
              enum log_facility syslog_facility, enum log_class log_level);

/**
 * Deinitializes log library.
 */
void log_deinit(void);

/**
 * Parse string as a log level. -1 is returned on error.
 */
int log_level_from_string(const char *str);

/**
 * Logs functions and they're variable arguments derivative. You should not call
 * these functions directly as they're not really ergonomics. Use the
 * appropriate LOG_XXX macros.
 */

void log_msg(enum log_class log_class, const char *module, const char *file,
             int lineno, const char *fmt, ...) PRINTF(5);

noreturn void log_fatal(const char *module, const char *file, int lineno,
                        const char *fmt, ...) PRINTF(4);

noreturn void bug(const char *module, const char *file, int line,
                  const char *func, const char *fmt, ...);

void log_msg_va(enum log_class log_class, const char *module, const char *file,
                int lineno, const char *fmt, va_list va) VPRINTF(5);

noreturn void log_fatal_va(const char *module, const char *file, int lineno,
                           const char *fmt, va_list va) VPRINTF(4);

#define BUG(...) bug(LOG_MODULE, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define xassert(x)                                                             \
  do {                                                                         \
    IGNORE_WARNING("-Wtautological-compare")                                   \
    if (unlikely(!(x))) {                                                      \
      BUG("assertion failed: '%s'", #x);                                       \
    }                                                                          \
    UNIGNORE_WARNINGS                                                          \
  } while (0)

#ifdef LOG_IMPLEMENTATION
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

static bool colorize = false;
static bool do_syslog = false;
static enum log_class log_level = LOG_CLASS_NONE;

static const struct {
  const char name[8];
  const char log_prefix[7];
  uint8_t color;
  int syslog_equivalent;
} log_level_map[] = {
    [LOG_CLASS_NONE] = {"none", "none", 5, -1},
    [LOG_CLASS_ERROR] = {"error", " err", 31, LOG_ERR},
    [LOG_CLASS_WARNING] = {"warning", "warn", 33, LOG_WARNING},
    [LOG_CLASS_INFO] = {"info", "info", 97, LOG_INFO},
    [LOG_CLASS_DEBUG] = {"debug", " dbg", 36, LOG_DEBUG},
};

#if defined(__SANITIZE_ADDRESS__) || HAS_FEATURE(address_sanitizer)
#include <sanitizer/asan_interface.h>
#define ASAN_ENABLED 1
#endif

static void print_stack_trace(void) {
#ifdef ASAN_ENABLED
  fputs("\nStack trace:\n", stderr);
  __sanitizer_print_stack_trace();
#endif
}

void log_init(enum log_colorize _colorize, bool _do_syslog,
              enum log_facility syslog_facility, enum log_class _log_level) {
  static const int facility_map[] = {
      [LOG_FACILITY_USER] = LOG_USER,
      [LOG_FACILITY_DAEMON] = LOG_DAEMON,
  };

  /* Don't use colors if NO_COLOR is defined and not empty */
  const char *no_color_str = getenv("NO_COLOR");
  const bool no_color = no_color_str != NULL && no_color_str[0] != '\0';

  colorize =
      _colorize == LOG_COLORIZE_ALWAYS ||
      (_colorize == LOG_COLORIZE_AUTO && !no_color && isatty(STDERR_FILENO));
  do_syslog = _do_syslog;
  log_level = _log_level;

  int slvl = log_level_map[_log_level].syslog_equivalent;
  if (slvl < 0)
    do_syslog = false;

  if (do_syslog) {
    openlog(NULL, /*LOG_PID */ 0, facility_map[syslog_facility]);

    xassert(slvl >= 0);
    setlogmask(LOG_UPTO(slvl));
  }
}

void log_deinit(void) {
  if (do_syslog)
    closelog();
}

static void _log(enum log_class log_class, const char *module, const char *file,
                 int lineno, const char *fmt, va_list va) {

  xassert(log_class > LOG_CLASS_NONE);
  xassert(log_class < ALEN(log_level_map));

  if (log_class > log_level)
    return;

  const char *prefix = log_level_map[log_class].log_prefix;
  unsigned int class_clr = log_level_map[log_class].color;

  char clr[16];
  snprintf(clr, sizeof(clr), "\033[%um", class_clr);
  fprintf(stderr, "%s%s%s: ", colorize ? clr : "", prefix,
          colorize ? "\033[0m" : "");

  if (colorize)
    fputs("\033[2m", stderr);
  fprintf(stderr, "%s:%d: [%s] ", file, lineno, module);
  if (colorize)
    fputs("\033[0m", stderr);

  vfprintf(stderr, fmt, va);

  fputc('\n', stderr);
}

static void _sys_log(enum log_class log_class, const char *module,
                     const char UNUSED *file, int UNUSED lineno,
                     const char *fmt, va_list va) {
  xassert(log_class > LOG_CLASS_NONE);
  xassert(log_class < ALEN(log_level_map));

  if (!do_syslog)
    return;

  if (log_class > log_level)
    return;

  /* Map our log level to syslog's level */
  int level = log_level_map[log_class].syslog_equivalent;

  char msg[4096];
  int n = vsnprintf(msg, sizeof(msg), fmt, va);
  xassert(n >= 0);

  syslog(level, "%s: %s", module, msg);
}

void log_msg_va(enum log_class log_class, const char *module, const char *file,
                int lineno, const char *fmt, va_list va) {
  va_list va2;
  va_copy(va2, va);
  _log(log_class, module, file, lineno, fmt, va);
  _sys_log(log_class, module, file, lineno, fmt, va2);
  va_end(va2);
}

void log_msg(enum log_class log_class, const char *module, const char *file,
             int lineno, const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  log_msg_va(log_class, module, file, lineno, fmt, va);
  va_end(va);
}

noreturn void log_fatal(const char *module, const char *file, int lineno,
                        const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  log_fatal_va(module, file, lineno, fmt, va);
  va_end(va);
}

noreturn void log_fatal_va(const char *module, const char *file, int lineno,
                           const char *fmt, va_list va) {
  log_msg_va(LOG_CLASS_ERROR, module, file, lineno, fmt, va);
  print_stack_trace();
  fflush(stderr);
  abort();
}

noreturn void bug(const char *module, const char *file, int line,
                  const char *func, const char *fmt, ...) {
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  const char *msg = likely(n >= 0) ? buf : "??";
  log_msg(LOG_CLASS_ERROR, module, file, line, "BUG in %s(): %s", func, msg);
  print_stack_trace();
  fflush(stderr);
  abort();
}

static size_t map_len(void) { return ALEN(log_level_map); }

int log_level_from_string(const char *str) {
  if (unlikely(str[0] == '\0'))
    return -1;

  for (int i = 0, n = map_len(); i < n; i++)
    if (strcmp(str, log_level_map[i].name) == 0)
      return i;

  return -1;
}

#undef LOG_ERR
#undef LOG_INFO
#endif

/**
 * Log macros are define here as syslog.h also define LOG_ERR, LOG_INFO.
 */

#define LOG_FATAL(...) log_fatal(LOG_MODULE, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERR(...)                                                           \
  log_msg(LOG_CLASS_ERROR, LOG_MODULE, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)                                                          \
  log_msg(LOG_CLASS_WARNING, LOG_MODULE, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)                                                          \
  log_msg(LOG_CLASS_INFO, LOG_MODULE, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DBG(...)                                                           \
  log_msg(LOG_CLASS_DEBUG, LOG_MODULE, __FILE__, __LINE__, __VA_ARGS__)

#endif
