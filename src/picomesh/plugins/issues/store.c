/* issues — bug tracker (relational, issue #18).
 *
 *   open(repo_id, author_id)              → issue_id
 *   close(issue_id)                       → 1 closed / 0 unknown
 *   status(issue_id)                      → 0 unknown / 1 open / 2 closed
 *   count_open_in_repo(repo_id)           → number of open issues
 *   count_total                           → total tracked
 *
 * State is RELATIONAL ROWS in the `rstore_uid` cluster's `issues` table
 * (id, repo_id, number, status, author_uid, created_at), provisioned by that
 * cluster's schema. This replaces the previous `sharded_storage` KV layout —
 * the hand-maintained `open:<repo>` counter is now a real `COUNT(… status=
 * 'open')`, and the close compare-and-set is a single `UPDATE … WHERE status=
 * 'open'`, so the open count can never drift from the rows. Like the other
 * migrated runtime services (accounts/session/token_issuer) the table lives on
 * the cluster's global shard; per-namespace placement lands with the git_repo
 * migration. */

#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yclass/yheaders.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/ysecurity/jwt.h>
#include <picomesh/ysecurity/secret.h>
#include <picomesh/yjson/yjson.h>
#include <picomesh/plugin/relational_storage/relational_sql.h>
#include <picomesh/plugin/git_repo/git_repo.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ISSUES_STORE "rstore_uid" /* the uid-sharded data cluster */
#define ISSUES_DB    "uid"        /* logical database within it */

/* No in-memory state — every op delegates to relational storage. */
struct PICOMESH_CLASS_ANNOTATE("class@issues:issues") issues_issues_data {
    char _unused;
};

/* Open a handle onto the issues table. All issues live on the cluster's global
 * shard (REL_SHARD_GLOBAL), matching the other migrated runtime services. */
static struct picomesh_void_result issues_open(struct rel_handle *handle)
{
    struct picomesh_void_result open_res = rel_open(handle, ISSUES_STORE, ISSUES_DB);
    if (PICOMESH_IS_ERR(open_res))
        return PICOMESH_ERR(picomesh_void, "issues: open rstore_uid failed", open_res);
    handle->shard = REL_SHARD_GLOBAL;
    return PICOMESH_OK_VOID();
}

/* Repo-scoped issue authz (issue #30): when the caller is a VERIFIED principal
 * (JWT present), require `min_role`+ on the repo's owning namespace (inherited)
 * or a site admin. A NULL/anonymous caller is the trusted in-process path.
 * Returns 1 if allowed. */
static int is_caller_can(struct yheaders *hdrs, uint32_t repo_id, const char *min_role)
{
    struct picomesh_authctx caller;
    picomesh_authctx_from_headers(hdrs, picomesh_active_engine(), &caller);
    if (!caller.authenticated) return 0; /* fail closed */
    if (picomesh_groups_contains(caller.groups_csv, PICOMESH_GROUP_SYSTEM)) return 1; /* trusted internal */
    if (picomesh_groups_max_role(caller.groups_csv, "site") >= picomesh_role_rank("maintainer")) return 1;
    struct picomesh_engine *engine = picomesh_active_engine();
    if (!engine) return 0;
    struct ctx repo_ctx = picomesh_engine_service_ctx(engine, "git_repo");
    struct object_ptr_result create_res = git_repo_git_repo_create(&repo_ctx);
    if (PICOMESH_IS_ERR(create_res)) { picomesh_error_destroy(create_res.error); return 0; }
    struct picomesh_string_result namespace_res =
        git_repo_git_repo_namespace_of(&repo_ctx, create_res.value, NULL, repo_id);
    if (PICOMESH_IS_ERR(namespace_res)) { picomesh_error_destroy(namespace_res.error); return 0; }
    int allowed = namespace_res.value && namespace_res.value[0] &&
                  picomesh_groups_effective_role(caller.groups_csv, namespace_res.value) >=
                      picomesh_role_rank(min_role);
    free(namespace_res.value);
    return allowed;
}

/* Site admin / internal capability — gates the global issue scans. Fail closed. */
static int issues_caller_site_admin(struct yheaders *hdrs)
{
    struct picomesh_authctx caller;
    picomesh_authctx_from_headers(hdrs, picomesh_active_engine(), &caller);
    if (!caller.authenticated) return 0;
    if (picomesh_groups_contains(caller.groups_csv, PICOMESH_GROUP_SYSTEM)) return 1;
    return picomesh_groups_max_role(caller.groups_csv, "site") >= picomesh_role_rank("maintainer");
}

PICOMESH_CLASS_ANNOTATE("override@issues:issues:issues_open")
struct picomesh_uint32_result issues_issues_open_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                  uint32_t repo_id, uint32_t author_id)
{
    (void)ctx; (void)obj;
    /* Filing an issue needs reporter+ on the repo namespace (service-local
     * defense-in-depth on top of the gateway policy gate). */
    if (!is_caller_can(hdrs, repo_id, "reporter"))
        return PICOMESH_ERR(picomesh_uint32, "issues_open: forbidden (insufficient namespace role)");
    /* The author is the AUTHENTICATED caller, not a client-supplied id — a
     * reporter must not be able to file an issue as someone else. The arg is
     * honoured only for the trusted in-process (NULL-JWT) path. */
    struct picomesh_authctx caller;
    picomesh_authctx_from_headers(hdrs, picomesh_active_engine(), &caller);
    if (caller.authenticated) author_id = caller.uid;

    struct rel_handle handle;
    struct picomesh_void_result open_res = issues_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_uint32, "issues_open: storage open failed", open_res);

    /* The per-repo `number` is the next sequence for the repo, computed in the
     * same statement so concurrent opens on one repo cannot collide on the
     * UNIQUE(repo_id, number) constraint. */
    struct yjson_writer *args_writer = yjson_writer_new();
    if (!args_writer) return PICOMESH_ERR(picomesh_uint32, "issues_open: out of memory");
    yjson_writer_begin_array(args_writer);
    yjson_writer_int(args_writer, (int64_t)repo_id);
    yjson_writer_int(args_writer, (int64_t)repo_id);
    yjson_writer_int(args_writer, (int64_t)author_id);
    yjson_writer_int(args_writer, (int64_t)picomesh_security_now());
    char *args = rel_args_take(args_writer);

    struct picomesh_json_result insert_res = rel_exec(&handle, hdrs,
        "INSERT INTO issues(repo_id, number, status, author_uid, created_at) "
        "VALUES(?, (SELECT COALESCE(MAX(number),0)+1 FROM issues WHERE repo_id=?), 'open', ?, ?)",
        args);
    free(args);
    if (PICOMESH_IS_ERR(insert_res)) return PICOMESH_ERR(picomesh_uint32, "issues_open: persist issue failed", insert_res);
    struct yjson_doc *doc = yjson_parse(insert_res.value ? insert_res.value : "{}",
                                        insert_res.value ? strlen(insert_res.value) : 2);
    int64_t issue_id = doc ? yjson_as_int(yjson_object_get(yjson_doc_root(doc), "last_insert_rowid"), 0) : 0;
    if (doc) yjson_doc_free(doc);
    free(insert_res.value);
    if (issue_id <= 0) return PICOMESH_ERR(picomesh_uint32, "issues_open: no insert id");
    yinfo("issues: open id=%lld repo=%u by=%u", (long long)issue_id, repo_id, author_id);
    return PICOMESH_OK(picomesh_uint32, (uint32_t)issue_id);
}

PICOMESH_CLASS_ANNOTATE("override@issues:issues:issues_close")
struct picomesh_int_result issues_issues_close_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                uint32_t issue_id)
{
    (void)ctx; (void)obj;
    struct rel_handle handle;
    struct picomesh_void_result open_res = issues_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_int, "issues_close: storage open failed", open_res);

    /* Load repo_id + current status. rel_query_int_result keeps a real backend
     * error distinct from "absent" (fallback) so we never treat a read failure
     * as a missing issue. */
    char *load_args = rel_args1i((int64_t)issue_id);
    struct picomesh_int64_result repo_res =
        rel_query_int_result(&handle, hdrs, "SELECT repo_id FROM issues WHERE id=?", load_args, "repo_id", 0);
    if (PICOMESH_IS_ERR(repo_res)) { free(load_args); return PICOMESH_ERR(picomesh_int, "issues_close: load failed", repo_res); }
    uint32_t repo_id = (uint32_t)repo_res.value;
    if (repo_id == 0) { free(load_args); return PICOMESH_OK(picomesh_int, 0); } /* unknown issue */
    free(load_args);

    /* Closing needs reporter+ on the repo namespace (service-local). */
    if (!is_caller_can(hdrs, repo_id, "reporter"))
        return PICOMESH_ERR(picomesh_int, "issues_close: forbidden (insufficient namespace role)");

    /* Flip open→closed atomically. `changes==1` ⇒ we closed it; `0` ⇒ it was
     * absent or already closed (no double work), distinct from a backend error. */
    char *close_args = rel_args1i((int64_t)issue_id);
    struct picomesh_json_result update_res =
        rel_exec(&handle, hdrs, "UPDATE issues SET status='closed' WHERE id=? AND status='open'", close_args);
    free(close_args);
    if (PICOMESH_IS_ERR(update_res)) return PICOMESH_ERR(picomesh_int, "issues_close: close update failed", update_res);
    struct yjson_doc *doc = yjson_parse(update_res.value ? update_res.value : "{}",
                                        update_res.value ? strlen(update_res.value) : 2);
    int64_t changes = doc ? yjson_as_int(yjson_object_get(yjson_doc_root(doc), "changes"), 0) : 0;
    if (doc) yjson_doc_free(doc);
    free(update_res.value);
    return PICOMESH_OK(picomesh_int, changes > 0 ? 1 : 0);
}

PICOMESH_CLASS_ANNOTATE("override@issues:issues:issues_status")
struct picomesh_int_result issues_issues_status_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                 uint32_t issue_id)
{
    (void)ctx; (void)obj;
    struct rel_handle handle;
    struct picomesh_void_result open_res = issues_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_int, "issues_status: storage open failed", open_res);

    char *args = rel_args1i((int64_t)issue_id);
    struct picomesh_json_result row_res =
        rel_query(&handle, hdrs, "SELECT repo_id, status FROM issues WHERE id=?", args);
    free(args);
    if (PICOMESH_IS_ERR(row_res)) return PICOMESH_ERR(picomesh_int, "issues_status: load failed", row_res);
    struct yjson_doc *doc = yjson_parse(row_res.value ? row_res.value : "[]",
                                        row_res.value ? strlen(row_res.value) : 2);
    free(row_res.value);
    const struct yjson_value *array = doc ? yjson_doc_root(doc) : NULL;
    if (!array || !yjson_is_array(array) || yjson_array_size(array) == 0) {
        if (doc) yjson_doc_free(doc);
        return PICOMESH_OK(picomesh_int, 0); /* unknown issue */
    }
    const struct yjson_value *row = yjson_array_at(array, 0);
    uint32_t repo_id = (uint32_t)yjson_as_int(yjson_object_get(row, "repo_id"), 0);
    const char *status = yjson_as_string(yjson_object_get(row, "status"), "");
    int closed = status && strcmp(status, "closed") == 0;
    yjson_doc_free(doc);

    /* Reading an issue's status needs reporter+ on the repo's namespace. */
    if (!is_caller_can(hdrs, repo_id, "reporter"))
        return PICOMESH_ERR(picomesh_int, "issues_status: forbidden (insufficient namespace role)");
    return PICOMESH_OK(picomesh_int, closed ? 2 : 1);
}

PICOMESH_CLASS_ANNOTATE("override@issues:issues:issues_count_open_in_repo")
struct picomesh_size_result issues_issues_count_open_in_repo_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                              uint32_t repo_id)
{
    (void)ctx; (void)obj;
    /* The open-issue count is repo-scoped: reporter+ on the repo's namespace. */
    if (!is_caller_can(hdrs, repo_id, "reporter"))
        return PICOMESH_ERR(picomesh_size, "issues_count_open: forbidden (insufficient namespace role)");
    struct rel_handle handle;
    struct picomesh_void_result open_res = issues_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_size, "issues_count_open: storage open failed", open_res);

    char *args = rel_args1i((int64_t)repo_id);
    struct picomesh_int64_result count_res = rel_query_int_result(&handle, hdrs,
        "SELECT COUNT(*) AS n FROM issues WHERE repo_id=? AND status='open'", args, "n", 0);
    free(args);
    if (PICOMESH_IS_ERR(count_res)) return PICOMESH_ERR(picomesh_size, "issues_count_open: read failed", count_res);
    return PICOMESH_OK(picomesh_size, (size_t)(count_res.value < 0 ? 0 : count_res.value));
}

PICOMESH_CLASS_ANNOTATE("override@issues:issues:issues_count_total")
struct picomesh_size_result issues_issues_count_total_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct rel_handle handle;
    struct picomesh_void_result open_res = issues_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_size, "issues_count_total: storage open failed", open_res);
    struct picomesh_int64_result count_res =
        rel_query_int_result(&handle, hdrs, "SELECT COUNT(*) AS n FROM issues", "[]", "n", 0);
    if (PICOMESH_IS_ERR(count_res)) return PICOMESH_ERR(picomesh_size, "issues_count_total: read failed", count_res);
    return PICOMESH_OK(picomesh_size, (size_t)(count_res.value < 0 ? 0 : count_res.value));
}

/* List issues as a JSON array of rows (gh#15) — every issue, not scoped per
 * repo. Site-admin only (global cross-repo scan). */
PICOMESH_CLASS_ANNOTATE("override@issues:issues:issues_list")
struct picomesh_json_result issues_issues_list_impl(struct ctx *ctx, struct object *obj,
                                                   struct yheaders *hdrs,
                                                   int64_t offset, int64_t limit)
{
    (void)ctx; (void)obj;
    if (!issues_caller_site_admin(hdrs))
        return PICOMESH_ERR(picomesh_json, "issues_list: forbidden (site admin only)");
    struct rel_handle handle;
    struct picomesh_void_result open_res = issues_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_json, "issues_list: storage open failed", open_res);
    return rel_query_page(&handle, hdrs,
        "SELECT id, repo_id, number, status, author_uid, created_at FROM issues", "[]", "id", 0,
        offset, limit);
}

/* Unbounded variant — every issue. Use with care on large deployments. */
PICOMESH_CLASS_ANNOTATE("override@issues:issues:issues_list_all")
struct picomesh_json_result issues_issues_list_all_impl(struct ctx *ctx, struct object *obj,
                                                        struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    if (!issues_caller_site_admin(hdrs))
        return PICOMESH_ERR(picomesh_json, "issues_list_all: forbidden (site admin only)");
    struct rel_handle handle;
    struct picomesh_void_result open_res = issues_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_json, "issues_list_all: storage open failed", open_res);
    return rel_query_page(&handle, hdrs,
        "SELECT id, repo_id, number, status, author_uid, created_at FROM issues", "[]", "id", 0, 0, 0);
}

/* The repo an issue belongs to (0 if absent). Lets the authorizer resolve
 * issue_id -> repo_id -> namespace and gate issue operations by the repo's
 * namespace role (issue #30). */
PICOMESH_CLASS_ANNOTATE("override@issues:issues:issues_repo_of")
struct picomesh_uint32_result issues_issues_repo_of_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         uint32_t issue_id)
{
    (void)ctx; (void)obj;
    struct rel_handle handle;
    struct picomesh_void_result open_res = issues_open(&handle);
    if (PICOMESH_IS_ERR(open_res)) return PICOMESH_ERR(picomesh_uint32, "issues_repo_of: storage open failed", open_res);
    char *args = rel_args1i((int64_t)issue_id);
    struct picomesh_int64_result repo_res =
        rel_query_int_result(&handle, hdrs, "SELECT repo_id FROM issues WHERE id=?", args, "repo_id", 0);
    free(args);
    if (PICOMESH_IS_ERR(repo_res)) return PICOMESH_ERR(picomesh_uint32, "issues_repo_of: read failed", repo_res);
    return PICOMESH_OK(picomesh_uint32, (uint32_t)repo_res.value);
}

#include "store.gen.c"
