#ifndef DHUB_XSNPRINTF_H_INCLUDE
#define DHUB_XSNPRINTF_H_INCLUDE

#include "macros.h"
#include <stdarg.h>
#include <stddef.h>

size_t xsnprintf(char *restrict buf, size_t n, const char *restrict format, ...)
    PRINTF(3) NONNULL_ARGS;

#endif
