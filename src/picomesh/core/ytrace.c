/* ytrace.c — switchable trace points with non-blocking, per-thread buffered
 * output. POSIX-only (pthread).
 *
 * Two cooperating mechanisms:
 *
 *   1. Trace-point registry (g_points, g_mutex): one static bool per call site,
 *      seeded at first hit from YTRACE_DEFAULT_ON filtered by YTRACE_LOG_LEVEL.
 *      The registry mutex is touched only on first-hit registration and on the
 *      rare control ops — never on the per-line emit path.
 *
 *   2. Output path (the multi-thread part): an enabled emit formats the line on
 *      the CALLING thread and appends it — with a numeric timestamp — into that
 *      thread's OWN double buffer, under a PER-THREAD lock. A single background
 *      collector wakes every YTRACE_FLUSH_MS, swaps each thread's active buffer
 *      for its spare (O(1) under the per-thread lock), k-way merges every drained
 *      line by timestamp, and writes the whole batch to the sink in one call. So
 *      the shared sink lock is paid once per flush, not once per line, and worker
 *      threads never serialize on it. Overflow (collector behind) drops + counts;
 *      it never blocks a worker.
 */

#define _POSIX_C_SOURCE 200809L

#include <picomesh/core/ytrace.h>

#if YTRACE_C_ENABLED

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---- trace-point registry ------------------------------------------------ */

static ytrace_point_t g_points[YTRACE_C_MAX_POINTS];
static size_t g_point_count = 0;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_initialized = false;
static bool g_default_enabled = false;
static int g_min_level = 0;        /* rank threshold from YTRACE_LOG_LEVEL (0 = all) */

static struct ytime_timer *g_timers[YTIME_MAX_TIMERS];
static size_t g_timer_count = 0;

#define YTRACE_LOCK()   pthread_mutex_lock(&g_mutex)
#define YTRACE_UNLOCK() pthread_mutex_unlock(&g_mutex)

/* trace<debug<info<warn<error. Used both for YTRACE_LOG_LEVEL parsing and for
 * the per-point filter at registration time. */
static int level_rank(const char *level)
{
    if (!level) return 2;
    switch (level[0]) {
    case 't': return 0; /* trace */
    case 'd': return 1; /* debug */
    case 'i': return 2; /* info  */
    case 'w': return 3; /* warn  */
    case 'e': return 4; /* error */
    default:  return 2;
    }
}

/* ---- output sink + colours ---------------------------------------------- */

static int  g_sink_fd = 2;         /* stderr by default; YTRACE_FILE overrides */
static bool g_use_colors = false;
static long g_flush_ms = 250;
static size_t g_buf_cap = 128 * 1024;

#define ANSI_RESET  "\033[0m"
#define ANSI_GRAY   "\033[90m"
#define ANSI_CYAN   "\033[36m"
#define ANSI_GREEN  "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_RED    "\033[31m"
#define ANSI_BOLD   "\033[1m"

static bool sink_supports_color(int fd)
{
    if (getenv("NO_COLOR")) return false;
    if (!isatty(fd)) return false;
    const char *term = getenv("TERM");
    if (!term || strcmp(term, "dumb") == 0) return false;
    return true;
}

static const char *level_color(const char *level)
{
    if (!g_use_colors) return "";
    if (strcmp(level, "trace") == 0) return ANSI_GRAY;
    if (strcmp(level, "debug") == 0) return ANSI_CYAN;
    if (strcmp(level, "info")  == 0) return ANSI_GREEN;
    if (strcmp(level, "warn")  == 0) return ANSI_YELLOW;
    if (strcmp(level, "error") == 0) return ANSI_RED ANSI_BOLD;
    return "";
}

static const char *color_reset(void)
{
    return g_use_colors ? ANSI_RESET : "";
}

static void format_timestamp(char *buf, size_t bufsize, const struct timespec *ts)
{
    struct tm tmv;
    localtime_r(&ts->tv_sec, &tmv);
    int ms = (int)(ts->tv_nsec / 1000000);
    snprintf(buf, bufsize, "%02d:%02d:%02d.%03d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
}

#include <picomesh/platform/time.h>

double picomesh_ytime_monotonic_sec(void)
{
    return picomesh_platform_time_monotonic_sec();
}

static void ytrace_write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t written = write(fd, buf + off, len - off);
        if (written < 0) { if (errno == EINTR) continue; break; }
        if (written == 0) break;
        off += (size_t)written;
    }
}

/* ---- per-thread output buffers ------------------------------------------ */

/* A formatted line is stored as [uint64 ts_ns][uint16 len][len text bytes]. */
#define YTRACE_REC_HDR (sizeof(uint64_t) + sizeof(uint16_t))
#define YTRACE_REC_MAX 1024

struct ytrace_logbuf {
    char    *data;
    size_t   len;
    size_t   cap;
    uint64_t drop_count;      /* lines dropped while THIS buffer was active+full */
    uint64_t drop_first_ts;   /* timestamp of the first such drop (marker position) */
};

struct ytrace_thread {
    struct ytrace_logbuf  buf[2];      /* ping-pong: writer fills buf[active] */
    int                   active;
    pthread_mutex_t       lock;        /* per-thread: only this writer + collector */
    unsigned long         tid;
    int                   dead;        /* thread exited; collector reclaims */
    struct ytrace_thread *next;        /* registry list */
};

static struct ytrace_thread *g_threads = NULL;
static pthread_mutex_t g_threads_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t   g_tls_key;
static bool g_tls_key_made = false;
static __thread struct ytrace_thread *tl_self = NULL;

static pthread_t g_collector;
static bool g_collector_started = false;
static volatile int g_collector_stop = 0;

static void ytrace_flush_all(void);

static void *ytrace_collector_main(void *arg)
{
    (void)arg;
    while (!g_collector_stop) {
        struct timespec ts = {g_flush_ms / 1000, (g_flush_ms % 1000) * 1000000L};
        nanosleep(&ts, NULL);
        ytrace_flush_all();
    }
    ytrace_flush_all(); /* final drain */
    return NULL;
}

/* pthread_key destructor: the thread is exiting and will no longer log, so mark
 * its buffers for the collector to drain and reclaim. Does NOT free here — the
 * collector may be mid-drain. */
static void ytrace_tls_dtor(void *arg)
{
    struct ytrace_thread *thread = arg;
    if (thread) thread->dead = 1;
}

static struct ytrace_thread *ytrace_thread_register(void)
{
    struct ytrace_thread *thread = calloc(1, sizeof(*thread));
    if (!thread) return NULL;
    thread->buf[0].data = malloc(g_buf_cap);
    thread->buf[1].data = malloc(g_buf_cap);
    if (!thread->buf[0].data || !thread->buf[1].data) {
        free(thread->buf[0].data); free(thread->buf[1].data); free(thread);
        return NULL;
    }
    thread->buf[0].cap = thread->buf[1].cap = g_buf_cap;
    pthread_mutex_init(&thread->lock, NULL);
    thread->tid = (unsigned long)pthread_self();

    pthread_mutex_lock(&g_threads_lock);
    thread->next = g_threads;
    g_threads = thread;
    /* Start the collector on the first thread that ever emits — so a process
     * that never logs (tracing off) never spawns the background thread. */
    if (!g_collector_started) {
        if (pthread_create(&g_collector, NULL, ytrace_collector_main, NULL) == 0)
            g_collector_started = true;
    }
    pthread_mutex_unlock(&g_threads_lock);

    tl_self = thread;
    if (g_tls_key_made) pthread_setspecific(g_tls_key, thread);
    return thread;
}

/* Hot path: append one formatted line to this thread's active buffer. */
static void ytrace_emit(uint64_t ts_ns, const char *text, size_t len)
{
    struct ytrace_thread *thread = tl_self;
    if (!thread) {
        thread = ytrace_thread_register();
        if (!thread) { ytrace_write_all(g_sink_fd, text, len); return; }
    }
    size_t need = YTRACE_REC_HDR + len;
    if (need > thread->buf[0].cap) { ytrace_write_all(g_sink_fd, text, len); return; }

    pthread_mutex_lock(&thread->lock);
    struct ytrace_logbuf *buf = &thread->buf[thread->active];
    if (buf->len + need > buf->cap) {
        int other = thread->active ^ 1;
        if (thread->buf[other].len == 0) {  /* spare drained by collector → swap */
            thread->active = other;
            buf = &thread->buf[other];
        } else {                            /* collector behind → drop, don't block.
                                             * Record the gap on the buffer (O(1)):
                                             * the collector turns it into a marker
                                             * positioned at the first drop's time. */
            if (buf->drop_count == 0) buf->drop_first_ts = ts_ns;
            buf->drop_count++;
            pthread_mutex_unlock(&thread->lock);
            return;
        }
    }
    char *dest = buf->data + buf->len;
    uint16_t len16 = (uint16_t)len;
    memcpy(dest, &ts_ns, sizeof(ts_ns));
    memcpy(dest + sizeof(ts_ns), &len16, sizeof(len16));
    memcpy(dest + YTRACE_REC_HDR, text, len);
    buf->len += need;
    pthread_mutex_unlock(&thread->lock);
}

/* ---- collector scratch (only the collector thread touches these) -------- */

/* A line to write, in the merged batch. text!=NULL → a real log line (points into
 * a thread buffer); text==NULL → a synthesized "lines dropped here" marker that
 * the collector formats at write time, positioned at ts_ns (the first drop). */
struct ytrace_desc {
    uint64_t      ts_ns;
    const char   *text;
    uint16_t      len;
    unsigned long tid;
    uint64_t      drop;
};
struct ytrace_snap { struct ytrace_thread *t; int idx; };

static struct ytrace_desc *g_desc = NULL; static size_t g_desc_cap = 0;
static struct ytrace_snap *g_snap = NULL; static size_t g_snap_cap = 0;
static char  *g_outbuf = NULL;            static size_t g_outbuf_cap = 0;

static int grow(void **buf, size_t *cap, size_t need, size_t elem)
{
    if (need <= *cap) return 1;
    size_t new_cap = *cap ? *cap : 256;
    while (new_cap < need) new_cap *= 2;
    void *new_buf = realloc(*buf, new_cap * elem);
    if (!new_buf) return 0;
    *buf = new_buf; *cap = new_cap;
    return 1;
}

static int cmp_desc_ts(const void *a, const void *b)
{
    uint64_t left_ts = ((const struct ytrace_desc *)a)->ts_ns;
    uint64_t right_ts = ((const struct ytrace_desc *)b)->ts_ns;
    return (left_ts > right_ts) - (left_ts < right_ts);
}

/* Format one synthesized drop marker into `out`, positioned at `ts_ns`. */
static int format_drop_marker(char *out, size_t cap, uint64_t ts_ns,
                              unsigned long tid, uint64_t drop)
{
    struct timespec ts = {(time_t)(ts_ns / 1000000000ull),
                          (long)(ts_ns % 1000000000ull)};
    char time_buf[32];
    format_timestamp(time_buf, sizeof(time_buf), &ts);
    const char *color = g_use_colors ? ANSI_YELLOW : "";
    const char *reset = g_use_colors ? ANSI_RESET : "";
    return snprintf(out, cap,
                    "[%s] %s[drop ]%s ytrace: %llu line(s) dropped here "
                    "(thread %lu, buffer full)\n",
                    time_buf, color, reset, (unsigned long long)drop, tid);
}

static void ytrace_flush_all(void)
{
    size_t ndesc = 0, nsnap = 0;

    /* Phase 1 — under each thread's lock: pick a drainable buffer (a full one the
     * writer swapped away, else force-swap the partial active buffer) and read its
     * drop counter. The text bytes stay valid: a non-active buffer with len>0 is
     * owned by the collector and the writer won't touch it until we reset its len.
     * So we parse it lock-free below. */
    pthread_mutex_lock(&g_threads_lock);
    for (struct ytrace_thread *thread = g_threads; thread; thread = thread->next) {
        pthread_mutex_lock(&thread->lock);
        int active = thread->active, inactive = active ^ 1, take = -1;
        if (thread->buf[inactive].len > 0) {
            take = inactive;
        } else if (thread->buf[active].len > 0) {
            thread->active = inactive;  /* O(1) swap: writer moves to the free buffer */
            take = active;
        }
        unsigned long tid = thread->tid;
        char    *data        = take >= 0 ? thread->buf[take].data          : NULL;
        size_t   data_len    = take >= 0 ? thread->buf[take].len           : 0;
        uint64_t drop_count  = take >= 0 ? thread->buf[take].drop_count    : 0;
        uint64_t drop_ts     = take >= 0 ? thread->buf[take].drop_first_ts : 0;
        pthread_mutex_unlock(&thread->lock);

        if (take < 0) continue;

        if (grow((void **)&g_snap, &g_snap_cap, nsnap + 1, sizeof(*g_snap))) {
            g_snap[nsnap].t = thread; g_snap[nsnap].idx = take; nsnap++;
        }
        const char *cursor = data, *end = data + data_len;
        while (cursor + YTRACE_REC_HDR <= end) {
            uint64_t ts; uint16_t rec_len;
            memcpy(&ts, cursor, sizeof(ts));
            memcpy(&rec_len, cursor + sizeof(ts), sizeof(rec_len));
            const char *txt = cursor + YTRACE_REC_HDR;
            if (txt + rec_len > end) break;
            if (grow((void **)&g_desc, &g_desc_cap, ndesc + 1, sizeof(*g_desc))) {
                g_desc[ndesc] = (struct ytrace_desc){.ts_ns = ts, .text = txt, .len = rec_len};
                ndesc++;
            }
            cursor = txt + rec_len;
        }
        /* The marker lands at the first dropped line's timestamp — so after the
         * merge it appears in the stream exactly at the gap. */
        if (drop_count > 0 && grow((void **)&g_desc, &g_desc_cap, ndesc + 1, sizeof(*g_desc))) {
            g_desc[ndesc] = (struct ytrace_desc){.ts_ns = drop_ts, .text = NULL,
                                                 .tid = tid, .drop = drop_count};
            ndesc++;
        }
    }
    pthread_mutex_unlock(&g_threads_lock);

    /* Phase 2 — merge by timestamp + write once. */
    if (ndesc) {
        qsort(g_desc, ndesc, sizeof(*g_desc), cmp_desc_ts);
        size_t total = 0, marker_count = 0;
        for (size_t i = 0; i < ndesc; ++i) {
            if (g_desc[i].text) total += g_desc[i].len; else marker_count++;
        }
        size_t reserve = total + marker_count * 128 + 1;
        if (grow((void **)&g_outbuf, &g_outbuf_cap, reserve, 1)) {
            size_t off = 0;
            for (size_t i = 0; i < ndesc; ++i) {
                if (g_desc[i].text) {
                    memcpy(g_outbuf + off, g_desc[i].text, g_desc[i].len);
                    off += g_desc[i].len;
                } else if (off + 128 <= g_outbuf_cap) {
                    int marker_len = format_drop_marker(g_outbuf + off, g_outbuf_cap - off,
                                                        g_desc[i].ts_ns, g_desc[i].tid,
                                                        g_desc[i].drop);
                    if (marker_len > 0) off += (size_t)marker_len;
                }
            }
            ytrace_write_all(g_sink_fd, g_outbuf, off);
        }
    }

    /* Phase 3 — free the drained buffers (writer may now reuse them). */
    for (size_t i = 0; i < nsnap; ++i) {
        pthread_mutex_lock(&g_snap[i].t->lock);
        g_snap[i].t->buf[g_snap[i].idx].len = 0;
        g_snap[i].t->buf[g_snap[i].idx].drop_count = 0;
        pthread_mutex_unlock(&g_snap[i].t->lock);
    }

    /* Phase 4 — reclaim threads that exited and are fully drained. Only the
     * collector frees, so this is race-free. */
    pthread_mutex_lock(&g_threads_lock);
    struct ytrace_thread **link = &g_threads;
    while (*link) {
        struct ytrace_thread *thread = *link;
        pthread_mutex_lock(&thread->lock);
        int empty = (thread->buf[0].len == 0 && thread->buf[1].len == 0);
        pthread_mutex_unlock(&thread->lock);
        if (thread->dead && empty) {
            *link = thread->next;
            pthread_mutex_destroy(&thread->lock);
            free(thread->buf[0].data); free(thread->buf[1].data); free(thread);
        } else {
            link = &thread->next;
        }
    }
    pthread_mutex_unlock(&g_threads_lock);
}

/* ---- init / shutdown ----------------------------------------------------- */

void ytrace_init(void)
{
    YTRACE_LOCK();
    if (g_initialized) { YTRACE_UNLOCK(); return; }

    const char *def = getenv("YTRACE_DEFAULT_ON");
    if (def) {
        g_default_enabled = (strcmp(def, "yes") == 0 || strcmp(def, "1") == 0 ||
                             strcmp(def, "true") == 0);
    }
    const char *lvl = getenv("YTRACE_LOG_LEVEL");
    g_min_level = (lvl && *lvl) ? level_rank(lvl) : 0;

    const char *flush = getenv("YTRACE_FLUSH_MS");
    if (flush && *flush) { long v = atol(flush); if (v > 0) g_flush_ms = v; }

    const char *bufkb = getenv("YTRACE_BUF_KB");
    if (bufkb && *bufkb) { long v = atol(bufkb); if (v > 0) g_buf_cap = (size_t)v * 1024; }

    const char *file = getenv("YTRACE_FILE");
    if (file && *file) {
        int fd = open(file, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) g_sink_fd = fd;
    }
    g_use_colors = sink_supports_color(g_sink_fd);

    if (!g_tls_key_made && pthread_key_create(&g_tls_key, ytrace_tls_dtor) == 0)
        g_tls_key_made = true;

    g_initialized = true;
    YTRACE_UNLOCK();
}

void ytrace_shutdown(void)
{
    /* Stop the collector (it does a final flush on its way out), then clear the
     * point registry. Thread buffers are left for process exit. */
    bool started;
    YTRACE_LOCK();
    started = g_collector_started;
    YTRACE_UNLOCK();
    if (started) {
        g_collector_stop = 1;
        pthread_join(g_collector, NULL);
    }
    YTRACE_LOCK();
    g_collector_started = false;
    g_point_count = 0;
    g_initialized = false;
    YTRACE_UNLOCK();
}

bool ytrace_register(bool *enabled, const char *file, int line, const char *func,
                     const char *level, const char *message)
{
    YTRACE_LOCK();
    if (!g_initialized) {
        YTRACE_UNLOCK();
        ytrace_init();
        YTRACE_LOCK();
    }
    /* Default-on (YTRACE_DEFAULT_ON) AND this point's level passes the
     * YTRACE_LOG_LEVEL threshold. So YTRACE_LOG_LEVEL=error lights up only the
     * yerror() sites; everything else stays off. */
    *enabled = g_default_enabled && (level_rank(level) >= g_min_level);
    if (g_point_count < YTRACE_C_MAX_POINTS) {
        g_points[g_point_count] = (ytrace_point_t){.enabled = enabled,
                                                   .file = file,
                                                   .line = line,
                                                   .function = func,
                                                   .level = level,
                                                   .message = message};
        g_point_count++;
    } else {
        static bool warned = false;
        if (!warned) {
            fprintf(stderr, "[ytrace] WARNING: max trace points (%d) exceeded\n",
                    YTRACE_C_MAX_POINTS);
            warned = true;
        }
    }
    YTRACE_UNLOCK();
    return *enabled;
}

void ytrace_output(const char *level, const char *file, int line, const char *func,
                   const char *fmt, ...)
{
    char msg_buf[1024];
    char time_buf[32];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t ts_ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    format_timestamp(time_buf, sizeof(time_buf), &ts);

    const char *base_name = strrchr(file, '/');
    if (!base_name) base_name = strrchr(file, '\\');
    if (base_name) base_name++; else base_name = file;

    char rec[YTRACE_REC_MAX];
    int rec_len = snprintf(rec, sizeof(rec), "[%s] %s[%-5s]%s %s:%d (%s): %s\n", time_buf,
                           level_color(level), level, color_reset(), base_name, line, func,
                           msg_buf);
    if (rec_len <= 0) return;
    if ((size_t)rec_len >= sizeof(rec)) rec_len = (int)sizeof(rec) - 1; /* truncated line */
    ytrace_emit(ts_ns, rec, (size_t)rec_len);
}

/* ---- runtime control (operate on the point registry) -------------------- */

void ytrace_set_all_enabled(bool enabled)
{
    YTRACE_LOCK();
    for (size_t i = 0; i < g_point_count; i++) *g_points[i].enabled = enabled;
    YTRACE_UNLOCK();
}

void ytrace_set_level_enabled(const char *level, bool enabled)
{
    YTRACE_LOCK();
    for (size_t i = 0; i < g_point_count; i++) {
        if (strcmp(g_points[i].level, level) == 0) *g_points[i].enabled = enabled;
    }
    YTRACE_UNLOCK();
}

void ytrace_set_file_enabled(const char *file, bool enabled)
{
    YTRACE_LOCK();
    for (size_t i = 0; i < g_point_count; i++) {
        const char *base_name = strrchr(g_points[i].file, '/');
        if (!base_name) base_name = strrchr(g_points[i].file, '\\');
        base_name = base_name ? base_name + 1 : g_points[i].file;
        if (strcmp(g_points[i].file, file) == 0 || strcmp(base_name, file) == 0) {
            *g_points[i].enabled = enabled;
        }
    }
    YTRACE_UNLOCK();
}

void ytrace_set_function_enabled(const char *function, bool enabled)
{
    YTRACE_LOCK();
    for (size_t i = 0; i < g_point_count; i++) {
        if (strcmp(g_points[i].function, function) == 0) *g_points[i].enabled = enabled;
    }
    YTRACE_UNLOCK();
}

size_t ytrace_get_point_count(void)
{
    YTRACE_LOCK();
    size_t count = g_point_count;
    YTRACE_UNLOCK();
    return count;
}

const ytrace_point_t *ytrace_get_points(void) { return g_points; }

void ytrace_list(void)
{
    YTRACE_LOCK();
    fprintf(stderr, "\n[ytrace] Registered trace points: %zu\n", g_point_count);
    fprintf(stderr, "%-4s %-7s %-6s %-30s %-20s %s\n", "IDX", "ENABLED", "LEVEL", "FILE:LINE",
            "FUNCTION", "MESSAGE");
    for (size_t i = 0; i < g_point_count; i++) {
        const ytrace_point_t *point = &g_points[i];
        const char *base_name = strrchr(point->file, '/');
        if (!base_name) base_name = strrchr(point->file, '\\');
        base_name = base_name ? base_name + 1 : point->file;
        char loc_buf[32];
        snprintf(loc_buf, sizeof(loc_buf), "%s:%d", base_name, point->line);
        char msg_buf[32];
        if (point->message && strlen(point->message) > 0) {
            snprintf(msg_buf, sizeof(msg_buf), "%.28s%s", point->message,
                     strlen(point->message) > 28 ? ".." : "");
        } else {
            msg_buf[0] = '\0';
        }
        fprintf(stderr, "%-4zu %-7s %-6s %-30s %-20s %s\n", i, *point->enabled ? "ON" : "off",
                point->level, loc_buf, point->function, msg_buf);
    }
    fprintf(stderr, "\n");
    YTRACE_UNLOCK();
}

void ytime_timer_observe(struct ytime_timer *timer, const char *name, const char *file, int line,
                         const char *function, double elapsed_ms)
{
    YTRACE_LOCK();
    if (!timer->registered) {
        timer->name = name;
        timer->file = file;
        timer->line = line;
        timer->function = function;
        timer->count = 0;
        timer->sum_ms = 0.0;
        timer->last_ms = 0.0;
        timer->min_ms = elapsed_ms;
        timer->max_ms = elapsed_ms;
        timer->avg_ms = 0.0;
        if (g_timer_count < YTIME_MAX_TIMERS) {
            g_timers[g_timer_count++] = timer;
        } else {
            static bool warned = false;
            if (!warned) {
                fprintf(stderr, "[ytrace] WARNING: max timers (%d) exceeded\n", YTIME_MAX_TIMERS);
                warned = true;
            }
        }
        timer->registered = true;
    }
    timer->count++;
    timer->sum_ms += elapsed_ms;
    timer->last_ms = elapsed_ms;
    timer->avg_ms = timer->sum_ms / (double)timer->count;
    if (elapsed_ms < timer->min_ms) timer->min_ms = elapsed_ms;
    if (elapsed_ms > timer->max_ms) timer->max_ms = elapsed_ms;
    YTRACE_UNLOCK();
}

size_t ytime_timer_get_count(void)
{
    YTRACE_LOCK();
    size_t count = g_timer_count;
    YTRACE_UNLOCK();
    return count;
}

const struct ytime_timer *const *ytime_timer_get_all(void)
{
    return (const struct ytime_timer *const *)g_timers;
}

void ytime_timer_list(void)
{
    YTRACE_LOCK();
    fprintf(stderr, "\n[ytrace] Registered timers: %zu\n", g_timer_count);
    fprintf(stderr, "%-4s %-20s %-30s %10s %10s %10s %10s %10s\n", "IDX", "NAME", "FILE:LINE",
            "N", "LAST(ms)", "AVG(ms)", "MIN(ms)", "MAX(ms)");
    for (size_t i = 0; i < g_timer_count; i++) {
        const struct ytime_timer *timer = g_timers[i];
        const char *base_name = strrchr(timer->file, '/');
        if (!base_name) base_name = strrchr(timer->file, '\\');
        base_name = base_name ? base_name + 1 : timer->file;
        char loc_buf[32];
        snprintf(loc_buf, sizeof(loc_buf), "%s:%d", base_name, timer->line);
        fprintf(stderr, "%-4zu %-20s %-30s %10llu %10.3f %10.3f %10.3f %10.3f\n", i,
                timer->name ? timer->name : "", loc_buf, (unsigned long long)timer->count,
                timer->last_ms, timer->avg_ms, timer->min_ms, timer->max_ms);
    }
    fprintf(stderr, "\n");
    YTRACE_UNLOCK();
}

void ytime_timer_reset_all(void)
{
    YTRACE_LOCK();
    for (size_t i = 0; i < g_timer_count; i++) {
        struct ytime_timer *timer = g_timers[i];
        timer->count = 0;
        timer->sum_ms = 0.0;
        timer->last_ms = 0.0;
        timer->min_ms = 0.0;
        timer->max_ms = 0.0;
        timer->avg_ms = 0.0;
    }
    YTRACE_UNLOCK();
}

#endif /* YTRACE_C_ENABLED */
