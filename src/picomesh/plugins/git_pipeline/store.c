/* git_pipeline — CI job queue + runner-agent job lifecycle.
 *
 *   enqueue(repo_id)                       → job_id
 *   enqueue_job(repo_id, ref, path, t)     → job_id (+ stored descriptor meta)
 *   lease(runner_id)                       → job_id (0 if queue empty)
 *   lease_job(runner_id, labels)           → JSON descriptor or null
 *   job_descriptor(job_id)                 → JSON descriptor or null
 *   append_log(job_id, offset, chunk)      → new log length (owner-gated)
 *   read_log(job_id)                       → log text
 *   complete(job_id, status)               → 1 ok / 0 unknown
 *   complete_job(job_id, status, summary)  → 1 ok / 0 unknown (owner-gated)
 *   requeue_expired()                      → number of timed-out leases requeued
 *   count_pending / count_running / count_done
 *   list / list_all
 *
 * Status: 0=queued, 1=running, 2=succeeded, 3=failed.
 *
 * External runner agents (docs/runner-agent.md) lease via `lease_job`, fetch
 * details via `job_descriptor`, stream output via `append_log`, and report via
 * `complete_job`. The gateway resolves an `rnr_` token to a JWT whose sub is the
 * runner_id, so the runner's identity arrives in yheaders["uid"]; ownership ops
 * (append_log / complete_job) require it to match the job's leased runner.
 *
 * State lives in the shared `sharded_storage` service (context `git_pipeline`),
 * NOT in this process — so multiple objects / gateway workers share one source
 * of truth with nothing cached in memory.
 *
 * Storage layout in the `git_pipeline` context (side keys live outside the
 * "job:" namespace so list("job:") returns only canonical rows):
 *   next_id               → monotonic job-id counter
 *   job:<id>              → "<repo_id>\t<runner_id>\t<status>"
 *   meta:<id>             → JSON {ref, pipeline_path, timeout}
 *   lease_expiry:<id>     → epoch seconds the active lease times out
 *   log:<id>              → accumulated runner log text
 *   summary:<id>          → completion summary
 *   pending/running/done  → live counts by state
 */

#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yclass/yheaders.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yjson/yjson.h>
#include <picomesh/ysecurity/jwt.h>
#include <picomesh/plugin/sharded_storage/sharded_storage.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIPE_CTX "git_pipeline"
#define GP_LEASE_TTL_DEFAULT 300 /* seconds, when a job carries no timeout */

/* No in-memory state — every op delegates to storage. */
struct PICOMESH_CLASS_ANNOTATE("class@git_pipeline:git_pipeline") git_pipeline_git_pipeline_data {
    char _unused;
};

struct gp_storage {
    struct ctx c;
    struct object *obj;
};
PICOMESH_RESULT_DECLARE(gp_storage, struct gp_storage);

static struct gp_storage_result gp_open(void)
{
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return PICOMESH_ERR(gp_storage, "git_pipeline: no active engine");
    struct gp_storage h = {.c = picomesh_engine_service_ctx(e, "sharded_storage")};
    struct object_ptr_result o = sharded_storage_db_create(&h.c);
    if (PICOMESH_IS_ERR(o)) return PICOMESH_ERR(gp_storage, "git_pipeline: storage_db_create failed", o);
    h.obj = o.value;
    return PICOMESH_OK(gp_storage, h);
}

/* Read an int. A real backend error is propagated; an absent/empty key
 * (db_get → "") yields `fallback`. */
static struct picomesh_int64_result gp_get(struct gp_storage *h, struct yheaders *hdrs, const char *key, int64_t fallback)
{
    struct picomesh_string_result r = sharded_storage_db_get(&h->c, h->obj, hdrs, PIPE_CTX, key);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int64, "git_pipeline: storage read failed", r);
    int64_t v = (r.value && r.value[0]) ? strtoll(r.value, NULL, 10) : fallback;
    free(r.value);
    return PICOMESH_OK(picomesh_int64, v);
}

/* Atomic counter / id bump — OK value is the value after the add. A backend
 * failure is propagated, never collapsed into a (valid) job id 0 / 0 count. */
static struct picomesh_int64_result gp_incr(struct gp_storage *h, struct yheaders *hdrs, const char *key, int64_t delta)
{
    struct picomesh_int64_result r = sharded_storage_db_incr(&h->c, h->obj, hdrs, PIPE_CTX, key, delta);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int64, "git_pipeline: counter update failed", r);
    return r;
}

/* Atomic compare-and-set on a job row. OK value: 1 = swapped, 0 = compare
 * mismatch (another caller changed the row first). A backend error is
 * propagated — distinct from a clean mismatch. */
static struct picomesh_int_result gp_cas(struct gp_storage *h, struct yheaders *hdrs, const char *key,
                                         const char *expected, const char *replacement)
{
    struct picomesh_int_result r =
        sharded_storage_db_compare_and_set(&h->c, h->obj, hdrs, PIPE_CTX, key, expected, replacement);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int, "git_pipeline: compare-and-set failed", r);
    return r;
}

/* Load job:<id> → repo_id/runner_id/status. OK value: 1 = present (out
 * params filled), 0 = absent. A backend read failure is propagated. */
static struct picomesh_int_result job_load(struct gp_storage *h, struct yheaders *hdrs, uint32_t job_id,
                                           uint32_t *repo_id, uint32_t *runner_id, int *status)
{
    char k[40];
    snprintf(k, sizeof(k), "job:%u", job_id);
    struct picomesh_string_result r = sharded_storage_db_get(&h->c, h->obj, hdrs, PIPE_CTX, k);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int, "git_pipeline: job read failed", r);
    if (!r.value || !r.value[0]) { free(r.value); return PICOMESH_OK(picomesh_int, 0); }
    char *s = r.value, *t1 = strchr(s, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
    if (!t1 || !t2) { free(s); return PICOMESH_OK(picomesh_int, 0); }
    *t1 = *t2 = 0;
    if (repo_id) *repo_id = (uint32_t)strtoul(s, NULL, 10);
    if (runner_id) *runner_id = (uint32_t)strtoul(t1 + 1, NULL, 10);
    if (status) *status = atoi(t2 + 1);
    free(s);
    return PICOMESH_OK(picomesh_int, 1);
}

/* Write the canonical job row; propagates a failed write. */
static struct picomesh_void_result job_store(struct gp_storage *h, struct yheaders *hdrs, uint32_t job_id,
                                             uint32_t repo_id, uint32_t runner_id, int status)
{
    char k[40], v[48];
    snprintf(k, sizeof(k), "job:%u", job_id);
    snprintf(v, sizeof(v), "%u\t%u\t%d", repo_id, runner_id, status);
    struct picomesh_int_result r = sharded_storage_db_set(&h->c, h->obj, hdrs, PIPE_CTX, k, v);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_void, "git_pipeline: job write failed", r);
    return PICOMESH_OK_VOID();
}

/* ---- side-key (job:<id>:<suffix>) helpers ---- */

/* Side keys live OUTSIDE the "job:" namespace ("<suffix>:<id>", e.g.
 * "meta:42") so the canonical-row scan list("job:") stays clean. */
static void gp_aux_key(char *buf, size_t cap, uint32_t job_id, const char *suffix)
{
    snprintf(buf, cap, "%s:%u", suffix, job_id);
}

/* Read a side key (owned string, possibly empty). */
static struct picomesh_string_result gp_aux_get(struct gp_storage *h, struct yheaders *hdrs,
                                                uint32_t job_id, const char *suffix)
{
    char key[56];
    gp_aux_key(key, sizeof(key), job_id, suffix);
    struct picomesh_string_result r = sharded_storage_db_get(&h->c, h->obj, hdrs, PIPE_CTX, key);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_string, "git_pipeline: side-key read failed", r);
    return r;
}

static struct picomesh_void_result gp_aux_set(struct gp_storage *h, struct yheaders *hdrs,
                                              uint32_t job_id, const char *suffix, const char *value)
{
    char key[56];
    gp_aux_key(key, sizeof(key), job_id, suffix);
    struct picomesh_int_result r = sharded_storage_db_set(&h->c, h->obj, hdrs, PIPE_CTX, key, value);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_void, "git_pipeline: side-key write failed", r);
    return PICOMESH_OK_VOID();
}

/* The job timeout recorded in meta, or the default if none / unreadable. */
static int64_t gp_job_timeout(struct gp_storage *h, struct yheaders *hdrs, uint32_t job_id)
{
    struct picomesh_string_result r = gp_aux_get(h, hdrs, job_id, "meta");
    int64_t timeout = GP_LEASE_TTL_DEFAULT;
    if (PICOMESH_IS_ERR(r)) { picomesh_error_destroy(r.error); return timeout; }
    if (r.value && r.value[0]) {
        struct yjson_doc *doc = yjson_parse(r.value, strlen(r.value));
        if (doc) {
            int64_t recorded = yjson_as_int(yjson_object_get(yjson_doc_root(doc), "timeout"), 0);
            if (recorded > 0) timeout = recorded;
            yjson_doc_free(doc);
        }
    }
    free(r.value);
    return timeout;
}

/* The caller's authenticated runner id, as resolved by the gateway into
 * yheaders["uid"] (a runner JWT carries sub=runner_id). 0 means no gateway
 * context (internal/test) — accepted. Otherwise must match the job's runner. */
static int gp_caller_owns(struct yheaders *hdrs, uint32_t leased_runner)
{
    uint32_t caller = hdrs ? yheaders_get_u32(hdrs, "uid", 0) : 0;
    return caller == 0 || caller == leased_runner;
}

/* Claim the next queued job for runner_id via FIFO CAS, adjusting counters and
 * recording the lease expiry. OK value: job_id (0 if no job is queued). Shared
 * by `lease` (returns the id) and `lease_job` (returns the descriptor). */
static struct picomesh_uint32_result gp_claim_next(struct gp_storage *h, struct yheaders *hdrs, uint32_t runner_id)
{
    /* FIFO: first queued job id wins. Job ids are 1..next_id, so scan that
     * range (lease is not on the throughput hot path — enqueue is). */
    struct picomesh_int64_result lastr = gp_get(h, hdrs, "next_id", 0);
    if (PICOMESH_IS_ERR(lastr)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline: read next_id failed", lastr);
    int64_t last = lastr.value;
    for (uint32_t id = 1; id <= (uint32_t)last; ++id) {
        uint32_t rp = 0; int st = -1;
        struct picomesh_int_result lr = job_load(h, hdrs, id, &rp, NULL, &st);
        if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline: job load failed", lr);
        if (lr.value == 0 || st != 0) continue;  /* absent or not queued */
        /* Claim the job by CAS-ing its row from queued (runner 0, status 0)
         * to running. If two runners race the same queued job, only one CAS
         * swaps; the loser (clean mismatch) falls through to the next id — no
         * double-lease. A backend CAS error propagates. */
        char k[40], expected[48], replacement[48];
        snprintf(k, sizeof(k), "job:%u", id);
        snprintf(expected, sizeof(expected), "%u\t%u\t%d", rp, 0u, 0);
        snprintf(replacement, sizeof(replacement), "%u\t%u\t%d", rp, runner_id, 1);
        struct picomesh_int_result cas = gp_cas(h, hdrs, k, expected, replacement);
        if (PICOMESH_IS_ERR(cas)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline: claim CAS failed", cas);
        if (cas.value == 0) continue;  /* lost the race for this job */
        struct picomesh_int64_result pdec = gp_incr(h, hdrs, "pending", -1);
        if (PICOMESH_IS_ERR(pdec)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline: pending update failed", pdec);
        struct picomesh_int64_result rinc = gp_incr(h, hdrs, "running", 1);
        if (PICOMESH_IS_ERR(rinc)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline: running update failed", rinc);
        /* A lease must have an expiry: if the runner disappears, requeue_expired
         * can return the job to the queue. */
        int64_t expiry = picomesh_security_now() + gp_job_timeout(h, hdrs, id);
        char exp[24];
        snprintf(exp, sizeof(exp), "%lld", (long long)expiry);
        struct picomesh_void_result es = gp_aux_set(h, hdrs, id, "lease_expiry", exp);
        if (PICOMESH_IS_ERR(es)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline: lease expiry write failed", es);
        yinfo("git_pipeline: lease job=%u to runner=%u", id, runner_id);
        return PICOMESH_OK(picomesh_uint32, id);
    }
    return PICOMESH_OK(picomesh_uint32, 0);
}

/* Build the JSON job descriptor for job_id (or "null" if absent). */
static struct picomesh_json_result gp_descriptor_json(struct gp_storage *h, struct yheaders *hdrs, uint32_t job_id)
{
    uint32_t rp = 0, run = 0; int st = -1;
    struct picomesh_int_result lr = job_load(h, hdrs, job_id, &rp, &run, &st);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_json, "git_pipeline: descriptor load failed", lr);
    if (lr.value == 0) { char *n = strdup("null"); return n ? PICOMESH_OK(picomesh_json, n)
                                                            : PICOMESH_ERR(picomesh_json, "git_pipeline: oom"); }

    char ref[256] = {0}, pipeline_path[256] = {0};
    int64_t timeout = GP_LEASE_TTL_DEFAULT;
    struct picomesh_string_result meta = gp_aux_get(h, hdrs, job_id, "meta");
    if (PICOMESH_IS_ERR(meta)) return PICOMESH_ERR(picomesh_json, "git_pipeline: descriptor meta read failed", meta);
    if (meta.value && meta.value[0]) {
        struct yjson_doc *doc = yjson_parse(meta.value, strlen(meta.value));
        if (doc) {
            const struct yjson_value *root = yjson_doc_root(doc);
            snprintf(ref, sizeof(ref), "%s", yjson_as_string(yjson_object_get(root, "ref"), ""));
            snprintf(pipeline_path, sizeof(pipeline_path), "%s",
                     yjson_as_string(yjson_object_get(root, "pipeline_path"), ""));
            int64_t recorded = yjson_as_int(yjson_object_get(root, "timeout"), 0);
            if (recorded > 0) timeout = recorded;
            yjson_doc_free(doc);
        }
    }
    free(meta.value);

    struct picomesh_string_result le = gp_aux_get(h, hdrs, job_id, "lease_expiry");
    if (PICOMESH_IS_ERR(le)) return PICOMESH_ERR(picomesh_json, "git_pipeline: descriptor expiry read failed", le);
    int64_t lease_expiry = (le.value && le.value[0]) ? strtoll(le.value, NULL, 10) : 0;
    free(le.value);

    struct yjson_writer *w = yjson_writer_new();
    if (!w) return PICOMESH_ERR(picomesh_json, "git_pipeline: descriptor writer alloc failed");
    yjson_writer_begin_object(w);
    yjson_writer_key(w, "job_id");        yjson_writer_int(w, (int64_t)job_id);
    yjson_writer_key(w, "repo_id");       yjson_writer_int(w, (int64_t)rp);
    yjson_writer_key(w, "runner_id");     yjson_writer_int(w, (int64_t)run);
    yjson_writer_key(w, "status");        yjson_writer_int(w, st);
    yjson_writer_key(w, "ref");           yjson_writer_string(w, ref);
    yjson_writer_key(w, "pipeline_path"); yjson_writer_string(w, pipeline_path);
    yjson_writer_key(w, "timeout");       yjson_writer_int(w, timeout);
    yjson_writer_key(w, "lease_expiry");  yjson_writer_int(w, lease_expiry);
    yjson_writer_end_object(w);
    size_t len = 0;
    const char *data = yjson_writer_data(w, &len);
    char *out = data ? strdup(data) : NULL;
    yjson_writer_free(w);
    if (!out) return PICOMESH_ERR(picomesh_json, "git_pipeline: descriptor encode failed");
    return PICOMESH_OK(picomesh_json, out);
}

/* Transition job_id from queued/running to a terminal status (2 succeeded,
 * 3 failed), adjusting counters exactly once. OK value: 1 = transitioned,
 * 0 = unknown / already final / concurrent change. Shared by complete +
 * complete_job. */
static struct picomesh_int_result gp_transition(struct gp_storage *h, struct yheaders *hdrs,
                                                uint32_t job_id, int32_t status)
{
    uint32_t rp = 0, run = 0; int st = -1;
    struct picomesh_int_result lr = job_load(h, hdrs, job_id, &rp, &run, &st);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_int, "git_pipeline: complete load failed", lr);
    if (lr.value == 0) return PICOMESH_OK(picomesh_int, 0);          /* unknown job */
    if (st == 2 || st == 3) return PICOMESH_OK(picomesh_int, 0);     /* already final */
    int final_status = status == 0 ? 2 : 3;
    /* Transition the row atomically; only the caller that swaps adjusts the
     * counters, so a double-complete can't double-count. A clean CAS mismatch
     * (value 0) is a concurrent change; a backend error propagates. */
    char k[40], expected[48], replacement[48];
    snprintf(k, sizeof(k), "job:%u", job_id);
    snprintf(expected, sizeof(expected), "%u\t%u\t%d", rp, run, st);
    snprintf(replacement, sizeof(replacement), "%u\t%u\t%d", rp, run, final_status);
    struct picomesh_int_result cas = gp_cas(h, hdrs, k, expected, replacement);
    if (PICOMESH_IS_ERR(cas)) return PICOMESH_ERR(picomesh_int, "git_pipeline: transition CAS failed", cas);
    if (cas.value == 0) return PICOMESH_OK(picomesh_int, 0);
    if (st == 0) {
        struct picomesh_int64_result d = gp_incr(h, hdrs, "pending", -1);
        if (PICOMESH_IS_ERR(d)) return PICOMESH_ERR(picomesh_int, "git_pipeline: pending update failed", d);
    } else if (st == 1) {
        struct picomesh_int64_result d = gp_incr(h, hdrs, "running", -1);
        if (PICOMESH_IS_ERR(d)) return PICOMESH_ERR(picomesh_int, "git_pipeline: running update failed", d);
    }
    struct picomesh_int64_result dn = gp_incr(h, hdrs, "done", 1);
    if (PICOMESH_IS_ERR(dn)) return PICOMESH_ERR(picomesh_int, "git_pipeline: done update failed", dn);
    return PICOMESH_OK(picomesh_int, 1);
}

PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_enqueue")
struct picomesh_uint32_result git_pipeline_git_pipeline_enqueue_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                           uint32_t repo_id)
{
    (void)ctx; (void)obj;
    struct gp_storage_result sr = gp_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue: storage open failed", sr);
    struct gp_storage h = sr.value;
    struct picomesh_int64_result idr = gp_incr(&h, hdrs, "next_id", 1);
    if (PICOMESH_IS_ERR(idr)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue: allocate id failed", idr);
    uint32_t id = (uint32_t)idr.value;
    struct picomesh_void_result ws = job_store(&h, hdrs, id, repo_id, 0, 0);
    if (PICOMESH_IS_ERR(ws)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue: persist job failed", ws);
    struct picomesh_int64_result pinc = gp_incr(&h, hdrs, "pending", 1);
    if (PICOMESH_IS_ERR(pinc)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue: bump pending failed", pinc);
    return PICOMESH_OK(picomesh_uint32, id);
}

/* Enqueue with the execution metadata a runner needs to run the job: the ref to
 * build, the pipeline definition path, and a per-job timeout (seconds). */
PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_enqueue_job")
struct picomesh_uint32_result git_pipeline_git_pipeline_enqueue_job_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                           uint32_t repo_id, const char *ref,
                                                           const char *pipeline_path, int64_t timeout_seconds)
{
    (void)ctx; (void)obj;
    struct gp_storage_result sr = gp_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue_job: storage open failed", sr);
    struct gp_storage h = sr.value;
    struct picomesh_int64_result idr = gp_incr(&h, hdrs, "next_id", 1);
    if (PICOMESH_IS_ERR(idr)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue_job: allocate id failed", idr);
    uint32_t id = (uint32_t)idr.value;
    struct picomesh_void_result ws = job_store(&h, hdrs, id, repo_id, 0, 0);
    if (PICOMESH_IS_ERR(ws)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue_job: persist job failed", ws);

    struct yjson_writer *w = yjson_writer_new();
    if (!w) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue_job: writer alloc failed");
    yjson_writer_begin_object(w);
    yjson_writer_key(w, "ref");           yjson_writer_string(w, ref ? ref : "");
    yjson_writer_key(w, "pipeline_path"); yjson_writer_string(w, pipeline_path ? pipeline_path : "");
    yjson_writer_key(w, "timeout");       yjson_writer_int(w, timeout_seconds > 0 ? timeout_seconds : GP_LEASE_TTL_DEFAULT);
    yjson_writer_end_object(w);
    size_t mlen = 0;
    const char *mdata = yjson_writer_data(w, &mlen);
    char *meta = mdata ? strdup(mdata) : NULL;
    yjson_writer_free(w);
    if (!meta) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue_job: meta encode failed");
    struct picomesh_void_result ms = gp_aux_set(&h, hdrs, id, "meta", meta);
    free(meta);
    if (PICOMESH_IS_ERR(ms)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue_job: meta write failed", ms);

    struct picomesh_int64_result pinc = gp_incr(&h, hdrs, "pending", 1);
    if (PICOMESH_IS_ERR(pinc)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue_job: bump pending failed", pinc);
    return PICOMESH_OK(picomesh_uint32, id);
}

PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_lease")
struct picomesh_uint32_result git_pipeline_git_pipeline_lease_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         uint32_t runner_id)
{
    (void)ctx; (void)obj;
    struct gp_storage_result sr = gp_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_lease: storage open failed", sr);
    struct gp_storage h = sr.value;
    return gp_claim_next(&h, hdrs, runner_id);
}

/* The runner-agent lease entry point: claims the next job and returns the full
 * descriptor (or JSON null when the queue is empty). `labels` is reserved for
 * capability matching, deferred per the MVP. */
PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_lease_job")
struct picomesh_json_result git_pipeline_git_pipeline_lease_job_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         uint32_t runner_id, const char *labels)
{
    (void)ctx; (void)obj; (void)labels;
    /* The caller's runner token must match the runner_id it leases as, so one
     * runner cannot claim work under another runner's identity. */
    if (!gp_caller_owns(hdrs, runner_id))
        return PICOMESH_ERR(picomesh_json, "git_pipeline_lease_job: caller is not this runner");
    struct gp_storage_result sr = gp_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_json, "git_pipeline_lease_job: storage open failed", sr);
    struct gp_storage h = sr.value;
    struct picomesh_uint32_result claim = gp_claim_next(&h, hdrs, runner_id);
    if (PICOMESH_IS_ERR(claim)) return PICOMESH_ERR(picomesh_json, "git_pipeline_lease_job: claim failed", claim);
    if (claim.value == 0) { char *n = strdup("null"); return n ? PICOMESH_OK(picomesh_json, n)
                                                               : PICOMESH_ERR(picomesh_json, "git_pipeline_lease_job: oom"); }
    return gp_descriptor_json(&h, hdrs, claim.value);
}

PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_job_descriptor")
struct picomesh_json_result git_pipeline_git_pipeline_job_descriptor_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         uint32_t job_id)
{
    (void)ctx; (void)obj;
    struct gp_storage_result sr = gp_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_json, "git_pipeline_job_descriptor: storage open failed", sr);
    struct gp_storage h = sr.value;
    return gp_descriptor_json(&h, hdrs, job_id);
}

/* Append a log chunk for a job the caller currently holds the lease on. The
 * server rejects writes from a runner that does not own the active lease. The
 * `offset` is advisory in the MVP — the chunk is appended to whatever is
 * stored. OK value: the new total log length. */
PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_append_log")
struct picomesh_int64_result git_pipeline_git_pipeline_append_log_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         uint32_t job_id, int64_t offset, const char *chunk)
{
    (void)ctx; (void)obj; (void)offset;
    struct gp_storage_result sr = gp_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_int64, "git_pipeline_append_log: storage open failed", sr);
    struct gp_storage h = sr.value;
    uint32_t run = 0; int st = -1;
    struct picomesh_int_result lr = job_load(&h, hdrs, job_id, NULL, &run, &st);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_int64, "git_pipeline_append_log: load failed", lr);
    if (lr.value == 0) return PICOMESH_ERR(picomesh_int64, "git_pipeline_append_log: unknown job");
    if (!gp_caller_owns(hdrs, run)) return PICOMESH_ERR(picomesh_int64, "git_pipeline_append_log: caller does not own this lease");

    struct picomesh_string_result existing = gp_aux_get(&h, hdrs, job_id, "log");
    if (PICOMESH_IS_ERR(existing)) return PICOMESH_ERR(picomesh_int64, "git_pipeline_append_log: log read failed", existing);
    size_t old_len = existing.value ? strlen(existing.value) : 0;
    size_t add_len = chunk ? strlen(chunk) : 0;
    char *combined = malloc(old_len + add_len + 1);
    if (!combined) { free(existing.value); return PICOMESH_ERR(picomesh_int64, "git_pipeline_append_log: oom"); }
    if (old_len) memcpy(combined, existing.value, old_len);
    if (add_len) memcpy(combined + old_len, chunk, add_len);
    combined[old_len + add_len] = 0;
    free(existing.value);
    struct picomesh_void_result ws = gp_aux_set(&h, hdrs, job_id, "log", combined);
    free(combined);
    if (PICOMESH_IS_ERR(ws)) return PICOMESH_ERR(picomesh_int64, "git_pipeline_append_log: log write failed", ws);
    return PICOMESH_OK(picomesh_int64, (int64_t)(old_len + add_len));
}

PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_read_log")
struct picomesh_string_result git_pipeline_git_pipeline_read_log_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         uint32_t job_id)
{
    (void)ctx; (void)obj;
    struct gp_storage_result sr = gp_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_string, "git_pipeline_read_log: storage open failed", sr);
    struct gp_storage h = sr.value;
    struct picomesh_string_result r = gp_aux_get(&h, hdrs, job_id, "log");
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_string, "git_pipeline_read_log: read failed", r);
    if (!r.value) { char *empty = strdup(""); return empty ? PICOMESH_OK(picomesh_string, empty)
                                                           : PICOMESH_ERR(picomesh_string, "git_pipeline_read_log: oom"); }
    return r;
}

PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_complete")
struct picomesh_int_result git_pipeline_git_pipeline_complete_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         uint32_t job_id, int32_t status)
{
    (void)ctx; (void)obj;
    struct gp_storage_result sr = gp_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_int, "git_pipeline_complete: storage open failed", sr);
    struct gp_storage h = sr.value;
    return gp_transition(&h, hdrs, job_id, status);
}

/* Runner-reported completion: store the summary, transition the job, and clear
 * the lease expiry. Owner-gated — only the runner holding the lease may report. */
PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_complete_job")
struct picomesh_int_result git_pipeline_git_pipeline_complete_job_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         uint32_t job_id, int32_t status, const char *summary)
{
    (void)ctx; (void)obj;
    struct gp_storage_result sr = gp_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_int, "git_pipeline_complete_job: storage open failed", sr);
    struct gp_storage h = sr.value;
    uint32_t run = 0; int st = -1;
    struct picomesh_int_result lr = job_load(&h, hdrs, job_id, NULL, &run, &st);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_int, "git_pipeline_complete_job: load failed", lr);
    if (lr.value == 0) return PICOMESH_OK(picomesh_int, 0);
    if (!gp_caller_owns(hdrs, run)) return PICOMESH_ERR(picomesh_int, "git_pipeline_complete_job: caller does not own this lease");

    struct picomesh_void_result ss = gp_aux_set(&h, hdrs, job_id, "summary", summary ? summary : "");
    if (PICOMESH_IS_ERR(ss)) return PICOMESH_ERR(picomesh_int, "git_pipeline_complete_job: summary write failed", ss);
    struct picomesh_int_result tr = gp_transition(&h, hdrs, job_id, status);
    if (PICOMESH_IS_ERR(tr)) return PICOMESH_ERR(picomesh_int, "git_pipeline_complete_job: transition failed", tr);
    if (tr.value == 1) {
        char key[56];
        gp_aux_key(key, sizeof(key), job_id, "lease_expiry");
        struct picomesh_int_result del = sharded_storage_db_del(&h.c, h.obj, hdrs, PIPE_CTX, key);
        if (PICOMESH_IS_ERR(del)) return PICOMESH_ERR(picomesh_int, "git_pipeline_complete_job: clear lease failed", del);
    }
    return tr;
}

/* Sweep running jobs whose lease has expired (the runner disappeared) back to
 * queued so another runner can pick them up. OK value: number requeued. */
PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_requeue_expired")
struct picomesh_size_result git_pipeline_git_pipeline_requeue_expired_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct gp_storage_result sr = gp_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_size, "git_pipeline_requeue_expired: storage open failed", sr);
    struct gp_storage h = sr.value;
    struct picomesh_int64_result lastr = gp_get(&h, hdrs, "next_id", 0);
    if (PICOMESH_IS_ERR(lastr)) return PICOMESH_ERR(picomesh_size, "git_pipeline_requeue_expired: read next_id failed", lastr);
    int64_t last = lastr.value, now = picomesh_security_now();
    size_t requeued = 0;
    for (uint32_t id = 1; id <= (uint32_t)last; ++id) {
        uint32_t rp = 0, run = 0; int st = -1;
        struct picomesh_int_result lr = job_load(&h, hdrs, id, &rp, &run, &st);
        if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_size, "git_pipeline_requeue_expired: load failed", lr);
        if (lr.value == 0 || st != 1) continue;  /* only running jobs can expire */
        struct picomesh_string_result le = gp_aux_get(&h, hdrs, id, "lease_expiry");
        if (PICOMESH_IS_ERR(le)) return PICOMESH_ERR(picomesh_size, "git_pipeline_requeue_expired: expiry read failed", le);
        int64_t expiry = (le.value && le.value[0]) ? strtoll(le.value, NULL, 10) : 0;
        free(le.value);
        if (expiry == 0 || now < expiry) continue;  /* no expiry recorded or not yet due */
        /* CAS running → queued (runner 0). A clean mismatch means the job
         * changed underneath us (completed/relased) — skip it. */
        char k[40], expected[48], replacement[48];
        snprintf(k, sizeof(k), "job:%u", id);
        snprintf(expected, sizeof(expected), "%u\t%u\t%d", rp, run, 1);
        snprintf(replacement, sizeof(replacement), "%u\t%u\t%d", rp, 0u, 0);
        struct picomesh_int_result cas = gp_cas(&h, hdrs, k, expected, replacement);
        if (PICOMESH_IS_ERR(cas)) return PICOMESH_ERR(picomesh_size, "git_pipeline_requeue_expired: CAS failed", cas);
        if (cas.value == 0) continue;
        char exp_key[56];
        gp_aux_key(exp_key, sizeof(exp_key), id, "lease_expiry");
        struct picomesh_int_result del = sharded_storage_db_del(&h.c, h.obj, hdrs, PIPE_CTX, exp_key);
        if (PICOMESH_IS_ERR(del)) return PICOMESH_ERR(picomesh_size, "git_pipeline_requeue_expired: clear expiry failed", del);
        struct picomesh_int64_result rdec = gp_incr(&h, hdrs, "running", -1);
        if (PICOMESH_IS_ERR(rdec)) return PICOMESH_ERR(picomesh_size, "git_pipeline_requeue_expired: running update failed", rdec);
        struct picomesh_int64_result pinc = gp_incr(&h, hdrs, "pending", 1);
        if (PICOMESH_IS_ERR(pinc)) return PICOMESH_ERR(picomesh_size, "git_pipeline_requeue_expired: pending update failed", pinc);
        yinfo("git_pipeline: requeued expired job=%u (was runner=%u)", id, run);
        ++requeued;
    }
    return PICOMESH_OK(picomesh_size, requeued);
}

PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_count_pending")
struct picomesh_size_result git_pipeline_git_pipeline_count_pending_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct gp_storage_result sr = gp_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_size, "git_pipeline_count_pending: storage open failed", sr);
    struct gp_storage h = sr.value;
    struct picomesh_int64_result nr = gp_get(&h, hdrs, "pending", 0);
    if (PICOMESH_IS_ERR(nr)) return PICOMESH_ERR(picomesh_size, "git_pipeline_count_pending: read failed", nr);
    return PICOMESH_OK(picomesh_size, (size_t)(nr.value < 0 ? 0 : nr.value));
}

PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_count_running")
struct picomesh_size_result git_pipeline_git_pipeline_count_running_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct gp_storage_result sr = gp_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_size, "git_pipeline_count_running: storage open failed", sr);
    struct gp_storage h = sr.value;
    struct picomesh_int64_result nr = gp_get(&h, hdrs, "running", 0);
    if (PICOMESH_IS_ERR(nr)) return PICOMESH_ERR(picomesh_size, "git_pipeline_count_running: read failed", nr);
    return PICOMESH_OK(picomesh_size, (size_t)(nr.value < 0 ? 0 : nr.value));
}

PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_count_done")
struct picomesh_size_result git_pipeline_git_pipeline_count_done_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct gp_storage_result sr = gp_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_size, "git_pipeline_count_done: storage open failed", sr);
    struct gp_storage h = sr.value;
    struct picomesh_int64_result nr = gp_get(&h, hdrs, "done", 0);
    if (PICOMESH_IS_ERR(nr)) return PICOMESH_ERR(picomesh_size, "git_pipeline_count_done: read failed", nr);
    return PICOMESH_OK(picomesh_size, (size_t)(nr.value < 0 ? 0 : nr.value));
}

/* List ALL pipeline runs' stored entries as a JSON array (gh#15) — every
 * object, not a pending/running/done count. Delegates to the namespace scan. */
PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_list")
struct picomesh_json_result git_pipeline_git_pipeline_list_impl(struct ctx *ctx, struct object *obj,
                                                         struct yheaders *hdrs,
                                                         int64_t offset, int64_t limit)
{
    (void)ctx; (void)obj;
    struct gp_storage_result sr = gp_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_json, "git_pipeline_list: storage open failed", sr);
    struct gp_storage h = sr.value;
    return sharded_storage_db_list(&h.c, h.obj, hdrs, PIPE_CTX, "job:", offset, limit);
}

/* Unbounded variant — every run. Use with care on large deployments. */
PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_list_all")
struct picomesh_json_result git_pipeline_git_pipeline_list_all_impl(struct ctx *ctx, struct object *obj,
                                                                    struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct gp_storage_result sr = gp_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_json, "git_pipeline_list_all: storage open failed", sr);
    struct gp_storage h = sr.value;
    return sharded_storage_db_list_all(&h.c, h.obj, hdrs, PIPE_CTX, "job:");
}

#include "store.gen.c"
