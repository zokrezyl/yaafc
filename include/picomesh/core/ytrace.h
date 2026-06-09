#ifndef PICOMESH_CORE_YTRACE_H
#define PICOMESH_CORE_YTRACE_H

/*
 * Switchable trace points. Each trace site has a static bool toggleable at
 * runtime; when off, only an if-check fires.
 *
 *   ytrace("processing item %d", id);
 *   ydebug("buffer size: %zu", size);
 *   yinfo("connection established");
 *   ywarn("timeout exceeded: %dms", ms);
 *   yerror("failed to open: %s", path);
 *
 * Env:
 *   YTRACE_DEFAULT_ON=yes      enable trace points at startup (default: off).
 *   YTRACE_LOG_LEVEL=error     when DEFAULT_ON, only activate points at this
 *                              level or higher (trace<debug<info<warn<error).
 *                              Unset = all levels. e.g. =error → only yerror().
 *   YTRACE_FILE=/path          write the merged log here (default: stderr).
 *   YTRACE_FLUSH_MS=250        collector merge/flush interval (default 250 ms).
 *   YTRACE_BUF_KB=128          per-thread, per-buffer size (two per thread).
 *
 * Output is NON-BLOCKING and lock-free across threads on the hot path: each
 * thread appends formatted lines into its OWN double buffer (guarded only by a
 * per-thread lock, never the shared sink), and a background collector
 * periodically swaps each thread's buffer, k-way merges all lines by timestamp,
 * and writes the batch in one go. Logging never serializes worker threads on a
 * global stderr lock, and never back-pressures a worker (overflow drops + counts).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Re-exported from platform so legacy ytrace_*() macros don't need
 * to learn about platform headers. New code should call
 * picomesh_platform_time_monotonic_sec directly. */
double picomesh_ytime_monotonic_sec(void);

#ifndef YTRACE_C_ENABLED
#define YTRACE_C_ENABLED 1
#endif
#ifndef YTRACE_C_ENABLE_TRACE
#define YTRACE_C_ENABLE_TRACE 1
#endif
#ifndef YTRACE_C_ENABLE_DEBUG
#define YTRACE_C_ENABLE_DEBUG 1
#endif
#ifndef YTRACE_C_ENABLE_INFO
#define YTRACE_C_ENABLE_INFO 1
#endif
#ifndef YTRACE_C_ENABLE_WARN
#define YTRACE_C_ENABLE_WARN 1
#endif
#ifndef YTRACE_C_ENABLE_ERROR
#define YTRACE_C_ENABLE_ERROR 1
#endif

#if YTRACE_C_ENABLED

#ifndef YTRACE_C_MAX_POINTS
#define YTRACE_C_MAX_POINTS 4096
#endif

typedef struct {
    bool *enabled;
    const char *file;
    int line;
    const char *function;
    const char *level;
    const char *message;
} ytrace_point_t;

void ytrace_init(void);
void ytrace_shutdown(void);

bool ytrace_register(bool *enabled, const char *file, int line, const char *func,
                     const char *level, const char *message);

void ytrace_output(const char *level, const char *file, int line, const char *func,
                   const char *fmt, ...)
#ifndef _MSC_VER
    __attribute__((format(printf, 5, 6)))
#endif
    ;

void ytrace_set_all_enabled(bool enabled);
void ytrace_set_level_enabled(const char *level, bool enabled);
void ytrace_set_file_enabled(const char *file, bool enabled);
void ytrace_set_function_enabled(const char *function, bool enabled);

size_t ytrace_get_point_count(void);
const ytrace_point_t *ytrace_get_points(void);
void ytrace_list(void);

#ifndef YTIME_MAX_TIMERS
#define YTIME_MAX_TIMERS 256
#endif

struct ytime_timer {
    const char *name;
    const char *file;
    int line;
    const char *function;
    bool registered;
    uint64_t count;
    double sum_ms;
    double last_ms;
    double min_ms;
    double max_ms;
    double avg_ms;
};

void ytime_timer_observe(struct ytime_timer *t, const char *name, const char *file, int line,
                         const char *function, double elapsed_ms);
size_t ytime_timer_get_count(void);
const struct ytime_timer *const *ytime_timer_get_all(void);
void ytime_timer_list(void);
void ytime_timer_reset_all(void);

#define PICOMESH_TRACE_EMIT(level_str, fmt, ...)                                                      \
    do {                                                                                           \
        static bool _ytrace_enabled_ = false;                                                      \
        static bool _ytrace_registered_ = false;                                                   \
        if (!_ytrace_registered_) {                                                                \
            _ytrace_enabled_ = ytrace_register(&_ytrace_enabled_, __FILE__, __LINE__, __func__,    \
                                               level_str, fmt);                                    \
            _ytrace_registered_ = true;                                                            \
        }                                                                                          \
        if (_ytrace_enabled_) {                                                                    \
            ytrace_output(level_str, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__);            \
        }                                                                                          \
    } while (0)

#if YTRACE_C_ENABLE_TRACE
#define ytrace(fmt, ...) PICOMESH_TRACE_EMIT("trace", fmt, ##__VA_ARGS__)
#else
#define ytrace(fmt, ...) ((void)0)
#endif

#if YTRACE_C_ENABLE_DEBUG
#define ydebug(fmt, ...) PICOMESH_TRACE_EMIT("debug", fmt, ##__VA_ARGS__)
#else
#define ydebug(fmt, ...) ((void)0)
#endif

#if YTRACE_C_ENABLE_INFO
#define yinfo(fmt, ...) PICOMESH_TRACE_EMIT("info", fmt, ##__VA_ARGS__)
#else
#define yinfo(fmt, ...) ((void)0)
#endif

#if YTRACE_C_ENABLE_WARN
#define ywarn(fmt, ...) PICOMESH_TRACE_EMIT("warn", fmt, ##__VA_ARGS__)
#else
#define ywarn(fmt, ...) ((void)0)
#endif

#if YTRACE_C_ENABLE_ERROR
#define yerror(fmt, ...) PICOMESH_TRACE_EMIT("error", fmt, ##__VA_ARGS__)
#else
#define yerror(fmt, ...) ((void)0)
#endif

#define ylog(lvl, fmt, ...) PICOMESH_TRACE_EMIT(lvl, fmt, ##__VA_ARGS__)

#define ytime_start(name) double ytime_##name = picomesh_ytime_monotonic_sec()

#define ytime_report(name)                                                                         \
    do {                                                                                           \
        static struct ytime_timer _ytime_timer_##name;                                             \
        double _ytime_elapsed_ms_ =                                                                \
            (picomesh_ytime_monotonic_sec() - ytime_##name) * 1000.0;                                 \
        ytime_timer_observe(&_ytime_timer_##name, #name, __FILE__, __LINE__, __func__,             \
                            _ytime_elapsed_ms_);                                                   \
        yinfo(#name ": %.3f ms  (avg %.3f ms, min %.3f, max %.3f, n=%llu)", _ytime_elapsed_ms_,    \
              _ytime_timer_##name.avg_ms, _ytime_timer_##name.min_ms, _ytime_timer_##name.max_ms,  \
              (unsigned long long)_ytime_timer_##name.count);                                      \
    } while (0)

#else /* !YTRACE_C_ENABLED */

#define ytrace(fmt, ...) ((void)0)
#define ydebug(fmt, ...) ((void)0)
#define yinfo(fmt, ...) ((void)0)
#define ywarn(fmt, ...) ((void)0)
#define yerror(fmt, ...) ((void)0)
#define ylog(lvl, fmt, ...) ((void)0)
#define ytime_start(name) ((void)0)
#define ytime_report(name) ((void)0)

static inline void ytrace_init(void) {}
static inline void ytrace_shutdown(void) {}
static inline void ytrace_set_all_enabled(bool e) { (void)e; }
static inline void ytrace_set_level_enabled(const char *l, bool e) { (void)l; (void)e; }
static inline void ytrace_set_file_enabled(const char *f, bool e) { (void)f; (void)e; }
static inline void ytrace_set_function_enabled(const char *f, bool e) { (void)f; (void)e; }
static inline size_t ytrace_get_point_count(void) { return 0; }
static inline const ytrace_point_t *ytrace_get_points(void) { return NULL; }
static inline void ytrace_list(void) {}
struct ytime_timer;
static inline size_t ytime_timer_get_count(void) { return 0; }
static inline const struct ytime_timer *const *ytime_timer_get_all(void) { return NULL; }
static inline void ytime_timer_list(void) {}
static inline void ytime_timer_reset_all(void) {}

#endif /* YTRACE_C_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_CORE_YTRACE_H */
