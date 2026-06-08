/* git_pipeline — CI job queue + runner-agent job lifecycle (relational, #18).
 *
 *   enqueue(repo_id)                       → job_id
 *   enqueue_job(repo_id, ref, path, t)     → job_id (+ descriptor meta)
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
 * State is RELATIONAL ROWS in the `rstore_uid` cluster's `pipeline_runs` table
 * (id, repo_id, runner_id, status, ref, pipeline_path, timeout, lease_expiry,
 * log, summary, …), provisioned by that cluster's schema. This replaces the
 * previous `sharded_storage` KV layout — the hand-kept pending/running/done
 * counters are now `COUNT(… status=…)` so they can't drift from the rows; the
 * FIFO lease and the queued→running / *→terminal transitions are single atomic
 * `UPDATE … WHERE status=…` statements instead of read-modify-write CAS. Like
 * the other migrated services the table lives on the cluster's global shard. */

#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yclass/yheaders.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yjson/yjson.h>
#include <picomesh/ysecurity/jwt.h>
#include <picomesh/ysecurity/secret.h>
#include <picomesh/plugin/relational_storage/relational_sql.h>
#include <picomesh/plugin/git_repo/git_repo.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIPE_STORE "rstore_uid" /* the uid-sharded data cluster */
#define PIPE_DB    "uid"        /* logical database within it */
#define GP_LEASE_TTL_DEFAULT 300 /* seconds, when a job carries no timeout */

/* No in-memory state — every op delegates to relational storage. */
struct PICOMESH_CLASS_ANNOTATE("class@git_pipeline:git_pipeline") git_pipeline_git_pipeline_data {
    char _unused;
};

/* Open a handle onto the pipeline_runs table on the cluster's global shard. */
static struct picomesh_void_result gp_open(struct rel_handle *handle)
{
    struct picomesh_void_result open_res = rel_open(handle, PIPE_STORE, PIPE_DB);
    if (PICOMESH_IS_ERR(open_res))
        return PICOMESH_ERR(picomesh_void, "git_pipeline: open rstore_uid failed", open_res);
    handle->shard = REL_SHARD_GLOBAL;
    return PICOMESH_OK_VOID();
}

/* Run a write and return its `changes`/`last_insert_rowid` field; a backend
 * error is propagated. */
static struct picomesh_int64_result gp_exec_field(struct rel_handle *handle, struct yheaders *hdrs,
                                                  const char *sql, const char *args, const char *field)
{
    struct picomesh_json_result exec_res = rel_exec(handle, hdrs, sql, args ? args : "[]");
    if (PICOMESH_IS_ERR(exec_res)) return PICOMESH_ERR(picomesh_int64, "git_pipeline: exec failed", exec_res);
    struct yjson_doc *doc = yjson_parse(exec_res.value ? exec_res.value : "{}",
                                        exec_res.value ? strlen(exec_res.value) : 2);
    int64_t value = doc ? yjson_as_int(yjson_object_get(yjson_doc_root(doc), field), 0) : 0;
    if (doc) yjson_doc_free(doc);
    free(exec_res.value);
    return PICOMESH_OK(picomesh_int64, value);
}

/* Load a job row's repo_id/runner_id/status. OK value: 1 = present (out params
 * filled), 0 = absent. A backend read failure is propagated. */
static struct picomesh_int_result job_load(struct rel_handle *handle, struct yheaders *hdrs, uint32_t job_id,
                                           uint32_t *repo_id, uint32_t *runner_id, int *status)
{
    char *args = rel_args1i((int64_t)job_id);
    struct picomesh_json_result row_res =
        rel_query(handle, hdrs, "SELECT repo_id, runner_id, status FROM pipeline_runs WHERE id=?", args);
    free(args);
    if (PICOMESH_IS_ERR(row_res)) return PICOMESH_ERR(picomesh_int, "git_pipeline: job read failed", row_res);
    struct yjson_doc *doc = yjson_parse(row_res.value ? row_res.value : "[]",
                                        row_res.value ? strlen(row_res.value) : 2);
    free(row_res.value);
    const struct yjson_value *array = doc ? yjson_doc_root(doc) : NULL;
    if (!array || !yjson_is_array(array) || yjson_array_size(array) == 0) {
        if (doc) yjson_doc_free(doc);
        return PICOMESH_OK(picomesh_int, 0);
    }
    const struct yjson_value *row = yjson_array_at(array, 0);
    if (repo_id) *repo_id = (uint32_t)yjson_as_int(yjson_object_get(row, "repo_id"), 0);
    if (runner_id) *runner_id = (uint32_t)yjson_as_int(yjson_object_get(row, "runner_id"), 0);
    if (status) *status = (int)yjson_as_int(yjson_object_get(row, "status"), -1);
    yjson_doc_free(doc);
    return PICOMESH_OK(picomesh_int, 1);
}

/* Does the caller own this job's lease? Identity comes ONLY from the verified
 * runner JWT (its sub = runner_id), never the bare yheaders["uid"], and never an
 * unauthenticated request. NO `caller == 0` bypass. */
static int gp_caller_owns(struct yheaders *hdrs, uint32_t leased_runner)
{
    struct picomesh_authctx caller;
    picomesh_authctx_from_headers(hdrs, picomesh_active_engine(), &caller);
    if (!caller.authenticated || caller.uid == 0) return 0;
    return caller.uid == leased_runner;
}

/* Site admin / internal capability — gates the global job scans. Fail closed. */
static int gp_caller_site_admin(struct yheaders *hdrs)
{
    struct picomesh_authctx caller;
    picomesh_authctx_from_headers(hdrs, picomesh_active_engine(), &caller);
    if (!caller.authenticated) return 0;
    if (picomesh_groups_contains(caller.groups_csv, PICOMESH_GROUP_SYSTEM)) return 1;
    return picomesh_groups_max_role(caller.groups_csv, "site") >= picomesh_role_rank("maintainer");
}

/* The repo's namespace path, or NULL on error/absent (caller frees). */
static char *gp_repo_namespace(uint32_t repo_id)
{
    struct picomesh_engine *engine = picomesh_active_engine();
    if (!engine) return NULL;
    struct ctx repo_ctx = picomesh_engine_service_ctx(engine, "git_repo");
    struct object_ptr_result create_res = git_repo_git_repo_create(&repo_ctx);
    if (PICOMESH_IS_ERR(create_res)) { picomesh_error_destroy(create_res.error); return NULL; }
    struct picomesh_string_result namespace_res =
        git_repo_git_repo_namespace_of(&repo_ctx, create_res.value, NULL, repo_id);
    if (PICOMESH_IS_ERR(namespace_res)) { picomesh_error_destroy(namespace_res.error); return NULL; }
    return namespace_res.value; /* may be NULL/"" */
}

/* Read access to a job's metadata/log (issue #30): the runner that leased it, OR
 * a verified caller with reporter+ on the repo's namespace (inherited), OR a site
 * admin. Anonymous/unknown job → denied. */
static int gp_job_readable(struct rel_handle *handle, struct yheaders *hdrs, uint32_t job_id)
{
    uint32_t repo_id = 0, leased_runner = 0; int status = -1;
    struct picomesh_int_result load_res = job_load(handle, hdrs, job_id, &repo_id, &leased_runner, &status);
    if (PICOMESH_IS_ERR(load_res)) { picomesh_error_destroy(load_res.error); return 0; }
    if (load_res.value == 0) return 0; /* no such job */
    if (leased_runner != 0 && gp_caller_owns(hdrs, leased_runner)) return 1;

    struct picomesh_authctx caller;
    picomesh_authctx_from_headers(hdrs, picomesh_active_engine(), &caller);
    if (!caller.authenticated) return 0;
    if (picomesh_groups_contains(caller.groups_csv, PICOMESH_GROUP_SYSTEM)) return 1; /* trusted internal */
    if (picomesh_groups_max_role(caller.groups_csv, "site") >= picomesh_role_rank("maintainer")) return 1;

    char *namespace_path = gp_repo_namespace(repo_id);
    int allowed = namespace_path && namespace_path[0] &&
                  picomesh_groups_effective_role(caller.groups_csv, namespace_path) >=
                      picomesh_role_rank("reporter");
    free(namespace_path);
    return allowed;
}

/* Write authz for a repo-scoped pipeline op (issue #30): a VERIFIED principal
 * with `min_role`+ on the repo's namespace (inherited) or a site admin. A
 * NULL/anonymous caller is the trusted in-process path. Returns 1 if allowed. */
static int gp_caller_can_write(struct yheaders *hdrs, uint32_t repo_id, const char *min_role)
{
    struct picomesh_authctx caller;
    picomesh_authctx_from_headers(hdrs, picomesh_active_engine(), &caller);
    if (!caller.authenticated) return 0; /* fail closed */
    if (picomesh_groups_contains(caller.groups_csv, PICOMESH_GROUP_SYSTEM)) return 1; /* trusted internal */
    if (picomesh_groups_max_role(caller.groups_csv, "site") >= picomesh_role_rank("maintainer")) return 1;
    char *namespace_path = gp_repo_namespace(repo_id);
    int allowed = namespace_path && namespace_path[0] &&
                  picomesh_groups_effective_role(caller.groups_csv, namespace_path) >=
                      picomesh_role_rank(min_role);
    free(namespace_path);
    return allowed;
}

/* Claim the next queued job for runner_id via atomic FIFO UPDATE. OK value:
 * job_id (0 if no job is queued). Shared by `lease` and `lease_job`. */
static struct picomesh_uint32_result gp_claim_next(struct rel_handle *handle, struct yheaders *hdrs, uint32_t runner_id)
{
    for (int attempt = 0; attempt < 64; ++attempt) {
        struct picomesh_int64_result candidate_res = rel_query_int_result(handle, hdrs,
            "SELECT id FROM pipeline_runs WHERE status=0 ORDER BY id LIMIT 1", "[]", "id", 0);
        if (PICOMESH_IS_ERR(candidate_res)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline: scan queued failed", candidate_res);
        if (candidate_res.value == 0) return PICOMESH_OK(picomesh_uint32, 0); /* empty queue */

        /* Atomic claim: only the UPDATE that flips status 0→1 wins; a racing
         * runner gets changes==0 and retries the next candidate. lease_expiry =
         * now + the job's own timeout column. */
        struct yjson_writer *args_writer = yjson_writer_new();
        if (!args_writer) return PICOMESH_ERR(picomesh_uint32, "git_pipeline: oom");
        yjson_writer_begin_array(args_writer);
        yjson_writer_int(args_writer, (int64_t)runner_id);
        yjson_writer_int(args_writer, picomesh_security_now());
        yjson_writer_int(args_writer, candidate_res.value);
        char *args = rel_args_take(args_writer);
        struct picomesh_int64_result changes_res = gp_exec_field(handle, hdrs,
            "UPDATE pipeline_runs SET status=1, runner_id=?, lease_expiry=?+timeout "
            "WHERE id=? AND status=0", args, "changes");
        free(args);
        if (PICOMESH_IS_ERR(changes_res)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline: claim failed", changes_res);
        if (changes_res.value == 1) {
            yinfo("git_pipeline: lease job=%lld to runner=%u", (long long)candidate_res.value, runner_id);
            return PICOMESH_OK(picomesh_uint32, (uint32_t)candidate_res.value);
        }
        /* lost the race — try again */
    }
    return PICOMESH_OK(picomesh_uint32, 0); /* heavy contention: report empty, runner retries */
}

/* Build the JSON job descriptor for job_id (or "null" if absent). */
static struct picomesh_json_result gp_descriptor_json(struct rel_handle *handle, struct yheaders *hdrs, uint32_t job_id)
{
    char *args = rel_args1i((int64_t)job_id);
    struct picomesh_json_result row_res = rel_query(handle, hdrs,
        "SELECT id, repo_id, runner_id, status, ref, pipeline_path, timeout, lease_expiry "
        "FROM pipeline_runs WHERE id=?", args);
    free(args);
    if (PICOMESH_IS_ERR(row_res)) return PICOMESH_ERR(picomesh_json, "git_pipeline: descriptor load failed", row_res);
    struct yjson_doc *doc = yjson_parse(row_res.value ? row_res.value : "[]",
                                        row_res.value ? strlen(row_res.value) : 2);
    free(row_res.value);
    const struct yjson_value *array = doc ? yjson_doc_root(doc) : NULL;
    if (!array || !yjson_is_array(array) || yjson_array_size(array) == 0) {
        if (doc) yjson_doc_free(doc);
        char *null_str = strdup("null");
        return null_str ? PICOMESH_OK(picomesh_json, null_str) : PICOMESH_ERR(picomesh_json, "git_pipeline: oom");
    }
    const struct yjson_value *row = yjson_array_at(array, 0);
    struct yjson_writer *writer = yjson_writer_new();
    if (!writer) { yjson_doc_free(doc); return PICOMESH_ERR(picomesh_json, "git_pipeline: descriptor writer alloc failed"); }
    yjson_writer_begin_object(writer);
    yjson_writer_key(writer, "job_id");        yjson_writer_int(writer, (int64_t)job_id);
    yjson_writer_key(writer, "repo_id");       yjson_writer_int(writer, yjson_as_int(yjson_object_get(row, "repo_id"), 0));
    yjson_writer_key(writer, "runner_id");     yjson_writer_int(writer, yjson_as_int(yjson_object_get(row, "runner_id"), 0));
    yjson_writer_key(writer, "status");        yjson_writer_int(writer, yjson_as_int(yjson_object_get(row, "status"), -1));
    yjson_writer_key(writer, "ref");           yjson_writer_string(writer, yjson_as_string(yjson_object_get(row, "ref"), ""));
    yjson_writer_key(writer, "pipeline_path"); yjson_writer_string(writer, yjson_as_string(yjson_object_get(row, "pipeline_path"), ""));
    yjson_writer_key(writer, "timeout");       yjson_writer_int(writer, yjson_as_int(yjson_object_get(row, "timeout"), GP_LEASE_TTL_DEFAULT));
    yjson_writer_key(writer, "lease_expiry");  yjson_writer_int(writer, yjson_as_int(yjson_object_get(row, "lease_expiry"), 0));
    yjson_writer_end_object(writer);
    yjson_doc_free(doc);
    size_t len = 0;
    const char *data = yjson_writer_data(writer, &len);
    char *out = data ? strdup(data) : NULL;
    yjson_writer_free(writer);
    if (!out) return PICOMESH_ERR(picomesh_json, "git_pipeline: descriptor encode failed");
    return PICOMESH_OK(picomesh_json, out);
}

/* Transition job_id from queued/running to a terminal status (2 succeeded,
 * 3 failed). OK value: 1 = transitioned, 0 = unknown / already final /
 * concurrent change. The single UPDATE … WHERE status IN (0,1) is atomic, so a
 * double-complete affects 0 rows the second time. */
static struct picomesh_int_result gp_transition(struct rel_handle *handle, struct yheaders *hdrs,
                                                uint32_t job_id, int32_t status)
{
    int final_status = status == 0 ? 2 : 3;
    struct yjson_writer *args_writer = yjson_writer_new();
    if (!args_writer) return PICOMESH_ERR(picomesh_int, "git_pipeline: oom");
    yjson_writer_begin_array(args_writer);
    yjson_writer_int(args_writer, final_status);
    yjson_writer_int(args_writer, (int64_t)job_id);
    char *args = rel_args_take(args_writer);
    struct picomesh_int64_result changes_res = gp_exec_field(handle, hdrs,
        "UPDATE pipeline_runs SET status=? WHERE id=? AND status IN (0,1)", args, "changes");
    free(args);
    if (PICOMESH_IS_ERR(changes_res)) return PICOMESH_ERR(picomesh_int, "git_pipeline: transition failed", changes_res);
    return PICOMESH_OK(picomesh_int, changes_res.value > 0 ? 1 : 0);
}

PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_enqueue")
struct picomesh_uint32_result git_pipeline_git_pipeline_enqueue_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                           uint32_t repo_id)
{
    (void)ctx; (void)obj;
    if (!gp_caller_can_write(hdrs, repo_id, "developer"))
        return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue: forbidden (insufficient namespace role)");
    struct rel_handle handle;
    struct picomesh_void_result open_res = gp_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue: storage open failed", open_res);

    struct yjson_writer *args_writer = yjson_writer_new();
    if (!args_writer) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue: oom");
    yjson_writer_begin_array(args_writer);
    yjson_writer_int(args_writer, (int64_t)repo_id);
    yjson_writer_int(args_writer, picomesh_security_now());
    char *args = rel_args_take(args_writer);
    struct picomesh_int64_result id_res = gp_exec_field(&handle, hdrs,
        "INSERT INTO pipeline_runs(repo_id, status, created_at) VALUES(?, 0, ?)", args, "last_insert_rowid");
    free(args);
    if (PICOMESH_IS_ERR(id_res)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue: persist job failed", id_res);
    if (id_res.value <= 0) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue: no insert id");
    return PICOMESH_OK(picomesh_uint32, (uint32_t)id_res.value);
}

/* Enqueue with the execution metadata a runner needs: the ref to build, the
 * pipeline definition path, and a per-job timeout (seconds). */
PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_enqueue_job")
struct picomesh_uint32_result git_pipeline_git_pipeline_enqueue_job_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                           uint32_t repo_id, const char *ref,
                                                           const char *pipeline_path, int64_t timeout_seconds)
{
    (void)ctx; (void)obj;
    if (!gp_caller_can_write(hdrs, repo_id, "developer"))
        return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue_job: forbidden (insufficient namespace role)");
    struct rel_handle handle;
    struct picomesh_void_result open_res = gp_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue_job: storage open failed", open_res);

    struct yjson_writer *args_writer = yjson_writer_new();
    if (!args_writer) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue_job: oom");
    yjson_writer_begin_array(args_writer);
    yjson_writer_int(args_writer, (int64_t)repo_id);
    yjson_writer_string(args_writer, ref ? ref : "");
    yjson_writer_string(args_writer, pipeline_path ? pipeline_path : "");
    yjson_writer_int(args_writer, timeout_seconds > 0 ? timeout_seconds : GP_LEASE_TTL_DEFAULT);
    yjson_writer_int(args_writer, picomesh_security_now());
    char *args = rel_args_take(args_writer);
    struct picomesh_int64_result id_res = gp_exec_field(&handle, hdrs,
        "INSERT INTO pipeline_runs(repo_id, status, ref, pipeline_path, timeout, created_at) "
        "VALUES(?, 0, ?, ?, ?, ?)", args, "last_insert_rowid");
    free(args);
    if (PICOMESH_IS_ERR(id_res)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue_job: persist job failed", id_res);
    if (id_res.value <= 0) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_enqueue_job: no insert id");
    return PICOMESH_OK(picomesh_uint32, (uint32_t)id_res.value);
}

PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_lease")
struct picomesh_uint32_result git_pipeline_git_pipeline_lease_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         uint32_t runner_id)
{
    (void)ctx; (void)obj;
    /* Leasing is a runner operation: the caller's runner token must match the
     * runner_id it leases as (issue #30). Mirrors lease_job. */
    if (!gp_caller_owns(hdrs, runner_id))
        return PICOMESH_ERR(picomesh_uint32, "git_pipeline_lease: caller is not this runner");
    struct rel_handle handle;
    struct picomesh_void_result open_res = gp_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_uint32, "git_pipeline_lease: storage open failed", open_res);
    return gp_claim_next(&handle, hdrs, runner_id);
}

/* The runner-agent lease entry point: claims the next job and returns the full
 * descriptor (or JSON null when the queue is empty). `labels` reserved (MVP). */
PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_lease_job")
struct picomesh_json_result git_pipeline_git_pipeline_lease_job_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         uint32_t runner_id, const char *labels)
{
    (void)ctx; (void)obj; (void)labels;
    if (!gp_caller_owns(hdrs, runner_id))
        return PICOMESH_ERR(picomesh_json, "git_pipeline_lease_job: caller is not this runner");
    struct rel_handle handle;
    struct picomesh_void_result open_res = gp_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_json, "git_pipeline_lease_job: storage open failed", open_res);
    struct picomesh_uint32_result claim_res = gp_claim_next(&handle, hdrs, runner_id);
    if (PICOMESH_IS_ERR(claim_res)) return PICOMESH_ERR(picomesh_json, "git_pipeline_lease_job: claim failed", claim_res);
    if (claim_res.value == 0) { char *null_str = strdup("null"); return null_str ? PICOMESH_OK(picomesh_json, null_str)
                                                               : PICOMESH_ERR(picomesh_json, "git_pipeline_lease_job: oom"); }
    return gp_descriptor_json(&handle, hdrs, claim_res.value);
}

PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_job_descriptor")
struct picomesh_json_result git_pipeline_git_pipeline_job_descriptor_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         uint32_t job_id)
{
    (void)ctx; (void)obj;
    struct rel_handle handle;
    struct picomesh_void_result open_res = gp_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_json, "git_pipeline_job_descriptor: storage open failed", open_res);
    if (!gp_job_readable(&handle, hdrs, job_id))
        return PICOMESH_ERR(picomesh_json, "git_pipeline_job_descriptor: forbidden (insufficient namespace role)");
    return gp_descriptor_json(&handle, hdrs, job_id);
}

/* Append a log chunk for a job the caller currently holds the lease on. OK
 * value: the new total log length. */
PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_append_log")
struct picomesh_int64_result git_pipeline_git_pipeline_append_log_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         uint32_t job_id, int64_t offset, const char *chunk)
{
    (void)ctx; (void)obj; (void)offset;
    struct rel_handle handle;
    struct picomesh_void_result open_res = gp_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_int64, "git_pipeline_append_log: storage open failed", open_res);
    uint32_t leased_runner = 0; int status = -1;
    struct picomesh_int_result load_res = job_load(&handle, hdrs, job_id, NULL, &leased_runner, &status);
    if (PICOMESH_IS_ERR(load_res)) return PICOMESH_ERR(picomesh_int64, "git_pipeline_append_log: load failed", load_res);
    if (load_res.value == 0) return PICOMESH_ERR(picomesh_int64, "git_pipeline_append_log: unknown job");
    if (!gp_caller_owns(hdrs, leased_runner)) return PICOMESH_ERR(picomesh_int64, "git_pipeline_append_log: caller does not own this lease");

    /* Append in one statement; the new length is derived from the stored row so
     * concurrent appends can't lose bytes the way a read-modify-write would. */
    struct yjson_writer *args_writer = yjson_writer_new();
    if (!args_writer) return PICOMESH_ERR(picomesh_int64, "git_pipeline_append_log: oom");
    yjson_writer_begin_array(args_writer);
    yjson_writer_string(args_writer, chunk ? chunk : "");
    yjson_writer_int(args_writer, (int64_t)job_id);
    char *args = rel_args_take(args_writer);
    struct picomesh_int64_result append_res = gp_exec_field(&handle, hdrs,
        "UPDATE pipeline_runs SET log = log || ? WHERE id=?", args, "changes");
    free(args);
    if (PICOMESH_IS_ERR(append_res)) return PICOMESH_ERR(picomesh_int64, "git_pipeline_append_log: log write failed", append_res);

    char *len_args = rel_args1i((int64_t)job_id);
    struct picomesh_int64_result len_res = rel_query_int_result(&handle, hdrs,
        "SELECT length(log) AS n FROM pipeline_runs WHERE id=?", len_args, "n", 0);
    free(len_args);
    if (PICOMESH_IS_ERR(len_res)) return PICOMESH_ERR(picomesh_int64, "git_pipeline_append_log: length read failed", len_res);
    return PICOMESH_OK(picomesh_int64, len_res.value);
}

PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_read_log")
struct picomesh_string_result git_pipeline_git_pipeline_read_log_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         uint32_t job_id)
{
    (void)ctx; (void)obj;
    struct rel_handle handle;
    struct picomesh_void_result open_res = gp_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_string, "git_pipeline_read_log: storage open failed", open_res);
    if (!gp_job_readable(&handle, hdrs, job_id))
        return PICOMESH_ERR(picomesh_string, "git_pipeline_read_log: forbidden (insufficient namespace role)");
    char *args = rel_args1i((int64_t)job_id);
    char *log = rel_query_str(&handle, hdrs, "SELECT log FROM pipeline_runs WHERE id=?", args, "log");
    free(args);
    if (!log) { char *empty = strdup(""); return empty ? PICOMESH_OK(picomesh_string, empty)
                                                  : PICOMESH_ERR(picomesh_string, "git_pipeline_read_log: oom"); }
    return PICOMESH_OK(picomesh_string, log);
}

PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_complete")
struct picomesh_int_result git_pipeline_git_pipeline_complete_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         uint32_t job_id, int32_t status)
{
    (void)ctx; (void)obj;
    struct rel_handle handle;
    struct picomesh_void_result open_res = gp_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_int, "git_pipeline_complete: storage open failed", open_res);
    /* FAIL CLOSED (issue #30): the runner that leased it, OR maintainer+ on the
     * repo's namespace (or site admin / trusted internal). */
    uint32_t repo_id = 0, leased_runner = 0; int row_status = -1;
    struct picomesh_int_result load_res = job_load(&handle, hdrs, job_id, &repo_id, &leased_runner, &row_status);
    if (PICOMESH_IS_ERR(load_res)) return PICOMESH_ERR(picomesh_int, "git_pipeline_complete: load failed", load_res);
    if (load_res.value == 0) return PICOMESH_OK(picomesh_int, 0); /* no such job */
    int by_runner = leased_runner != 0 && gp_caller_owns(hdrs, leased_runner);
    if (!by_runner && !gp_caller_can_write(hdrs, repo_id, "maintainer"))
        return PICOMESH_ERR(picomesh_int, "git_pipeline_complete: forbidden (insufficient namespace role)");
    return gp_transition(&handle, hdrs, job_id, status);
}

/* Runner-reported completion: store the summary, transition the job, and clear
 * the lease expiry. Owner-gated. */
PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_complete_job")
struct picomesh_int_result git_pipeline_git_pipeline_complete_job_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         uint32_t job_id, int32_t status, const char *summary)
{
    (void)ctx; (void)obj;
    struct rel_handle handle;
    struct picomesh_void_result open_res = gp_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_int, "git_pipeline_complete_job: storage open failed", open_res);
    uint32_t leased_runner = 0; int row_status = -1;
    struct picomesh_int_result load_res = job_load(&handle, hdrs, job_id, NULL, &leased_runner, &row_status);
    if (PICOMESH_IS_ERR(load_res)) return PICOMESH_ERR(picomesh_int, "git_pipeline_complete_job: load failed", load_res);
    if (load_res.value == 0) return PICOMESH_OK(picomesh_int, 0);
    if (!gp_caller_owns(hdrs, leased_runner)) return PICOMESH_ERR(picomesh_int, "git_pipeline_complete_job: caller does not own this lease");

    /* Store the summary and clear the lease in the same terminal UPDATE so a
     * completed job never keeps a live lease_expiry. */
    int final_status = status == 0 ? 2 : 3;
    struct yjson_writer *args_writer = yjson_writer_new();
    if (!args_writer) return PICOMESH_ERR(picomesh_int, "git_pipeline_complete_job: oom");
    yjson_writer_begin_array(args_writer);
    yjson_writer_int(args_writer, final_status);
    yjson_writer_string(args_writer, summary ? summary : "");
    yjson_writer_int(args_writer, (int64_t)job_id);
    char *args = rel_args_take(args_writer);
    struct picomesh_int64_result changes_res = gp_exec_field(&handle, hdrs,
        "UPDATE pipeline_runs SET status=?, summary=?, lease_expiry=0 WHERE id=? AND status IN (0,1)",
        args, "changes");
    free(args);
    if (PICOMESH_IS_ERR(changes_res)) return PICOMESH_ERR(picomesh_int, "git_pipeline_complete_job: transition failed", changes_res);
    return PICOMESH_OK(picomesh_int, changes_res.value > 0 ? 1 : 0);
}

/* Sweep running jobs whose lease has expired back to queued. OK value: number
 * requeued (a single atomic UPDATE over all expired rows). */
PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_requeue_expired")
struct picomesh_size_result git_pipeline_git_pipeline_requeue_expired_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct rel_handle handle;
    struct picomesh_void_result open_res = gp_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_size, "git_pipeline_requeue_expired: storage open failed", open_res);
    char *args = rel_args1i(picomesh_security_now());
    struct picomesh_int64_result changes_res = gp_exec_field(&handle, hdrs,
        "UPDATE pipeline_runs SET status=0, runner_id=0, lease_expiry=0 "
        "WHERE status=1 AND lease_expiry>0 AND lease_expiry<=?", args, "changes");
    free(args);
    if (PICOMESH_IS_ERR(changes_res)) return PICOMESH_ERR(picomesh_size, "git_pipeline_requeue_expired: requeue failed", changes_res);
    return PICOMESH_OK(picomesh_size, (size_t)(changes_res.value < 0 ? 0 : changes_res.value));
}

/* COUNT helper for a status filter. */
static struct picomesh_size_result gp_count(struct yheaders *hdrs, const char *where, const char *errlabel)
{
    struct rel_handle handle;
    struct picomesh_void_result open_res = gp_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_size, errlabel, open_res);
    char sql[96];
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) AS n FROM pipeline_runs WHERE %s", where);
    struct picomesh_int64_result count_res = rel_query_int_result(&handle, hdrs, sql, "[]", "n", 0);
    if (PICOMESH_IS_ERR(count_res)) return PICOMESH_ERR(picomesh_size, errlabel, count_res);
    return PICOMESH_OK(picomesh_size, (size_t)(count_res.value < 0 ? 0 : count_res.value));
}

PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_count_pending")
struct picomesh_size_result git_pipeline_git_pipeline_count_pending_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    return gp_count(hdrs, "status=0", "git_pipeline_count_pending: read failed");
}

PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_count_running")
struct picomesh_size_result git_pipeline_git_pipeline_count_running_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    return gp_count(hdrs, "status=1", "git_pipeline_count_running: read failed");
}

PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_count_done")
struct picomesh_size_result git_pipeline_git_pipeline_count_done_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    return gp_count(hdrs, "status IN (2,3)", "git_pipeline_count_done: read failed");
}

/* List ALL pipeline runs as a JSON array of rows (gh#15) — site admin only. */
PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_list")
struct picomesh_json_result git_pipeline_git_pipeline_list_impl(struct ctx *ctx, struct object *obj,
                                                         struct yheaders *hdrs,
                                                         int64_t offset, int64_t limit)
{
    (void)ctx; (void)obj;
    if (!gp_caller_site_admin(hdrs))
        return PICOMESH_ERR(picomesh_json, "git_pipeline_list: forbidden (site admin only)");
    struct rel_handle handle;
    struct picomesh_void_result open_res = gp_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_json, "git_pipeline_list: storage open failed", open_res);
    return rel_query_page(&handle, hdrs,
        "SELECT id, repo_id, runner_id, status, ref, pipeline_path FROM pipeline_runs", "[]", "id", 0,
        offset, limit);
}

/* Unbounded variant — every run. */
PICOMESH_CLASS_ANNOTATE("override@git_pipeline:git_pipeline:git_pipeline_list_all")
struct picomesh_json_result git_pipeline_git_pipeline_list_all_impl(struct ctx *ctx, struct object *obj,
                                                                    struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    if (!gp_caller_site_admin(hdrs))
        return PICOMESH_ERR(picomesh_json, "git_pipeline_list_all: forbidden (site admin only)");
    struct rel_handle handle;
    struct picomesh_void_result open_res = gp_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_json, "git_pipeline_list_all: storage open failed", open_res);
    return rel_query_page(&handle, hdrs,
        "SELECT id, repo_id, runner_id, status, ref, pipeline_path FROM pipeline_runs", "[]", "id", 0, 0, 0);
}

#include "store.gen.c"
