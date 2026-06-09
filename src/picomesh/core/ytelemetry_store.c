/* ytelemetry_store — in-memory trace collector store (see ytelemetry_store.h).
 *
 * Sharded for ingest parallelism, with a per-worker lock-free arena in front:
 *
 *   - Each worker thread accumulates parsed spans in a thread-confined arena,
 *     pre-bucketed by destination shard. The hot path takes NO lock (worker
 *     coroutines are cooperative, so the arena has a single writer).
 *   - A shard lock is taken only to FLUSH a full bucket (≈1 lock per
 *     `bucket_spans` spans) or on the periodic time flush — never per span.
 *   - The shared store is `shards` independent rings, each its own lock; a
 *     span's shard is hash(trace_id) % shards, so all spans of a trace stay
 *     co-located (single-shard get_trace; multi-trace queries fan out + merge).
 *
 * Every size/cadence knob is configurable via ytelemetry_store_init_config. */

#include <picomesh/core/ytelemetry_store.h>

#include <picomesh/json/json.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <uthash.h>

#define YTEL_DEFAULT_MAX_SPANS 50000
#define YTEL_DEFAULT_SHARDS 16u
#define YTEL_DEFAULT_BUCKET_SPANS 256
#define YTEL_DEFAULT_FLUSH_MS 50

struct ytelemetry_stored_span {
    char trace_id[33];
    char span_id[17];
    char parent_id[17];
    char name[64];
    char service[64];
    char node[80];
    char status[8]; /* "ok" / "error" */
    char kind[12];
    char err[96];
    uint64_t start_unix_ns;
    uint64_t duration_ns;
    uint32_t uid;
    int used;
};

/* Incrementally-maintained summary of one trace's live spans in a shard, so
 * the list query never rescans the ring. Created/updated as spans are stored,
 * decremented as they are evicted, removed when the trace's last span ages out.
 * Keyed by trace_id in the shard's `index` hash. */
struct ytel_trace_idx {
    char trace_id[33]; /* hash key */
    char root_name[64];
    char root_service[64];
    uint64_t start;
    uint64_t end;
    uint32_t live; /* non-evicted spans of this trace currently in the ring */
    int error;
    UT_hash_handle hh;
};

struct ytel_shard {
    pthread_mutex_t mu;
    struct ytelemetry_stored_span *ring;
    struct ytel_trace_idx *index; /* trace_id -> live summary */
    size_t cap;
    size_t head;  /* next write slot */
    size_t count; /* live entries (<= cap) */
    uint64_t ingested;
    uint64_t malformed;
    uint64_t evicted;
};

struct ytelemetry_store {
    struct ytel_shard *shards; /* [shard_count], allocated at init */
    unsigned shard_count;
    size_t shard_cap;    /* per-shard ring capacity */
    size_t bucket_spans; /* per-shard arena bucket size */
    uint64_t max_age_ns;
    uint64_t flush_ns;
    int inited;
    pthread_mutex_t init_mu;
};

static struct ytelemetry_store *ytelemetry_store_state(void)
{
    static struct ytelemetry_store store = {.init_mu = PTHREAD_MUTEX_INITIALIZER};
    return &store;
}

static uint64_t ytelemetry_store_now_ns(void)
{
    struct timespec ts = {0};
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void ytelemetry_store_copystr(char *dst, size_t cap, const char *src)
{
    if (!cap) return;
    size_t i = 0;
    if (src) for (; src[i] && i < cap - 1; ++i) dst[i] = src[i];
    dst[i] = 0;
}

/* FNV-1a over the trace_id, reduced to a shard index. */
static unsigned ytelemetry_shard_for(const char *trace_id, unsigned shard_count)
{
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char *byte = (const unsigned char *)trace_id; byte && *byte; ++byte) {
        hash ^= *byte;
        hash *= 1099511628211ull;
    }
    return (unsigned)(hash % shard_count);
}

/* Apply config + allocate shards. Caller holds init_mu; first call wins. */
static void ytelemetry_store_setup_locked(struct ytelemetry_store *store,
                                          const struct ytelemetry_store_config *config)
{
    if (store->inited) return;

    struct ytelemetry_store_config cfg = config ? *config : (struct ytelemetry_store_config){0};
    size_t total = cfg.max_spans ? cfg.max_spans : YTEL_DEFAULT_MAX_SPANS;
    store->shard_count = cfg.shards ? cfg.shards : YTEL_DEFAULT_SHARDS;
    store->bucket_spans = cfg.bucket_spans ? cfg.bucket_spans : YTEL_DEFAULT_BUCKET_SPANS;
    store->flush_ns = (cfg.flush_ms ? cfg.flush_ms : YTEL_DEFAULT_FLUSH_MS) * 1000000ull;
    store->max_age_ns = cfg.max_age_seconds * 1000000000ull;

    store->shards = calloc(store->shard_count, sizeof(*store->shards));
    if (!store->shards) { store->shard_count = 1; store->shards = calloc(1, sizeof(*store->shards)); }
    store->shard_cap = total / store->shard_count;
    if (!store->shard_cap) store->shard_cap = 1;
    for (unsigned k = 0; k < store->shard_count; ++k)
        pthread_mutex_init(&store->shards[k].mu, NULL);

    store->inited = 1;
}

static void ytelemetry_store_ensure(struct ytelemetry_store *store)
{
    if (store->inited) return;
    pthread_mutex_lock(&store->init_mu);
    ytelemetry_store_setup_locked(store, NULL);
    pthread_mutex_unlock(&store->init_mu);
}

void ytelemetry_store_init_config(const struct ytelemetry_store_config *config)
{
    struct ytelemetry_store *store = ytelemetry_store_state();
    pthread_mutex_lock(&store->init_mu);
    ytelemetry_store_setup_locked(store, config);
    pthread_mutex_unlock(&store->init_mu);
}

void ytelemetry_store_init(size_t max_spans, uint64_t max_age_seconds)
{
    struct ytelemetry_store_config cfg = {.max_spans = max_spans, .max_age_seconds = max_age_seconds};
    ytelemetry_store_init_config(&cfg);
}

/* Lazily allocate a shard's ring. Caller holds the shard lock. */
static void ytelemetry_shard_ensure_ring(struct ytel_shard *shard, size_t cap)
{
    if (shard->ring) return;
    shard->cap = cap ? cap : 1;
    shard->ring = calloc(shard->cap, sizeof(*shard->ring));
    if (!shard->ring) shard->cap = 0;
}

/* The i-th newest live span in a shard (0 = newest). Caller holds shard lock. */
static struct ytelemetry_stored_span *ytelemetry_shard_nth(struct ytel_shard *shard, size_t i)
{
    if (i >= shard->count) return NULL;
    size_t idx = (shard->head + shard->cap - 1 - i) % shard->cap;
    return &shard->ring[idx];
}

static int ytelemetry_store_fresh(uint64_t max_age_ns,
                                  const struct ytelemetry_stored_span *span, uint64_t now)
{
    if (!max_age_ns) return 1;
    return span->start_unix_ns + max_age_ns >= now;
}

/* Copy a parsed span into a stored_span (all strings interned, so the source
 * json_doc can be freed immediately afterwards). */
static void ytelemetry_fill_span(struct ytelemetry_stored_span *dst, const struct json_value *root,
                                 const char *trace, const char *span)
{
    memset(dst, 0, sizeof(*dst));
    ytelemetry_store_copystr(dst->trace_id, sizeof(dst->trace_id), trace);
    ytelemetry_store_copystr(dst->span_id, sizeof(dst->span_id), span);
    ytelemetry_store_copystr(dst->parent_id, sizeof(dst->parent_id),
                       json_as_string(json_object_get(root, "parent_span_id"), ""));
    ytelemetry_store_copystr(dst->name, sizeof(dst->name),
                       json_as_string(json_object_get(root, "name"), ""));
    ytelemetry_store_copystr(dst->service, sizeof(dst->service),
                       json_as_string(json_object_get(root, "service_name"), ""));
    ytelemetry_store_copystr(dst->node, sizeof(dst->node),
                       json_as_string(json_object_get(root, "node_id"), ""));
    ytelemetry_store_copystr(dst->status, sizeof(dst->status),
                       json_as_string(json_object_get(root, "status"), "ok"));
    ytelemetry_store_copystr(dst->kind, sizeof(dst->kind),
                       json_as_string(json_object_get(root, "kind"), "internal"));
    ytelemetry_store_copystr(dst->err, sizeof(dst->err),
                       json_as_string(json_object_get(root, "error_message"), ""));
    dst->start_unix_ns = (uint64_t)json_as_int(json_object_get(root, "start_time_ns"), 0);
    dst->duration_ns = (uint64_t)json_as_int(json_object_get(root, "duration_ns"), 0);
    const struct json_value *attrs = json_object_get(root, "attributes");
    if (attrs)
        dst->uid = (uint32_t)json_as_int(json_object_get(attrs, "picomesh.uid"), 0);
    dst->used = 1;
}

/* Append one already-filled span into a shard's ring. Caller holds shard->mu. */
static void ytelemetry_shard_put(struct ytel_shard *shard, size_t cap,
                                 const struct ytelemetry_stored_span *src)
{
    ytelemetry_shard_ensure_ring(shard, cap);
    if (!shard->ring) { shard->malformed++; return; }
    if (shard->count == shard->cap) {
        /* Evicting ring[head] — drop it from its trace's index entry. */
        struct ytelemetry_stored_span *old = &shard->ring[shard->head];
        if (old->used) {
            struct ytel_trace_idx *entry = NULL;
            HASH_FIND_STR(shard->index, old->trace_id, entry);
            if (entry && entry->live && --entry->live == 0) { HASH_DEL(shard->index, entry); free(entry); }
        }
        shard->evicted++;
    }
    shard->ring[shard->head] = *src;
    shard->head = (shard->head + 1) % shard->cap;
    if (shard->count < shard->cap) shard->count++;
    shard->ingested++;

    /* Maintain the per-trace summary so list/detail queries are O(1) lookups. */
    struct ytel_trace_idx *entry = NULL;
    HASH_FIND_STR(shard->index, src->trace_id, entry);
    if (!entry) {
        entry = calloc(1, sizeof(*entry));
        if (entry) {
            ytelemetry_store_copystr(entry->trace_id, sizeof(entry->trace_id), src->trace_id);
            entry->start = src->start_unix_ns;
            entry->end = src->start_unix_ns + src->duration_ns;
            HASH_ADD_STR(shard->index, trace_id, entry);
        }
    }
    if (entry) {
        entry->live++;
        if (src->start_unix_ns < entry->start) entry->start = src->start_unix_ns;
        uint64_t end = src->start_unix_ns + src->duration_ns;
        if (end > entry->end) entry->end = end;
        if (strcmp(src->status, "error") == 0) entry->error = 1;
        if (!entry->root_name[0] || !src->parent_id[0]) { /* first span, or the real root */
            ytelemetry_store_copystr(entry->root_name, sizeof(entry->root_name), src->name);
            ytelemetry_store_copystr(entry->root_service, sizeof(entry->root_service), src->service);
        }
    }
}

/* Per-worker, thread-confined accumulation arena (see file header). Buckets are
 * a flat [shard_count * bucket_spans] array, sized from the store config the
 * first time this thread ingests. */
struct ytel_arena {
    struct ytelemetry_stored_span *spans; /* shard_count * bucket_spans */
    size_t *fill;                         /* per-shard fill counts */
    unsigned shard_count;
    size_t bucket_spans;
    size_t total;
    uint64_t last_flush_ns;
};

static __thread struct ytel_arena ytel_tls_arena;

static void ytelemetry_arena_flush_shard(struct ytelemetry_store *store, struct ytel_arena *arena,
                                         unsigned shard_index)
{
    if (!arena->fill[shard_index]) return;
    struct ytel_shard *shard = &store->shards[shard_index];
    struct ytelemetry_stored_span *bucket = &arena->spans[(size_t)shard_index * arena->bucket_spans];
    pthread_mutex_lock(&shard->mu);
    for (size_t i = 0; i < arena->fill[shard_index]; ++i)
        ytelemetry_shard_put(shard, store->shard_cap, &bucket[i]);
    pthread_mutex_unlock(&shard->mu);
    arena->total -= arena->fill[shard_index];
    arena->fill[shard_index] = 0;
}

static void ytelemetry_arena_flush_all(struct ytelemetry_store *store, struct ytel_arena *arena,
                                       uint64_t now)
{
    for (unsigned k = 0; k < arena->shard_count; ++k) ytelemetry_arena_flush_shard(store, arena, k);
    arena->last_flush_ns = now;
}

void ytelemetry_store_flush_local(void)
{
    struct ytel_arena *arena = &ytel_tls_arena;
    if (!arena->spans || !arena->total) return;
    ytelemetry_arena_flush_all(ytelemetry_store_state(), arena, ytelemetry_store_now_ns());
}

/* Append one already-parsed span object into the calling thread's arena. The
 * lock-free hot path shared by the single and batch ingest entry points.
 * Returns 1 if accepted, 0 if the span is missing required fields. */
static int ytelemetry_append_parsed(struct ytelemetry_store *store, const struct json_value *root)
{
    const char *trace = root ? json_as_string(json_object_get(root, "trace_id"), NULL) : NULL;
    const char *span = root ? json_as_string(json_object_get(root, "span_id"), NULL) : NULL;
    if (!trace || !span) {
        struct ytel_shard *shard = &store->shards[0];
        pthread_mutex_lock(&shard->mu); shard->malformed++; pthread_mutex_unlock(&shard->mu);
        return 0;
    }

    unsigned shard_index = ytelemetry_shard_for(trace, store->shard_count);

    struct ytel_arena *arena = &ytel_tls_arena;
    if (!arena->spans) {
        arena->shard_count = store->shard_count;
        arena->bucket_spans = store->bucket_spans;
        arena->spans = calloc((size_t)arena->shard_count * arena->bucket_spans, sizeof(*arena->spans));
        arena->fill = calloc(arena->shard_count, sizeof(*arena->fill));
        if (!arena->spans || !arena->fill) {
            /* No arena (allocation failed): fall back to a direct locked put. */
            free(arena->spans); free(arena->fill); arena->spans = NULL; arena->fill = NULL;
            struct ytelemetry_stored_span one;
            ytelemetry_fill_span(&one, root, trace, span);
            struct ytel_shard *shard = &store->shards[shard_index];
            pthread_mutex_lock(&shard->mu);
            ytelemetry_shard_put(shard, store->shard_cap, &one);
            pthread_mutex_unlock(&shard->mu);
            return 1;
        }
    }

    /* Lock-free hot path: fill the parsed span into this worker's own bucket. */
    struct ytelemetry_stored_span *slot =
        &arena->spans[(size_t)shard_index * arena->bucket_spans + arena->fill[shard_index]];
    ytelemetry_fill_span(slot, root, trace, span);
    arena->fill[shard_index]++;
    arena->total++;

    if (arena->fill[shard_index] == arena->bucket_spans) {
        ytelemetry_arena_flush_shard(store, arena, shard_index);
    } else if ((arena->total & 63) == 0) {
        uint64_t now = ytelemetry_store_now_ns();
        if (now - arena->last_flush_ns > store->flush_ns)
            ytelemetry_arena_flush_all(store, arena, now);
    }
    return 1;
}

static void ytelemetry_count_malformed(struct ytelemetry_store *store)
{
    struct ytel_shard *shard = &store->shards[0];
    pthread_mutex_lock(&shard->mu); shard->malformed++; pthread_mutex_unlock(&shard->mu);
}

int ytelemetry_store_ingest_json(const char *json, size_t len)
{
    struct ytelemetry_store *store = ytelemetry_store_state();
    ytelemetry_store_ensure(store);
    struct json_doc *doc = json_parse(json, len);
    if (!doc) { ytelemetry_count_malformed(store); return 0; }
    int accepted = ytelemetry_append_parsed(store, json_doc_root(doc));
    json_doc_free(doc);
    return accepted;
}

/* Ingest a batch: the payload may be a JSON array of span objects (the
 * batched sender path) or a single span object (back-compat). Parsed ONCE,
 * then each element flows through the lock-free arena append. */
int ytelemetry_store_ingest_batch_json(const char *json, size_t len)
{
    struct ytelemetry_store *store = ytelemetry_store_state();
    ytelemetry_store_ensure(store);
    struct json_doc *doc = json_parse(json, len);
    if (!doc) { ytelemetry_count_malformed(store); return 0; }
    const struct json_value *root = json_doc_root(doc);
    int accepted = 0;
    if (json_is_array(root)) {
        size_t count = json_array_size(root);
        for (size_t i = 0; i < count; ++i)
            accepted += ytelemetry_append_parsed(store, json_array_at(root, i));
    } else {
        accepted = ytelemetry_append_parsed(store, root);
    }
    json_doc_free(doc);
    return accepted;
}

/* {ingested, malformed, evicted, stored, capacity, max_age_seconds} */
void ytelemetry_store_write_stats(struct json_writer *writer)
{
    struct ytelemetry_store *store = ytelemetry_store_state();
    ytelemetry_store_ensure(store);
    uint64_t ingested = 0, malformed = 0, evicted = 0, stored = 0;
    for (unsigned k = 0; k < store->shard_count; ++k) {
        struct ytel_shard *shard = &store->shards[k];
        pthread_mutex_lock(&shard->mu);
        ingested += shard->ingested;
        malformed += shard->malformed;
        evicted += shard->evicted;
        stored += shard->count;
        pthread_mutex_unlock(&shard->mu);
    }
    json_writer_begin_object(writer);
    json_writer_key(writer, "ingested"); json_writer_int(writer, (int64_t)ingested);
    json_writer_key(writer, "malformed"); json_writer_int(writer, (int64_t)malformed);
    json_writer_key(writer, "evicted"); json_writer_int(writer, (int64_t)evicted);
    json_writer_key(writer, "stored"); json_writer_int(writer, (int64_t)stored);
    json_writer_key(writer, "capacity"); json_writer_int(writer, (int64_t)(store->shard_cap * store->shard_count));
    json_writer_key(writer, "max_age_seconds"); json_writer_int(writer, (int64_t)(store->max_age_ns / 1000000000ull));
    json_writer_key(writer, "shards"); json_writer_int(writer, (int64_t)store->shard_count);
    json_writer_end_object(writer);
}

/* ---- query writers ---------------------------------------------------- */

static int cmp_u64(const void *a, const void *b)
{
    uint64_t left = *(const uint64_t *)a, right = *(const uint64_t *)b;
    return (left > right) - (left < right);
}

static uint64_t pctl_u64(const uint64_t *sorted, size_t count, double percentile)
{
    if (!count) return 0;
    size_t idx = (size_t)(percentile / 100.0 * (double)(count - 1) + 0.5);
    if (idx >= count) idx = count - 1;
    return sorted[idx];
}

static void emit_span(struct json_writer *writer, const struct ytelemetry_stored_span *span)
{
    json_writer_begin_object(writer);
    json_writer_key(writer, "span_id"); json_writer_string(writer, span->span_id);
    json_writer_key(writer, "parent_span_id"); json_writer_string(writer, span->parent_id);
    json_writer_key(writer, "trace_id"); json_writer_string(writer, span->trace_id);
    json_writer_key(writer, "name"); json_writer_string(writer, span->name);
    json_writer_key(writer, "kind"); json_writer_string(writer, span->kind);
    json_writer_key(writer, "service_name"); json_writer_string(writer, span->service);
    json_writer_key(writer, "node_id"); json_writer_string(writer, span->node);
    json_writer_key(writer, "start_time_ns"); json_writer_int(writer, (int64_t)span->start_unix_ns);
    json_writer_key(writer, "duration_ns"); json_writer_int(writer, (int64_t)span->duration_ns);
    json_writer_key(writer, "status"); json_writer_string(writer, span->status);
    if (span->err[0]) { json_writer_key(writer, "error_message"); json_writer_string(writer, span->err); }
    json_writer_key(writer, "attributes");
    json_writer_begin_object(writer);
    json_writer_key(writer, "picomesh.uid"); json_writer_int(writer, (int64_t)span->uid);
    json_writer_end_object(writer);
    json_writer_end_object(writer);
}

/* {trace_id, root_span_id, duration_ns, span_count, spans:[...]} — single
 * shard: every span of a trace hashes to the same shard. */
void ytelemetry_store_write_trace(struct json_writer *writer, const char *trace_id)
{
    struct ytelemetry_store *store = ytelemetry_store_state();
    ytelemetry_store_ensure(store);
    struct ytel_shard *shard =
        &store->shards[ytelemetry_shard_for(trace_id ? trace_id : "", store->shard_count)];
    pthread_mutex_lock(&shard->mu);

    /* The index tells us how many live spans to expect, so the scan stops as
     * soon as they are all found (recent traces sit near the ring front). */
    struct ytel_trace_idx *trace_idx = NULL;
    if (trace_id) HASH_FIND_STR(shard->index, trace_id, trace_idx);
    size_t want = trace_idx ? trace_idx->live : 0;

    enum { CAP = 8192 };
    struct ytelemetry_stored_span *match[CAP];
    size_t match_count = 0;
    for (size_t i = 0; i < shard->count && match_count < CAP && match_count < want; ++i) {
        struct ytelemetry_stored_span *span = ytelemetry_shard_nth(shard, i); /* newest first */
        if (span->used && trace_id && strcmp(span->trace_id, trace_id) == 0)
            match[match_count++] = span;
    }

    uint64_t min_start = 0, max_end = 0;
    for (size_t i = 0; i < match_count; ++i) {
        uint64_t end = match[i]->start_unix_ns + match[i]->duration_ns;
        if (i == 0 || match[i]->start_unix_ns < min_start) min_start = match[i]->start_unix_ns;
        if (i == 0 || end > max_end) max_end = end;
    }

    const char *root_span = "";
    for (size_t i = 0; i < match_count && !root_span[0]; ++i)
        if (!match[i]->parent_id[0]) root_span = match[i]->span_id;
    for (size_t i = 0; i < match_count && !root_span[0]; ++i) {
        int parent_in = 0;
        for (size_t j = 0; j < match_count; ++j)
            if (strcmp(match[j]->span_id, match[i]->parent_id) == 0) { parent_in = 1; break; }
        if (!parent_in) root_span = match[i]->span_id;
    }

    json_writer_begin_object(writer);
    json_writer_key(writer, "trace_id"); json_writer_string(writer, trace_id ? trace_id : "");
    json_writer_key(writer, "root_span_id"); json_writer_string(writer, root_span);
    json_writer_key(writer, "span_count"); json_writer_int(writer, (int64_t)match_count);
    json_writer_key(writer, "duration_ns"); json_writer_int(writer, (int64_t)(match_count ? max_end - min_start : 0));
    json_writer_key(writer, "spans");
    json_writer_begin_array(writer);
    for (size_t i = 0; i < match_count; ++i) emit_span(writer, match[i]);
    json_writer_end_array(writer);
    json_writer_end_object(writer);

    pthread_mutex_unlock(&shard->mu);
}

/* One summarised trace, copied out so the shard lock is released before merge. */
struct ytel_trace_sum {
    char trace_id[33];
    char root_name[64];
    char root_service[64];
    uint64_t start;
    uint64_t end;
    size_t span_count;
    int error;
};

static int cmp_sum_newest(const void *a, const void *b)
{
    const struct ytel_trace_sum *left = a, *right = b;
    return (left->start < right->start) - (left->start > right->start); /* newest first */
}

void ytelemetry_store_write_traces(struct json_writer *writer, const char *service,
                             uint64_t since_ns, const char *status)
{
    struct ytelemetry_store *store = ytelemetry_store_state();
    ytelemetry_store_ensure(store);
    uint64_t now = ytelemetry_store_now_ns();

    enum { PER_SHARD = 64 };
    struct ytel_trace_sum *cand = malloc((size_t)store->shard_count * PER_SHARD * sizeof(*cand));
    size_t ncand = 0;

    if (cand) {
        for (unsigned k = 0; k < store->shard_count; ++k) {
            struct ytel_shard *shard = &store->shards[k];
            pthread_mutex_lock(&shard->mu);

            char seen[PER_SHARD][33];
            size_t nseen = 0;
            for (size_t i = 0; i < shard->count && nseen < PER_SHARD; ++i) {
                struct ytelemetry_stored_span *span = ytelemetry_shard_nth(shard, i); /* newest→oldest */
                if (!span->used || !ytelemetry_store_fresh(store->max_age_ns, span, now)) continue;
                if (since_ns && span->start_unix_ns < since_ns) continue;
                if (service && service[0] && strcmp(span->service, service) != 0) continue;

                int dup = 0;
                for (size_t j = 0; j < nseen; ++j)
                    if (strcmp(seen[j], span->trace_id) == 0) { dup = 1; break; }
                if (dup) continue;

                /* Summary is precomputed in the index — no per-trace ring scan. */
                struct ytel_trace_idx *entry = NULL;
                HASH_FIND_STR(shard->index, span->trace_id, entry);
                if (!entry) continue; /* span racing its own eviction — skip */

                struct ytel_trace_sum sum = {0};
                ytelemetry_store_copystr(sum.trace_id, sizeof(sum.trace_id), span->trace_id);
                ytelemetry_store_copystr(sum.root_name, sizeof(sum.root_name), entry->root_name);
                ytelemetry_store_copystr(sum.root_service, sizeof(sum.root_service), entry->root_service);
                sum.start = entry->start;
                sum.end = entry->end;
                sum.span_count = entry->live;
                sum.error = entry->error;

                ytelemetry_store_copystr(seen[nseen], sizeof(seen[nseen]), span->trace_id);
                nseen++;
                cand[ncand++] = sum;
            }
            pthread_mutex_unlock(&shard->mu);
        }
        qsort(cand, ncand, sizeof(*cand), cmp_sum_newest);
    }

    json_writer_begin_object(writer);
    json_writer_key(writer, "traces");
    json_writer_begin_array(writer);
    size_t emitted = 0;
    for (size_t i = 0; i < ncand && emitted < 256; ++i) {
        const char *tstatus = cand[i].error ? "error" : "ok";
        if (status && status[0] && strcmp(status, tstatus) != 0) continue;
        json_writer_begin_object(writer);
        json_writer_key(writer, "trace_id"); json_writer_string(writer, cand[i].trace_id);
        json_writer_key(writer, "root_name"); json_writer_string(writer, cand[i].root_name);
        json_writer_key(writer, "service_name"); json_writer_string(writer, cand[i].root_service);
        json_writer_key(writer, "start_time_ns"); json_writer_int(writer, (int64_t)cand[i].start);
        json_writer_key(writer, "duration_ns"); json_writer_int(writer, (int64_t)(cand[i].end - cand[i].start));
        json_writer_key(writer, "span_count"); json_writer_int(writer, (int64_t)cand[i].span_count);
        json_writer_key(writer, "status"); json_writer_string(writer, tstatus);
        json_writer_end_object(writer);
        emitted++;
    }
    json_writer_end_array(writer);
    json_writer_end_object(writer);
    free(cand);
}

void ytelemetry_store_write_services(struct json_writer *writer)
{
    struct ytelemetry_store *store = ytelemetry_store_state();
    ytelemetry_store_ensure(store);

    enum { MAX_SVC = 128 };
    char names[MAX_SVC][64];
    size_t counts[MAX_SVC] = {0};
    size_t name_count = 0;
    for (unsigned k = 0; k < store->shard_count; ++k) {
        struct ytel_shard *shard = &store->shards[k];
        pthread_mutex_lock(&shard->mu);
        for (size_t i = 0; i < shard->count; ++i) {
            struct ytelemetry_stored_span *span = ytelemetry_shard_nth(shard, i);
            if (!span->used || !span->service[0]) continue;
            size_t j = 0;
            for (; j < name_count; ++j) if (strcmp(names[j], span->service) == 0) break;
            if (j == name_count && name_count < MAX_SVC) { ytelemetry_store_copystr(names[name_count], 64, span->service); name_count++; }
            if (j < MAX_SVC) counts[j]++;
        }
        pthread_mutex_unlock(&shard->mu);
    }

    json_writer_begin_object(writer);
    json_writer_key(writer, "services");
    json_writer_begin_array(writer);
    for (size_t j = 0; j < name_count; ++j) {
        json_writer_begin_object(writer);
        json_writer_key(writer, "service_name"); json_writer_string(writer, names[j]);
        json_writer_key(writer, "span_count"); json_writer_int(writer, (int64_t)counts[j]);
        json_writer_end_object(writer);
    }
    json_writer_end_array(writer);
    json_writer_end_object(writer);
}

void ytelemetry_store_write_operations(struct json_writer *writer, const char *service)
{
    struct ytelemetry_store *store = ytelemetry_store_state();
    ytelemetry_store_ensure(store);

    enum { MAX_OPS = 256 };
    char ops[MAX_OPS][64];
    size_t counts[MAX_OPS] = {0};
    size_t op_count = 0;
    for (unsigned k = 0; k < store->shard_count; ++k) {
        struct ytel_shard *shard = &store->shards[k];
        pthread_mutex_lock(&shard->mu);
        for (size_t i = 0; i < shard->count; ++i) {
            struct ytelemetry_stored_span *span = ytelemetry_shard_nth(shard, i);
            if (!span->used || !span->name[0]) continue;
            if (service && service[0] && strcmp(span->service, service) != 0) continue;
            size_t j = 0;
            for (; j < op_count; ++j) if (strcmp(ops[j], span->name) == 0) break;
            if (j == op_count && op_count < MAX_OPS) { ytelemetry_store_copystr(ops[op_count], 64, span->name); op_count++; }
            if (j < MAX_OPS) counts[j]++;
        }
        pthread_mutex_unlock(&shard->mu);
    }

    json_writer_begin_object(writer);
    json_writer_key(writer, "service"); json_writer_string(writer, service ? service : "");
    json_writer_key(writer, "operations");
    json_writer_begin_array(writer);
    for (size_t j = 0; j < op_count; ++j) {
        json_writer_begin_object(writer);
        json_writer_key(writer, "name"); json_writer_string(writer, ops[j]);
        json_writer_key(writer, "count"); json_writer_int(writer, (int64_t)counts[j]);
        json_writer_end_object(writer);
    }
    json_writer_end_array(writer);
    json_writer_end_object(writer);
}

void ytelemetry_store_write_latency(struct json_writer *writer, const char *service,
                             const char *operation, uint64_t window_ns)
{
    struct ytelemetry_store *store = ytelemetry_store_state();
    ytelemetry_store_ensure(store);
    uint64_t now = ytelemetry_store_now_ns();
    uint64_t floor = window_ns && now > window_ns ? now - window_ns : 0;

    size_t total = 0;
    for (unsigned k = 0; k < store->shard_count; ++k) {
        pthread_mutex_lock(&store->shards[k].mu);
        total += store->shards[k].count;
        pthread_mutex_unlock(&store->shards[k].mu);
    }

    uint64_t *durs = malloc((total ? total : 1) * sizeof(uint64_t));
    size_t count = 0;
    if (durs) {
        for (unsigned k = 0; k < store->shard_count; ++k) {
            struct ytel_shard *shard = &store->shards[k];
            pthread_mutex_lock(&shard->mu);
            for (size_t i = 0; i < shard->count && count < total; ++i) {
                struct ytelemetry_stored_span *span = ytelemetry_shard_nth(shard, i);
                if (!span->used) continue;
                if (floor && span->start_unix_ns < floor) continue;
                if (service && service[0] && strcmp(span->service, service) != 0) continue;
                if (operation && operation[0] && strcmp(span->name, operation) != 0) continue;
                durs[count++] = span->duration_ns;
            }
            pthread_mutex_unlock(&shard->mu);
        }
        qsort(durs, count, sizeof(uint64_t), cmp_u64);
    }

    json_writer_begin_object(writer);
    json_writer_key(writer, "service"); json_writer_string(writer, service ? service : "");
    json_writer_key(writer, "operation"); json_writer_string(writer, operation ? operation : "");
    json_writer_key(writer, "window_ns"); json_writer_int(writer, (int64_t)window_ns);
    json_writer_key(writer, "count"); json_writer_int(writer, (int64_t)count);
    json_writer_key(writer, "p50_ns"); json_writer_int(writer, (int64_t)pctl_u64(durs, count, 50));
    json_writer_key(writer, "p90_ns"); json_writer_int(writer, (int64_t)pctl_u64(durs, count, 90));
    json_writer_key(writer, "p99_ns"); json_writer_int(writer, (int64_t)pctl_u64(durs, count, 99));
    json_writer_key(writer, "max_ns"); json_writer_int(writer, (int64_t)(count ? durs[count - 1] : 0));
    json_writer_end_object(writer);

    free(durs);
}

void ytelemetry_store_write_errors(struct json_writer *writer, uint64_t since_ns)
{
    struct ytelemetry_store *store = ytelemetry_store_state();
    ytelemetry_store_ensure(store);

    enum { PER_SHARD = 64 };
    struct ytelemetry_stored_span *errs = malloc((size_t)store->shard_count * PER_SHARD * sizeof(*errs));
    size_t nerr = 0;
    if (errs) {
        for (unsigned k = 0; k < store->shard_count; ++k) {
            struct ytel_shard *shard = &store->shards[k];
            pthread_mutex_lock(&shard->mu);
            size_t taken = 0;
            for (size_t i = 0; i < shard->count && taken < PER_SHARD; ++i) {
                struct ytelemetry_stored_span *span = ytelemetry_shard_nth(shard, i); /* newest first */
                if (!span->used || strcmp(span->status, "error") != 0) continue;
                if (since_ns && span->start_unix_ns < since_ns) continue;
                errs[nerr++] = *span;
                taken++;
            }
            pthread_mutex_unlock(&shard->mu);
        }
    }

    json_writer_begin_object(writer);
    json_writer_key(writer, "errors");
    json_writer_begin_array(writer);
    if (errs) {
        for (size_t outer = 0; outer + 1 < nerr; ++outer)
            for (size_t inner = outer + 1; inner < nerr; ++inner)
                if (errs[inner].start_unix_ns > errs[outer].start_unix_ns) {
                    struct ytelemetry_stored_span tmp = errs[outer];
                    errs[outer] = errs[inner];
                    errs[inner] = tmp;
                }
        size_t emitted = 0;
        for (size_t i = 0; i < nerr && emitted < 256; ++i) { emit_span(writer, &errs[i]); emitted++; }
    }
    json_writer_end_array(writer);
    json_writer_end_object(writer);
    free(errs);
}
