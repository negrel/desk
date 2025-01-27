// error.h provides 3 error handling facilities reused across entire code base:
// * TRY: returns received error code in case of failure.
// * CATCH: handle error (by jumping to label) if any.
// * PANIC: logs the error, prints stack trace and abort.

#ifndef ERROR_H_INCLUDE
#define ERROR_H_INCLUDE

#define ERRNO_TRY(err)                                                         \
  do {                                                                         \
    int has_err = err < 0;                                                     \
    if (has_err)                                                               \
      return errno;                                                            \
  } while (0)

#define ERRNO_CATCH(err, label)                                                \
  do {                                                                         \
    int has_err = err < 0;                                                     \
    if (has_err)                                                               \
      goto label;                                                              \
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

#define SDBUS_CATCH(err, label)                                                \
  do {                                                                         \
    int errno_ = err;                                                          \
    if (errno_ < 0)                                                            \
      goto label;                                                              \
  } while (0)

#define SDBUS_PANIC(err, fmt, ...)                                             \
  do {                                                                         \
    int errno_ = err;                                                          \
    if (errno_ < 0)                                                            \
      LOG_FATAL(fmt ": %s %s", #err, strerror(-errno_));                       \
  } while (0)

// sd-event returns a negative errno-style code on error.

#define SDEV_TRY(err) SDBUS_TRY(err)
#define SDEV_CATCH(err, label) SDBUS_CATCH(err, label)
#define SDEV_PANIC(err, fmt, ...) SDBUS_PANIC(err, fmt, __VA_ARGS__)

#endif
