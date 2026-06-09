/* yperf — config-driven Linux perf_event_open(2) counters. See yperf.h. */

#include <picomesh/core/yperf.h>
#include <picomesh/config/config.h>
#include <picomesh/loop/loop.h>
#include <picomesh/core/ytrace.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { YPERF_MAX_EVENTS = 24 };

/* Perf reporting is explicitly turned ON by config, so its output must be
 * visible without enabling the global trace switch — and a permission
 * failure must be loud whether or not tracing is on. Emit through
 * ytrace_output directly: identical format and stream to the gated y*()
 * macros, but unconditional. */
#define YPERF_EMIT(level, ...) ytrace_output((level), __FILE__, __LINE__, __func__, __VA_ARGS__)

/* ---- config reading off the perf map node -----------------------------
 *
 * config_get peels path segments on a miss, so a deep dotted lookup can
 * wrongly resolve to a same-named top-level key. We already hold the
 * `perf` map node, so read each child straight off it instead — no
 * inheritance fallback, no surprises. */
struct perf_child_lookup {
    const char *key;
    const struct config_node *found;
};

static int perf_child_lookup_cb(const char *key, const struct config_node *val, void *ud)
{
    struct perf_child_lookup *look = ud;
    if (strcmp(key, look->key) == 0) {
        look->found = val;
        return 1; /* stop */
    }
    return 0;
}

static const struct config_node *perf_node_child(const struct config_node *map, const char *key)
{
    if (!map || config_node_kind(map) != CONFIG_MAP) return NULL;
    struct perf_child_lookup look = {.key = key, .found = NULL};
    config_node_for_each(map, perf_child_lookup_cb, &look);
    return look.found;
}

static int perf_config_enabled(const struct config_node *perf_node)
{
    return perf_node && config_node_as_bool(perf_node_child(perf_node, "enabled"), 0);
}

#if defined(__linux__)

#include <asm/unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

/* One opened hardware/software counter. */
struct yperf_counter {
    char name[32];
    int fd;
    uint64_t prev_scaled; /* last cumulative (scaled) value, for deltas */
    int multiplexed;      /* last read was scaled (more events than PMU slots) */
};

struct yperf {
    char label[64];
    struct yperf_counter counters[YPERF_MAX_EVENTS];
    size_t count;
    unsigned interval_ms;
    int log;
    struct loop *loop;
    struct loop_timer *timer; /* NULL when log == 0 */
};

/* glibc exposes no wrapper for this syscall. */
static long perf_event_open_syscall(struct perf_event_attr *attr, pid_t pid, int cpu,
                                    int group_fd, unsigned long flags)
{
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

/* Supported event name -> (type, config). static const LOCAL — a
 * program-lifetime constant table with no file-scope symbol. */
struct perf_event_spec {
    const char *name;
    uint32_t type;
    uint64_t config;
};

static const struct perf_event_spec *perf_event_table(size_t *count)
{
    static const struct perf_event_spec ROWS[] = {
        {"cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
        {"cpu-cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
        {"instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS},
        {"cache-references", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES},
        {"cache-misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES},
        {"branch-instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS},
        {"branches", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS},
        {"branch-misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES},
        {"bus-cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BUS_CYCLES},
        {"ref-cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_REF_CPU_CYCLES},
        {"stalled-cycles-frontend", PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_FRONTEND},
        {"stalled-cycles-backend", PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_BACKEND},
        {"cpu-clock", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK},
        {"task-clock", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK},
        {"page-faults", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS},
        {"faults", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS},
        {"minor-faults", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MIN},
        {"major-faults", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MAJ},
        {"context-switches", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES},
        {"cs", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES},
        {"cpu-migrations", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_MIGRATIONS},
        {"migrations", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_MIGRATIONS},
    };
    *count = sizeof(ROWS) / sizeof(ROWS[0]);
    return ROWS;
}

static int perf_event_lookup(const char *name, uint32_t *type, uint64_t *config)
{
    size_t n = 0;
    const struct perf_event_spec *rows = perf_event_table(&n);
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(rows[i].name, name) == 0) {
            *type = rows[i].type;
            *config = rows[i].config;
            return 1;
        }
    }
    return 0;
}

/* What a read() yields with read_format = TOTAL_TIME_ENABLED|RUNNING. */
struct perf_read_format {
    uint64_t value;
    uint64_t time_enabled;
    uint64_t time_running;
};

/* Read a counter's current cumulative value, scaled up to compensate for
 * PMU time-multiplexing (when more events are configured than the CPU has
 * hardware slots, the kernel runs each for only part of the window). */
static uint64_t yperf_counter_scaled(struct yperf_counter *counter)
{
    struct perf_read_format raw = {0};
    ssize_t n = read(counter->fd, &raw, sizeof(raw));
    if (n != (ssize_t)sizeof(raw)) return counter->prev_scaled; /* read failed: no progress */

    counter->multiplexed = (raw.time_running > 0 && raw.time_running < raw.time_enabled);
    if (raw.time_running == 0) return 0;
    if (raw.time_running < raw.time_enabled)
        return (uint64_t)((double)raw.value * (double)raw.time_enabled / (double)raw.time_running);
    return raw.value;
}

/* Timer tick: log one line of per-event deltas since the previous tick. */
static void yperf_report_tick(void *ud)
{
    struct yperf *perf = ud;
    char line[512];
    size_t off = 0;
    int any_multiplexed = 0;

    off += (size_t)snprintf(line + off, sizeof(line) - off, "perf[%s] +%ums:",
                            perf->label, perf->interval_ms);
    for (size_t i = 0; i < perf->count && off < sizeof(line); ++i) {
        struct yperf_counter *counter = &perf->counters[i];
        uint64_t cur = yperf_counter_scaled(counter);
        uint64_t delta = cur >= counter->prev_scaled ? cur - counter->prev_scaled : 0;
        counter->prev_scaled = cur;
        if (counter->multiplexed) any_multiplexed = 1;
        off += (size_t)snprintf(line + off, sizeof(line) - off, " %s=%" PRIu64,
                                counter->name, delta);
    }
    if (any_multiplexed && off < sizeof(line))
        snprintf(line + off, sizeof(line) - off, " [scaled: events multiplexed]");
    YPERF_EMIT("info", "%s", line);
}

void yperf_destroy(struct yperf *perf)
{
    if (!perf) return;
    /* A summary only makes sense for a sampler that actually ran: the timer
     * is started last, so a NULL timer here means logging was off or create
     * bailed half-built — in either case skip the (misleading) totals line. */
    int was_running = perf->timer != NULL;
    if (perf->timer) {
        loop_timer_stop(perf->timer);
        perf->timer = NULL;
    }
    if (was_running && perf->count > 0) {
        char line[512];
        size_t off = 0;
        int any_multiplexed = 0;
        off += (size_t)snprintf(line + off, sizeof(line) - off, "perf[%s] totals:", perf->label);
        for (size_t i = 0; i < perf->count && off < sizeof(line); ++i) {
            struct yperf_counter *counter = &perf->counters[i];
            uint64_t cur = yperf_counter_scaled(counter);
            if (counter->multiplexed) any_multiplexed = 1;
            off += (size_t)snprintf(line + off, sizeof(line) - off, " %s=%" PRIu64,
                                    counter->name, cur);
        }
        if (any_multiplexed && off < sizeof(line))
            snprintf(line + off, sizeof(line) - off, " [scaled]");
        YPERF_EMIT("info", "%s", line);
    }
    for (size_t i = 0; i < perf->count; ++i)
        if (perf->counters[i].fd > 0) close(perf->counters[i].fd);
    free(perf);
}

struct yperf_ptr_result yperf_create(const struct config_node *perf_node,
                                     struct loop *loop, const char *label)
{
    if (!perf_config_enabled(perf_node)) return PICOMESH_OK(yperf_ptr, NULL);

    const char *tag = (label && *label) ? label : "process";

    const char *mode = config_node_as_string(perf_node_child(perf_node, "mode"), "counters");
    if (strcmp(mode, "counters") != 0)
        YPERF_EMIT("warn",
                   "yperf[%s]: mode '%s' not implemented yet — only 'counters' is supported; "
                   "sampling/callgraph will be added later. Treating as counters.",
                   tag, mode);

    const struct config_node *events = perf_node_child(perf_node, "events");
    if (!events || config_node_kind(events) != CONFIG_LIST || config_node_size(events) == 0)
        return PICOMESH_ERR(yperf_ptr,
                            "yperf: perf.enabled is true but perf.events is empty/absent — "
                            "list at least one event (e.g. cycles, instructions)");

    int interval_ms = (int)config_node_as_int(perf_node_child(perf_node, "interval_ms"), 1000);
    if (interval_ms < 10) interval_ms = 10;
    int do_log = config_node_as_bool(perf_node_child(perf_node, "log"), 1);
    int exclude_kernel = config_node_as_bool(perf_node_child(perf_node, "exclude_kernel"), 0);

    struct yperf *perf = calloc(1, sizeof(struct yperf));
    if (!perf) return PICOMESH_ERR(yperf_ptr, "yperf: calloc failed");
    snprintf(perf->label, sizeof(perf->label), "%s", tag);
    perf->interval_ms = (unsigned)interval_ms;
    perf->log = do_log;
    perf->loop = loop;

    size_t want = config_node_size(events);
    for (size_t i = 0; i < want; ++i) {
        if (perf->count >= YPERF_MAX_EVENTS) {
            YPERF_EMIT("warn", "yperf[%s]: more than %d events configured — ignoring the rest",
                       tag, YPERF_MAX_EVENTS);
            break;
        }
        const char *ename = config_node_as_string(config_node_at(events, i), NULL);
        if (!ename || !*ename) continue;

        uint32_t type = 0;
        uint64_t config = 0;
        if (!perf_event_lookup(ename, &type, &config)) {
            YPERF_EMIT("error",
                       "yperf[%s]: unknown perf event '%s' (see docs/perf-counters.md for the "
                       "supported set)",
                       tag, ename);
            yperf_destroy(perf);
            return PICOMESH_ERR(yperf_ptr,
                                "yperf: unknown perf event in config (see log for the name)");
        }

        struct perf_event_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.size = sizeof(attr);
        attr.type = type;
        attr.config = config;
        attr.disabled = 1; /* open quiescent, enable explicitly below */
        attr.exclude_kernel = exclude_kernel ? 1 : 0;
        attr.exclude_hv = 1;
        attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;

        /* pid=0, cpu=-1: count THIS worker thread across whatever CPU it
         * runs on. Each counter is independent (no group leader), so the
         * read format above lets us rescale around multiplexing. */
        long fd = perf_event_open_syscall(&attr, 0, -1, -1, 0);
        if (fd < 0) {
            int err = errno;
            YPERF_EMIT("error",
                       "yperf[%s]: perf_event_open(%s) failed: %s (errno=%d); "
                       "check kernel.perf_event_paranoid or CAP_PERFMON%s",
                       tag, ename, strerror(err), err,
                       (err == EACCES || err == EPERM)
                           ? " (try perf.exclude_kernel: true to measure user-space only)"
                           : "");
            yperf_destroy(perf);
            return PICOMESH_ERR(yperf_ptr,
                                "yperf: perf_event_open failed — profiling was requested by "
                                "config but the kernel refused it (see log for event + errno; "
                                "check kernel.perf_event_paranoid / CAP_PERFMON)");
        }

        ioctl((int)fd, PERF_EVENT_IOC_RESET, 0);
        ioctl((int)fd, PERF_EVENT_IOC_ENABLE, 0);

        struct yperf_counter *counter = &perf->counters[perf->count++];
        snprintf(counter->name, sizeof(counter->name), "%s", ename);
        counter->fd = (int)fd;
        counter->prev_scaled = 0;
        counter->multiplexed = 0;
    }

    if (perf->count == 0) {
        yperf_destroy(perf);
        return PICOMESH_ERR(yperf_ptr, "yperf: perf.events listed no usable event names");
    }

    if (perf->log) {
        struct loop_timer_ptr_result tr =
            loop_timer_start(loop, perf->interval_ms, yperf_report_tick, perf);
        if (PICOMESH_IS_ERR(tr)) {
            yperf_destroy(perf);
            return PICOMESH_ERR(yperf_ptr, "yperf: failed to start the reporter timer", tr);
        }
        perf->timer = tr.value;
    }

    YPERF_EMIT("info", "yperf[%s]: counting %zu event(s) every %ums%s%s",
               perf->label, perf->count, perf->interval_ms,
               exclude_kernel ? " (user-space only)" : "",
               perf->log ? "" : " (periodic logging off)");
    return PICOMESH_OK(yperf_ptr, perf);
}

#else /* !__linux__ — feature compiled out, but never silently. */

struct yperf {
    int unused;
};

void yperf_destroy(struct yperf *perf)
{
    (void)perf;
}

struct yperf_ptr_result yperf_create(const struct config_node *perf_node,
                                     struct loop *loop, const char *label)
{
    (void)loop;
    if (perf_config_enabled(perf_node))
        YPERF_EMIT("warn",
                   "yperf[%s]: perf profiling requested in config but perf_event_open(2) is "
                   "Linux-only — this build cannot profile; ignoring perf.enabled.",
                   (label && *label) ? label : "process");
    return PICOMESH_OK(yperf_ptr, NULL);
}

#endif /* __linux__ */
