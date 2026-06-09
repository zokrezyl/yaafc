/* yspan — in-memory trace-span collector (see yspan.h). */

#include <picomesh/core/yspan.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define YSPAN_MAX 65536
#define YSPAN_OP_MAX 48

struct yspan_entry {
    char op[YSPAN_OP_MAX];
    double dur_us;
};

struct yspan_state {
    struct yspan_entry e[YSPAN_MAX];
    size_t n;       /* total recorded (capped at YSPAN_MAX) */
    int overflow;   /* set once we stop appending */
};

/* Process-global singleton. Spans are now recorded from the loop thread
 * AND from worker-pool threads (the gateway offloads forwards there), so
 * a small lock guards the ring. */
static struct yspan_state *yspan_state(void)
{
    static struct yspan_state s = {0};
    return &s;
}

static pthread_mutex_t *yspan_lock(void)
{
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    return &mu;
}

void yspan_record(const char *op, double dur_us)
{
    struct yspan_state *state = yspan_state();
    pthread_mutex_lock(yspan_lock());
    if (state->n >= YSPAN_MAX) { state->overflow = 1; pthread_mutex_unlock(yspan_lock()); return; }
    struct yspan_entry *entry = &state->e[state->n++];
    size_t i = 0;
    if (op) for (; op[i] && i < YSPAN_OP_MAX - 1; ++i) entry->op[i] = op[i];
    entry->op[i] = 0;
    entry->dur_us = dur_us;
    pthread_mutex_unlock(yspan_lock());
}

void yspan_reset(void)
{
    struct yspan_state *state = yspan_state();
    pthread_mutex_lock(yspan_lock());
    state->n = 0;
    state->overflow = 0;
    pthread_mutex_unlock(yspan_lock());
}

static int cmp_double(const void *a, const void *b)
{
    double left = *(const double *)a, right = *(const double *)b;
    return (left > right) - (left < right);
}

static double pctl(const double *sorted, size_t count, double percentile)
{
    if (!count) return 0.0;
    size_t idx = (size_t)(percentile / 100.0 * (double)(count - 1) + 0.5);
    if (idx >= count) idx = count - 1;
    return sorted[idx];
}

size_t yspan_dump(char *buf, size_t cap)
{
    struct yspan_state *state = yspan_state();
    pthread_mutex_lock(yspan_lock());
    size_t off = 0;
    int written = snprintf(buf + off, cap - off,
        "%-34s %8s %9s %9s %9s %9s\n",
        "op", "count", "p50_us", "p90_us", "p99_us", "max_us");
    if (written > 0) off += (size_t)written;

    /* Distinct ops, in first-seen order. */
    char ops[256][YSPAN_OP_MAX];
    size_t op_count = 0;
    for (size_t i = 0; i < state->n; ++i) {
        int found = 0;
        for (size_t j = 0; j < op_count; ++j)
            if (strcmp(ops[j], state->e[i].op) == 0) { found = 1; break; }
        if (!found && op_count < 256) {
            memcpy(ops[op_count], state->e[i].op, YSPAN_OP_MAX);
            op_count++;
        }
    }

    double *durs = malloc(state->n ? state->n * sizeof(double) : 1);
    if (!durs) { if (off < cap) buf[off] = 0; pthread_mutex_unlock(yspan_lock()); return off; }

    for (size_t j = 0; j < op_count; ++j) {
        size_t sample_count = 0;
        for (size_t i = 0; i < state->n; ++i)
            if (strcmp(state->e[i].op, ops[j]) == 0) durs[sample_count++] = state->e[i].dur_us;
        qsort(durs, sample_count, sizeof(double), cmp_double);
        if (off < cap) {
            written = snprintf(buf + off, cap - off,
                "%-34s %8zu %9.0f %9.0f %9.0f %9.0f\n",
                ops[j], sample_count, pctl(durs, sample_count, 50),
                pctl(durs, sample_count, 90),
                pctl(durs, sample_count, 99), durs[sample_count ? sample_count - 1 : 0]);
            if (written > 0) off += (size_t)written;
        }
    }
    free(durs);

    if (state->overflow && off < cap)
        off += (size_t)snprintf(buf + off, cap - off,
                                "(ring full at %d spans — older not shown)\n", YSPAN_MAX);
    if (off < cap) buf[off] = 0;
    else if (cap) buf[cap - 1] = 0;
    pthread_mutex_unlock(yspan_lock());
    return off;
}
