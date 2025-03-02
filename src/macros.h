#ifndef HUB_MACROS_H_INCLUDE
#define HUB_MACROS_H_INCLUDE

#define PASTE(a, b) a##b
#define XPASTE(a, b) PASTE(a, b)
#define STRLEN(str) (sizeof("" str "") - 1)
#define DO_PRAGMA(x) _Pragma(#x)
#define VERCMP(x, y, cx, cy) ((cx > x) || ((cx == x) && (cy >= y)))

#if defined(__GNUC__) && defined(__GNUC_MINOR__)
#define GNUC_AT_LEAST(x, y) VERCMP(x, y, __GNUC__, __GNUC_MINOR__)
#else
#define GNUC_AT_LEAST(x, y) 0
#endif

#if defined(__clang_major__) && defined(__clang_minor__)
#define CLANG_AT_LEAST(x, y) VERCMP(x, y, __clang_major__, __clang_minor__)
#else
#define CLANG_AT_LEAST(x, y) 0
#endif

#ifdef __has_attribute
#define HAS_ATTRIBUTE(x) __has_attribute(x)
#else
#define HAS_ATTRIBUTE(x) 0
#endif

#ifdef __has_builtin
#define HAS_BUILTIN(x) __has_builtin(x)
#else
#define HAS_BUILTIN(x) 0
#endif

#ifdef __has_include
#define HAS_INCLUDE(x) __has_include(x)
#else
#define HAS_INCLUDE(x) 0
#endif

#ifdef __has_feature
#define HAS_FEATURE(x) __has_feature(x)
#else
#define HAS_FEATURE(x) 0
#endif

// __has_extension() is a Clang macro used to determine if a feature is
// available even if not standardized in the current "-std" mode.
#ifdef __has_extension
#define HAS_EXTENSION(x) __has_extension(x)
#else
// Clang versions prior to 3.0 only supported __has_feature()
#define HAS_EXTENSION(x) HAS_FEATURE(x)
#endif

#if GNUC_AT_LEAST(3, 0) || HAS_ATTRIBUTE(unused) || defined(__TINYC__)
#define UNUSED __attribute__((__unused__))
#else
#define UNUSED
#endif

#if GNUC_AT_LEAST(3, 0) || HAS_ATTRIBUTE(const)
#define CONST __attribute__((__const__))
#else
#define CONST
#endif

#if GNUC_AT_LEAST(3, 0) || HAS_ATTRIBUTE(malloc)
#define MALLOC __attribute__((__malloc__))
#else
#define MALLOC
#endif

#if GNUC_AT_LEAST(3, 0) || HAS_ATTRIBUTE(constructor)
#define CONSTRUCTOR __attribute__((__constructor__))
#define HAVE_ATTR_CONSTRUCTOR 1
#else
#define CONSTRUCTOR
#endif

#if GNUC_AT_LEAST(3, 0) || HAS_ATTRIBUTE(destructor)
#define DESTRUCTOR __attribute__((__destructor__))
#else
#define DESTRUCTOR
#endif

#if GNUC_AT_LEAST(3, 0) || HAS_ATTRIBUTE(format)
#define PRINTF(x) __attribute__((__format__(__printf__, (x), (x + 1))))
#define VPRINTF(x) __attribute__((__format__(__printf__, (x), 0)))
#else
#define PRINTF(x)
#define VPRINTF(x)
#endif

#if (GNUC_AT_LEAST(3, 0) || HAS_BUILTIN(__builtin_expect)) &&                  \
    defined(__OPTIMIZE__)
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x) (x)
#define unlikely(x) (x)
#endif

#if GNUC_AT_LEAST(3, 1) || HAS_ATTRIBUTE(noinline)
#define NOINLINE __attribute__((__noinline__))
#else
#define NOINLINE
#endif

#if GNUC_AT_LEAST(3, 1) || HAS_ATTRIBUTE(always_inline)
#define ALWAYS_INLINE __attribute__((__always_inline__))
#else
#define ALWAYS_INLINE
#endif

#if GNUC_AT_LEAST(3, 3) || HAS_ATTRIBUTE(nonnull)
#define NONNULL_ARGS __attribute__((__nonnull__))
#define NONNULL_ARG(...) __attribute__((__nonnull__(__VA_ARGS__)))
#else
#define NONNULL_ARGS
#define NONNULL_ARG(...)
#endif

#if GNUC_AT_LEAST(3, 4) || HAS_ATTRIBUTE(warn_unused_result)
#define WARN_UNUSED_RESULT __attribute__((__warn_unused_result__))
#else
#define WARN_UNUSED_RESULT
#endif

#if GNUC_AT_LEAST(4, 1) || HAS_ATTRIBUTE(flatten)
#define FLATTEN __attribute__((__flatten__))
#else
#define FLATTEN
#endif

#if GNUC_AT_LEAST(4, 3) || HAS_ATTRIBUTE(hot)
#define HOT __attribute__((__hot__))
#else
#define HOT
#endif

#if GNUC_AT_LEAST(4, 3) || HAS_ATTRIBUTE(cold)
#define COLD __attribute__((__cold__))
#else
#define COLD
#endif

#if GNUC_AT_LEAST(4, 5) || HAS_BUILTIN(__builtin_unreachable)
#define UNREACHABLE() __builtin_unreachable()
#else
#define UNREACHABLE()
#endif

#if GNUC_AT_LEAST(5, 0) || HAS_ATTRIBUTE(returns_nonnull)
#define RETURNS_NONNULL __attribute__((__returns_nonnull__))
#else
#define RETURNS_NONNULL
#endif

#if HAS_ATTRIBUTE(diagnose_if)
#define DIAGNOSE_IF(x) __attribute__((diagnose_if((x), (#x), "error")))
#else
#define DIAGNOSE_IF(x)
#endif

#define XMALLOC MALLOC RETURNS_NONNULL WARN_UNUSED_RESULT
#define XSTRDUP XMALLOC NONNULL_ARGS

#if __STDC_VERSION__ >= 201112L
#define noreturn _Noreturn
#elif GNUC_AT_LEAST(3, 0)
#define noreturn __attribute__((__noreturn__))
#else
#define noreturn
#endif

#if CLANG_AT_LEAST(3, 6)
#define UNROLL_LOOP(n) DO_PRAGMA(clang loop unroll_count(n))
#elif GNUC_AT_LEAST(8, 0)
#define UNROLL_LOOP(n) DO_PRAGMA(GCC unroll(n))
#else
#define UNROLL_LOOP(n)
#endif

#ifdef __COUNTER__
// Supported by GCC 4.3+ and Clang
#define COUNTER_ __COUNTER__
#else
#define COUNTER_ __LINE__
#endif

#if defined(_DEBUG) && defined(HAVE_ATTR_CONSTRUCTOR)
#define UNITTEST static void CONSTRUCTOR XPASTE(unittest_, COUNTER_)(void)
#else
#define UNITTEST static void UNUSED XPASTE(unittest_, COUNTER_)(void)
#endif

#ifdef __clang__
#define IGNORE_WARNING(wflag)                                                  \
  DO_PRAGMA(clang diagnostic push)                                             \
  DO_PRAGMA(clang diagnostic ignored "-Wunknown-pragmas")                      \
  DO_PRAGMA(clang diagnostic ignored "-Wunknown-warning-option")               \
  DO_PRAGMA(clang diagnostic ignored wflag)
#define UNIGNORE_WARNINGS DO_PRAGMA(clang diagnostic pop)
#elif GNUC_AT_LEAST(4, 6)
#define IGNORE_WARNING(wflag)                                                  \
  DO_PRAGMA(GCC diagnostic push)                                               \
  DO_PRAGMA(GCC diagnostic ignored "-Wpragmas")                                \
  DO_PRAGMA(GCC diagnostic ignored wflag)
#define UNIGNORE_WARNINGS DO_PRAGMA(GCC diagnostic pop)
#else
#define IGNORE_WARNING(wflag)
#define UNIGNORE_WARNINGS
#endif

#define ALEN(v) (sizeof(v) / sizeof((v)[0]))

#endif
