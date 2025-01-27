// error.h provides 3 error handling facilities reused across entire code base:
// * TRY: returns received error code in case of failure.
// * CATCH: execute given block of code on error.
// * PANIC: logs the error, prints stack trace and abort.

#ifndef ERROR_H_INCLUDE
#define ERROR_H_INCLUDE

#include <errno.h>

// errno macros expects a negative integer when errno is set.

#define ERRNO_TRY(err)                                                         \
  do {                                                                         \
    int has_err = err < 0;                                                     \
    if (has_err)                                                               \
      return errno;                                                            \
  } while (0)

#define ERRNO_CATCH(err, block)                                                \
  do {                                                                         \
    int has_err = err < 0;                                                     \
    if (has_err)                                                               \
      block                                                                    \
  } while (0)

#define ERRNO_PANIC(err, fmt, ...)                                             \
  do {                                                                         \
    int has_err = err < 0;                                                     \
    if (has_err)                                                               \
      LOG_FATAL(fmt ": %s %s", #err, strerror(errno));                         \
  } while (0)

// sd-bus returns a negative errno-style code on error.

#define SDBUS_TRY(err)                                                         \
  do {                                                                         \
    int errno_ = err;                                                          \
    if (errno_ < 0)                                                            \
      return errno_;                                                           \
  } while (0)

#define SDBUS_CATCH(err, block)                                                \
  do {                                                                         \
    int errno_ = err;                                                          \
    if (errno_ < 0)                                                            \
      block                                                                    \
  } while (0)

#define SDBUS_PANIC(err, fmt, ...)                                             \
  do {                                                                         \
    int errno_ = err;                                                          \
    if (errno_ < 0)                                                            \
      LOG_FATAL(fmt ": %s %s", #err, strerror(-errno_));                       \
  } while (0)

// sd-event returns a negative errno-style code on error.

#define SDEV_TRY(err) SDBUS_TRY(err)
#define SDEV_CATCH(err, block) SDBUS_CATCH(err, block)
#define SDEV_PANIC(err, fmt, ...) SDBUS_PANIC(err, fmt, __VA_ARGS__)

#endif
