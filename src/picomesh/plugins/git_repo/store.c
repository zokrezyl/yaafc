/* git_repo — repository metadata + on-disk bare repos via libgit2.
 *
 *   make(owner_id, owner_name, repo_name) → repo_id (libgit2 init_bare on disk)
 *   delete(repo_id)                       → 1 / 0   (rm -rf the bare repo)
 *   owner_of(repo_id)                     → owner_uid (0 if missing)
 *   count_for_owner(owner_id)             → number of repos owned
 *   count_total                           → grand total
 *
 * On-disk layout (GitHub-style, per-user parent):
 *
 *   <repos_dir>/<owner_name>/<repo_name>.git
 *
 * `repos_dir` comes from yconfig (`git_repo.repos_dir`, default
 * `/tmp/picoforge/repos`). `owner_name` and `repo_name` are validated
 * against a strict charset (`[A-Za-z0-9._-]`, 1..63 chars) before
 * they go anywhere near the filesystem — preventing both path
 * traversal and SQL-like escape into other names.
 *
 * Metadata lives in the shared `sharded_storage` service (context
 * `git_repo`), NOT in this process — see the storage-layout comment below.
 * That is what lets multiple git_repo objects / gateway workers share one
 * source of truth (the on-disk repos + these rows) with nothing cached in
 * memory to fragment.
 *
 * libgit2 runtime: `git_libgit2_init()` is reference-counted internally
 * but we never shut it down — process-lifetime. The lazy-init pattern
 * (function-local static "tried" flag) mirrors backend_mdbx's shared
 * env init: there is no file-scope mutable state. */

#define _XOPEN_SOURCE 700  /* nftw / FTW_DEPTH / FTW_PHYS */

#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/yclass/class.h>
#include <picomesh/yclass/yheaders.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yconfig/yconfig.h>
#include <picomesh/yloop/yloop.h>
#include <picomesh/yloop/yexec.h>
#include <picomesh/ycore/idkey.h>
#include <picomesh/ysecurity/jwt.h>
#include <picomesh/ysecurity/secret.h>
#include <picomesh/plugin/sharded_storage/sharded_storage.h>
#include <picomesh/plugin/accounts/accounts.h>

#include <git2.h>
#include <pthread.h>
#include <uthash.h>

#include <errno.h>
#include <ftw.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define REPO_NAME_MAX 64

/* Repo metadata lives in the shared `sharded_storage` service, NOT in this
 * process's memory. The bare repos on disk (libgit2) are the content source
 * of truth; the storage rows below are the index. This is what lets ANY
 * number of git_repo objects / gateway workers see the same repos — there is
 * no per-object cached table to fragment.
 *
 * Storage layout in the `git_repo` context:
 *   repo:<rid>        → "<owner_id>\t<owner_name>\t<repo_name>\t<is_public>\t<namespace_id>"
 *   count             → total live repos (decimal)
 *   owner:<owner_id>  → newline-separated repo names that uid owns
 *
 * `owner_name` is the repo's owning NAMESPACE PATH (personal `alice` or nested
 * group `acme/platform`); `namespace_id` is the canonical id of that namespace
 * in the accounts `namespaces` table — the repo's reference into the namespace
 * tree (issue #30). Older rows without the 5th field fall back to hash(path).
 */
#define GIT_REPO_CTX "git_repo"

/* The class object carries NO repo state — every op delegates to storage. */
struct PICOMESH_CLASS_ANNOTATE("class@git_repo:git_repo") git_repo_git_repo_data {
    char _unused;
};

struct gr_storage {
    struct ctx c;
    struct object *obj;
};
PICOMESH_RESULT_DECLARE(gr_storage, struct gr_storage);

/* Open the sharded_storage dependency. peer==NULL ⇒ collocated in-process;
 * non-NULL ⇒ remote. Both go through sharded_storage_db_create, which caches
 * one proxy per channel (service-lifetime), so this is cheap to call per op. */
static struct gr_storage_result gr_open(void)
{
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return PICOMESH_ERR(gr_storage, "git_repo: no active engine");
    struct gr_storage h = {.c = picomesh_engine_service_ctx(e, "sharded_storage")};
    struct object_ptr_result o = sharded_storage_db_create(&h.c);
    if (PICOMESH_IS_ERR(o)) return PICOMESH_ERR(gr_storage, "git_repo: storage_db_create failed", o);
    h.obj = o.value;
    return PICOMESH_OK(gr_storage, h);
}

/* Resolve the owning namespace PATH to its canonical id via the accounts
 * service (issue #30). The namespaces table is the authority: a repo can only be
 * created under a namespace that already exists. Returns the id (>0) on success,
 * 0 when no such namespace exists; a backend error propagates. */
static struct picomesh_int64_result gr_resolve_namespace(struct yheaders *hdrs, const char *path)
{
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) return PICOMESH_ERR(picomesh_int64, "git_repo: no active engine for namespace resolve");
    struct ctx c = picomesh_engine_service_ctx(e, "accounts");
    struct object_ptr_result o = accounts_accounts_create(&c);
    if (PICOMESH_IS_ERR(o)) return PICOMESH_ERR(picomesh_int64, "git_repo: accounts unreachable", o);
    return accounts_accounts_ns_resolve(&c, o.value, hdrs, path);
}

/* A parsed repo metadata row. */
struct repo_rec {
    uint32_t owner_id;
    char     owner_name[REPO_NAME_MAX];
    char     repo_name[REPO_NAME_MAX];
    int      is_public;
    uint32_t namespace_id;   /* canonical id of the owning namespace (issue #30) */
};

/* Load repo:<rid> into *out. OK value: 1 = present (parsed into *out),
 * 0 = absent. A backend read failure is propagated — distinct from "no
 * such repo". */
static struct picomesh_int_result repo_load(struct gr_storage *h, struct yheaders *hdrs, uint32_t rid, struct repo_rec *out)
{
    char k[40];
    snprintf(k, sizeof(k), "repo:%u", rid);
    struct picomesh_string_result r = sharded_storage_db_get(&h->c, h->obj, hdrs, GIT_REPO_CTX, k);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int, "git_repo: repo read failed", r);
    if (!r.value || !r.value[0]) { free(r.value); return PICOMESH_OK(picomesh_int, 0); }
    char *s = r.value;
    char *t1 = strchr(s, '\t');
    char *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
    char *t3 = t2 ? strchr(t2 + 1, '\t') : NULL;
    if (!t1 || !t2 || !t3) { free(s); return PICOMESH_OK(picomesh_int, 0); }
    char *t4 = strchr(t3 + 1, '\t');   /* optional 5th field: namespace_id */
    *t1 = *t2 = *t3 = 0;
    if (t4) *t4 = 0;
    memset(out, 0, sizeof(*out));
    out->owner_id = (uint32_t)strtoul(s, NULL, 10);
    snprintf(out->owner_name, sizeof(out->owner_name), "%s", t1 + 1);
    snprintf(out->repo_name,  sizeof(out->repo_name),  "%s", t2 + 1);
    out->is_public = atoi(t3 + 1) ? 1 : 0;
    /* Older rows predate namespace_id — derive it from the path so the field is
     * always populated (the id is the deterministic hash of the path). */
    out->namespace_id = t4 ? (uint32_t)strtoul(t4 + 1, NULL, 10) : picomesh_fnv1a32(out->owner_name);
    if (!out->namespace_id) out->namespace_id = picomesh_fnv1a32(out->owner_name);
    free(s);
    return PICOMESH_OK(picomesh_int, 1);
}

/* Resource-level namespace RBAC check (issue #30). Returns 1 iff the verified
 * caller (identity + roles from the JWT in `hdrs`) holds at least
 * `required_role` on `ns_path` — counting role inheritance from parent
 * namespaces — OR is a site admin (site:maintainer+). Anonymous or invalid auth
 * yields 0 (fail closed). This intentionally mirrors the gateway policy gate as
 * defense-in-depth: a backend never trusts that it was only reached through the
 * boundary. The personal-namespace owner holds `owner` (>= every role), so the
 * common single-owner repo keeps working. */
static int repo_caller_has_role(struct yheaders *hdrs, const char *ns_path, const char *required_role)
{
    struct picomesh_authctx caller;
    picomesh_authctx_from_headers(hdrs, picomesh_active_engine(), &caller);
    if (!caller.authenticated) return 0; /* fail closed */
    if (picomesh_groups_contains(caller.groups_csv, PICOMESH_GROUP_SYSTEM)) return 1; /* trusted internal */
    if (picomesh_groups_max_role(caller.groups_csv, "site") >= picomesh_role_rank("maintainer"))
        return 1;
    return picomesh_groups_effective_role(caller.groups_csv, ns_path) >= picomesh_role_rank(required_role);
}

/* Visibility gate for the per-owner repo listings (issue #30): a signed-in
 * caller may enumerate ONLY their OWN repos (caller uid == owner_id), or a site
 * admin / trusted internal capability may enumerate anyone's. This stops a
 * private repo's names/counts leaking across namespaces to any signed-in user.
 * Fail closed: no credential → denied. (A future refinement can widen this to
 * public repos plus namespaces where the caller holds reporter+.) */
static int repo_caller_may_list(struct yheaders *hdrs, uint32_t owner_id)
{
    struct picomesh_authctx caller;
    picomesh_authctx_from_headers(hdrs, picomesh_active_engine(), &caller);
    if (!caller.authenticated) return 0;
    if (caller.uid == owner_id) return 1;
    if (picomesh_groups_contains(caller.groups_csv, PICOMESH_GROUP_SYSTEM)) return 1;
    return picomesh_groups_max_role(caller.groups_csv, "site") >= picomesh_role_rank("maintainer");
}

/* Write the canonical repo row; propagates a failed write. */
static struct picomesh_void_result repo_store_row(struct gr_storage *h, struct yheaders *hdrs, uint32_t rid, const struct repo_rec *rec)
{
    char k[40], v[192];
    snprintf(k, sizeof(k), "repo:%u", rid);
    snprintf(v, sizeof(v), "%u\t%s\t%s\t%d\t%u",
             rec->owner_id, rec->owner_name, rec->repo_name, rec->is_public, rec->namespace_id);
    struct picomesh_int_result r = sharded_storage_db_set(&h->c, h->obj, hdrs, GIT_REPO_CTX, k, v);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_void, "git_repo: repo write failed", r);
    return PICOMESH_OK_VOID();
}

/* Read the global repo count. Backend error propagated; absent → 0. */
static struct picomesh_int64_result gr_count_get(struct gr_storage *h, struct yheaders *hdrs)
{
    struct picomesh_string_result r = sharded_storage_db_get(&h->c, h->obj, hdrs, GIT_REPO_CTX, "count");
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int64, "git_repo: count read failed", r);
    int64_t n = (r.value && r.value[0]) ? strtoll(r.value, NULL, 10) : 0;
    free(r.value);
    return PICOMESH_OK(picomesh_int64, n < 0 ? 0 : n);
}

/* Atomic counter bump — OK value is the value after the add; backend failure
 * propagated, never collapsed into 0. */
static struct picomesh_int64_result gr_incr(struct gr_storage *h, struct yheaders *hdrs, const char *key, int64_t delta)
{
    struct picomesh_int64_result r = sharded_storage_db_incr(&h->c, h->obj, hdrs, GIT_REPO_CTX, key, delta);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int64, "git_repo: counter update failed", r);
    return r;
}

/* Atomic compare-and-set. OK value: 1 = swapped, 0 = compare mismatch. An
 * empty `expected` matches an absent/empty key, so the same primitive both
 * creates and updates the owner index. A backend error is propagated. */
static struct picomesh_int_result gr_cas(struct gr_storage *h, struct yheaders *hdrs, const char *key,
                                         const char *expected, const char *replacement)
{
    struct picomesh_int_result r =
        sharded_storage_db_compare_and_set(&h->c, h->obj, hdrs, GIT_REPO_CTX, key, expected, replacement);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_int, "git_repo: compare-and-set failed", r);
    return r;
}

/* The per-owner index: "owner:<uid>" = newline-joined repo names. OK value is
 * the malloc'd list (caller frees), "" when the owner has none. A backend
 * read failure is propagated. */
static struct picomesh_string_result owner_list_get(struct gr_storage *h, struct yheaders *hdrs, uint32_t owner_id)
{
    char k[40];
    snprintf(k, sizeof(k), "owner:%u", owner_id);
    struct picomesh_string_result r = sharded_storage_db_get(&h->c, h->obj, hdrs, GIT_REPO_CTX, k);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_string, "git_repo: owner index read failed", r);
    return r;
}

/* Is `name` present as a whole line in the newline-joined `list`? */
static int name_in_list(const char *list, const char *name)
{
    size_t nl = strlen(name);
    for (const char *p = list; p && *p; ) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == nl && memcmp(p, name, nl) == 0) return 1;
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

/* Append repo_name to "owner:<uid>" atomically via optimistic CAS-retry:
 * read the list, append, swap only if it has not changed under us. This is
 * what stops two concurrent creates by the same owner from clobbering each
 * other's list entry. Idempotent — a name already present is a no-op. A
 * backend read/CAS error is propagated; only a clean CAS mismatch retries. */
static struct picomesh_void_result owner_list_add(struct gr_storage *h, struct yheaders *hdrs,
                                                  uint32_t owner_id, const char *repo_name)
{
    char k[40];
    snprintf(k, sizeof(k), "owner:%u", owner_id);
    for (int attempt = 0; attempt < 128; ++attempt) {
        struct picomesh_string_result lr = owner_list_get(h, hdrs, owner_id);
        if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_void, "git_repo: owner index read failed", lr);
        char *list = lr.value;
        const char *cur = list ? list : "";
        if (name_in_list(cur, repo_name)) { free(list); return PICOMESH_OK_VOID(); }
        size_t need = strlen(cur) + strlen(repo_name) + 2;
        char *nl = malloc(need);
        if (!nl) { free(list); return PICOMESH_ERR(picomesh_void, "git_repo: owner index out of memory"); }
        snprintf(nl, need, "%s%s\n", cur, repo_name);
        struct picomesh_int_result cas = gr_cas(h, hdrs, k, cur, nl);
        free(nl);
        free(list);
        if (PICOMESH_IS_ERR(cas)) return PICOMESH_ERR(picomesh_void, "git_repo: owner index CAS failed", cas);
        if (cas.value) return PICOMESH_OK_VOID();
        /* clean mismatch → another writer changed the list; retry */
    }
    return PICOMESH_ERR(picomesh_void, "git_repo: owner index append contended out");
}

/* Remove repo_name from "owner:<uid>" atomically (same CAS-retry shape).
 * Rebuilds the list without mutating the bytes we pass as `expected`. */
static struct picomesh_void_result owner_list_remove(struct gr_storage *h, struct yheaders *hdrs,
                                                     uint32_t owner_id, const char *repo_name)
{
    char k[40];
    snprintf(k, sizeof(k), "owner:%u", owner_id);
    for (int attempt = 0; attempt < 128; ++attempt) {
        struct picomesh_string_result lr = owner_list_get(h, hdrs, owner_id);
        if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_void, "git_repo: owner index read failed", lr);
        char *list = lr.value;
        if (!list || !*list) { free(list); return PICOMESH_OK_VOID(); }  /* nothing to remove */
        size_t cap = strlen(list) + 1;
        char *nl = malloc(cap);
        if (!nl) { free(list); return PICOMESH_ERR(picomesh_void, "git_repo: owner index out of memory"); }
        nl[0] = 0;
        size_t len = 0, rn = strlen(repo_name);
        for (const char *p = list; *p; ) {
            const char *e = strchr(p, '\n');
            size_t llen = e ? (size_t)(e - p) : strlen(p);
            if (llen > 0 && !(llen == rn && memcmp(p, repo_name, rn) == 0))
                len += (size_t)snprintf(nl + len, cap - len, "%.*s\n", (int)llen, p);
            if (!e) break;
            p = e + 1;
        }
        struct picomesh_int_result cas = gr_cas(h, hdrs, k, list, nl);
        free(nl);
        free(list);
        if (PICOMESH_IS_ERR(cas)) return PICOMESH_ERR(picomesh_void, "git_repo: owner index CAS failed", cas);
        if (cas.value) return PICOMESH_OK_VOID();
    }
    return PICOMESH_ERR(picomesh_void, "git_repo: owner index remove contended out");
}

/* ---- per-NAMESPACE-PATH repo index (issue #30) -------------------------- *
 * The owner index above keys by owner uid, so a GROUP repo created as
 * make(<admin uid>, "acme", "api") is filed under the admin's uid, and the
 * group's namespace page (which knows only the path "acme") can't find it.
 * This parallel index keys by the repo's owning NAMESPACE PATH ("ns:<path>"),
 * so a namespace's repos — personal or group — are enumerable by path. Same
 * newline-joined value + CAS-retry shape as the owner index, generalised over an
 * arbitrary key. */
static struct picomesh_string_result repo_index_get(struct gr_storage *h, struct yheaders *hdrs, const char *key)
{
    struct picomesh_string_result r = sharded_storage_db_get(&h->c, h->obj, hdrs, GIT_REPO_CTX, key);
    if (PICOMESH_IS_ERR(r)) return PICOMESH_ERR(picomesh_string, "git_repo: index read failed", r);
    return r;
}

static struct picomesh_void_result repo_index_add(struct gr_storage *h, struct yheaders *hdrs,
                                                  const char *key, const char *name)
{
    for (int attempt = 0; attempt < 128; ++attempt) {
        struct picomesh_string_result lr = repo_index_get(h, hdrs, key);
        if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_void, "git_repo: index read failed", lr);
        char *list = lr.value;
        const char *cur = list ? list : "";
        if (name_in_list(cur, name)) { free(list); return PICOMESH_OK_VOID(); }
        size_t need = strlen(cur) + strlen(name) + 2;
        char *nl = malloc(need);
        if (!nl) { free(list); return PICOMESH_ERR(picomesh_void, "git_repo: index out of memory"); }
        snprintf(nl, need, "%s%s\n", cur, name);
        struct picomesh_int_result cas = gr_cas(h, hdrs, key, cur, nl);
        free(nl);
        free(list);
        if (PICOMESH_IS_ERR(cas)) return PICOMESH_ERR(picomesh_void, "git_repo: index CAS failed", cas);
        if (cas.value) return PICOMESH_OK_VOID();
    }
    return PICOMESH_ERR(picomesh_void, "git_repo: index append contended out");
}

static struct picomesh_void_result repo_index_remove(struct gr_storage *h, struct yheaders *hdrs,
                                                     const char *key, const char *name)
{
    for (int attempt = 0; attempt < 128; ++attempt) {
        struct picomesh_string_result lr = repo_index_get(h, hdrs, key);
        if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_void, "git_repo: index read failed", lr);
        char *list = lr.value;
        if (!list || !*list) { free(list); return PICOMESH_OK_VOID(); }
        size_t cap = strlen(list) + 1;
        char *nl = malloc(cap);
        if (!nl) { free(list); return PICOMESH_ERR(picomesh_void, "git_repo: index out of memory"); }
        nl[0] = 0;
        size_t len = 0, rn = strlen(name);
        for (const char *p = list; *p; ) {
            const char *e = strchr(p, '\n');
            size_t llen = e ? (size_t)(e - p) : strlen(p);
            if (llen > 0 && !(llen == rn && memcmp(p, name, rn) == 0))
                len += (size_t)snprintf(nl + len, cap - len, "%.*s\n", (int)llen, p);
            if (!e) break;
            p = e + 1;
        }
        struct picomesh_int_result cas = gr_cas(h, hdrs, key, list, nl);
        free(nl);
        free(list);
        if (PICOMESH_IS_ERR(cas)) return PICOMESH_ERR(picomesh_void, "git_repo: index CAS failed", cas);
        if (cas.value) return PICOMESH_OK_VOID();
    }
    return PICOMESH_ERR(picomesh_void, "git_repo: index remove contended out");
}

/* "ns:<namespace-path>" storage key for the per-namespace repo index. */
static void ns_index_key(char *out, size_t cap, const char *path)
{
    snprintf(out, cap, "ns:%s", path);
}

/* Resolve `git_repo.repos_dir` from yconfig; default is a per-host tmp
 * tree. The pointer is owned by yconfig (stable for the process life). */
/* The bare-repo root is REQUIRED config — no hardcoded default. A silent
 * fallback would let a misconfigured node create repos under a shared/wrong
 * directory unnoticed. Returns NULL (and the caller fails) when it can't be
 * resolved. The pointer is owned by yconfig (stable for the process life). */
static const char *resolve_repos_dir(void)
{
    struct picomesh_engine *e = picomesh_active_engine();
    if (!e) {
        ywarn("git_repo: no active engine — cannot resolve required config 'git_repo.repos_dir'");
        return NULL;
    }
    struct yconfig_node_ptr_result r =
        yconfig_get(picomesh_engine_config(e), "git_repo.repos_dir");
    const char *s = (PICOMESH_IS_OK(r) && r.value) ? yconfig_node_as_string(r.value, NULL) : NULL;
    if (!s || !*s) {
        ywarn("git_repo: required config 'git_repo.repos_dir' is missing — "
              "refusing to fall back to a shared default");
        return NULL;
    }
    return s;
}

/* Whether to create the on-disk bare repo (libgit2 git_repository_init)
 * at make time. Default ON. Set `git_repo.disk_init: false` to record
 * only the metadata — the bare repo is needed for real git push/clone,
 * but NOT for the HTML UI (listing/browsing), and `git_repository_init`
 * does dozens of tiny file writes that are crippling slow on the
 * in-browser wasm-emulated disk (it hangs create-repo). The demo turns
 * it off; real deployments leave it on. */
static int repo_disk_init_enabled(void)
{
    struct picomesh_engine *e = picomesh_active_engine();
    if (e) {
        struct yconfig_node_ptr_result r =
            yconfig_get(picomesh_engine_config(e), "git_repo.disk_init");
        if (PICOMESH_IS_OK(r) && r.value)
            return yconfig_node_as_int(r.value, 1) != 0;
    }
    return 1;
}

/* Initialise libgit2 once for the process. Subsequent callers reuse
 * the same global state libgit2 manages internally. The function-local
 * `tried` flag prevents the call from re-running on failure. */
static int ensure_libgit2(void)
{
    static int tried = 0;
    static int ok = 0;
    if (tried) return ok;
    int rc = git_libgit2_init();
    tried = 1;
    if (rc < 0) {
        const git_error *e = git_error_last();
        ywarn("git_repo: git_libgit2_init failed: %s",
              e && e->message ? e->message : "(no msg)");
        return 0;
    }
    ok = 1;
    ydebug("git_repo: libgit2 initialised (ref-count=%d)", rc);
    return 1;
}

/* Strict path-segment whitelist: lower/upper alpha, digits, dot, dash,
 * underscore; length 1..REPO_NAME_MAX-1; no leading dot (blocks `.git`
 * collisions and dotfile shenanigans). Same shape as the frontend's
 * `username_ok` / `reponame_ok` checks — we re-validate at the sink so
 * a future caller bypassing the frontend can't reach the filesystem
 * with a hostile name. */
static int path_segment_ok(const char *s)
{
    if (!s || !*s) return 0;
    if (s[0] == '.') return 0;
    size_t n = 0;
    for (const char *p = s; *p; ++p, ++n) {
        if (n >= REPO_NAME_MAX - 1) return 0;
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '.' || c == '-' || c == '_'))
            return 0;
    }
    return 1;
}

/* Validate a namespace PATH: one or more `/`-joined segments, each a valid
 * path segment. A repo's owning namespace may be a personal namespace
 * (`alice`) or a nested group namespace (`acme/platform`), so `owner_name`
 * is a path, not a single segment. Empty or trailing/double-slash is rejected. */
static int path_ok(const char *s)
{
    if (!s || !*s) return 0;
    char buf[256];
    if (strlen(s) >= sizeof(buf)) return 0;
    /* The full path is stored verbatim in repo_rec.owner_name (a single
     * REPO_NAME_MAX field) and is the basis of the deterministic repo id. A
     * path that does not fit would be silently truncated on store, so the id
     * (hashed from the full path) would no longer match the stored/derived
     * path, corrupting namespace_of and every later auth check. Reject rather
     * than truncate — fail closed. */
    if (strlen(s) >= REPO_NAME_MAX) return 0;
    snprintf(buf, sizeof(buf), "%s", s);
    int segments = 0;
    for (char *tok = strtok(buf, "/"); tok; tok = strtok(NULL, "/")) {
        if (!path_segment_ok(tok)) return 0;
        ++segments;
    }
    /* strtok collapses adjacent slashes, so re-check the raw form has no
     * leading/trailing/double slash that would let two paths alias. */
    size_t len = strlen(s);
    if (s[0] == '/' || s[len - 1] == '/' || strstr(s, "//")) return 0;
    return segments > 0;
}

/* Deterministic repo id: FNV-1a of "<owner_name>/<repo_name>" → uint32,
 * 0 nudged to 1 (0 is reserved for "missing"). This MUST match the
 * gateway's hash_repo() in src/picomesh/frontends/yhttp/frontend.c — the
 * gateway computes the id from names on every page (no lookup round-trip)
 * and the services must store/find repos under that same id. Keeping the
 * id derived from names (not a private counter) is what makes
 * read_tree/read_file/put_file reachable from the gateway. */
static uint32_t repo_hash(const char *owner_name, const char *repo_name)
{
    char key[160];
    snprintf(key, sizeof(key), "%s/%s", owner_name, repo_name);
    uint32_t h = 2166136261u;
    for (const char *p = key; *p; ++p) {
        h ^= (unsigned char)*p;
        h *= 16777619u;
    }
    return h ? h : 1;
}

/* Build `<repos_dir>/<owner_name>/<repo_name>.git` into `out`. */
static int repo_dir_build(const char *owner_name, const char *repo_name,
                          char *out, size_t cap)
{
    const char *root = resolve_repos_dir();
    if (!root) return -1;
    int n = snprintf(out, cap, "%s/%s/%s.git", root, owner_name, repo_name);
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

/* mkdir -p for the parent of the repo dir. libgit2's repository_init
 * makes the leaf, but the parent (`repos_dir`) has to exist. */
static int mkdir_p(const char *path)
{
    char buf[1024];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(buf)) return -1;
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i < n; ++i) {
        if (buf[i] == '/') {
            buf[i] = 0;
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
            buf[i] = '/';
        }
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

/* nftw callback: unlink files, rmdir directories. */
static int rm_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
    (void)st; (void)flag; (void)ftw;
    return remove(path);
}

static int rm_rf(const char *path)
{
    /* FTW_DEPTH: visit children before parents; FTW_PHYS: don't follow
     * symlinks (the bare repo never contains any, but defensive). */
    return nftw(path, rm_entry, 16, FTW_DEPTH | FTW_PHYS);
}

/* The on-disk half of make: create the per-user parent dir and run
 * libgit2's bare init. Both are blocking filesystem calls — on the
 * in-browser RISC-V emulator a single git_repository_init runs for tens
 * of seconds — so this runs on the libuv worker pool (yloop_run_blocking),
 * NOT the loop thread. It therefore touches ONLY its own `arg`: no
 * object, no yconfig, no loop state. Results (rc + a snapshot of
 * libgit2's thread-local error message) come back in the struct; the
 * caller inspects them after the await resumes. */
struct git_init_work {
    char parent[1024];   /* <repos_dir>/<owner_name> — mkdir_p target */
    char path[1024];     /* <repos_dir>/<owner_name>/<repo_name>.git  */
    int  mkdir_errno;    /* 0 on success, else errno from mkdir_p     */
    int  git_rc;         /* libgit2 return code (< 0 is failure)      */
    char git_errmsg[256];/* libgit2 error snapshot (same pool thread) */
};

static void git_init_work_fn(void *shard_state, void *arg)
{
    (void)shard_state;   /* make creates a fresh repo; nothing to cache here */
    struct git_init_work *w = arg;
    if (mkdir_p(w->parent) != 0) {
        w->mkdir_errno = errno ? errno : -1;
        return;
    }
    git_repository *repo = NULL;
    w->git_rc = git_repository_init(&repo, w->path, /*is_bare*/ 1);
    if (w->git_rc < 0) {
        const git_error *e = git_error_last();
        snprintf(w->git_errmsg, sizeof(w->git_errmsg), "%s",
                 e && e->message ? e->message : "(no msg)");
    } else {
        git_repository_free(repo);
    }
}

/* ---- tree/blob/commit I/O (libgit2, on the worker pool) ----------- *
 *
 * read_tree / read_file / put_file all touch the on-disk bare repo —
 * blocking I/O that must NOT run on the loop thread (a single libgit2
 * call costs tens of seconds under the in-browser emulator). Each has a
 * `*_work_fn` that runs on the libuv worker pool via the same
 * run_blocking_or_inline() dispatch make uses; the work fn touches ONLY
 * its own payload struct (no object, no yconfig, no loop state). Strings
 * handed back in `out` are malloc'd; ownership transfers to the Result
 * the impl returns (picomesh_string contract).                          */

/* ---- per-shard repository handle cache + affine executor -----------------
 *
 * libgit2 work (open repo → commit/read) is offloaded off the loop thread, but
 * NOT to the generic libuv pool: it runs on a KEY-AFFINE executor (yexec) keyed
 * by repo path, so every op for a repo lands on ONE owning thread. That thread
 * keeps the repo's git_repository handle in its OWN cache and reuses it across
 * calls — killing the ~12% per-commit git_repository_open (profiled) — with NO
 * locking, because affinity guarantees a repo is never touched by two threads
 * at once. Same-repo ops serialize on their owner thread, which is correct
 * anyway (commits to one branch share the ref). */

struct gr_cached {
    char path[1024];           /* key */
    git_repository *repo;
    UT_hash_handle hh;
};
struct gr_repo_cache {
    struct gr_cached *by_path;  /* uthash root; head = oldest (FIFO eviction) */
    int count;
    int max;                    /* open-fd budget per shard */
};

/* One per shard, built on the shard's own thread. */
static void *gr_cache_init(void *ud)
{
    (void)ud;
    ensure_libgit2();           /* this thread runs libgit2; init is ref-counted */
    struct gr_repo_cache *c = calloc(1, sizeof(*c));
    if (c) c->max = 64;
    return c;
}

static void gr_cache_free(void *state)
{
    struct gr_repo_cache *c = state;
    if (!c) return;
    struct gr_cached *e, *tmp;
    HASH_ITER(hh, c->by_path, e, tmp) {
        HASH_DEL(c->by_path, e);
        git_repository_free(e->repo);
        free(e);
    }
    free(c);
}

/* Open-or-reuse the handle for `path`. Cache-OWNED — never freed by callers
 * (use gr_repo_release, a no-op). Single shard thread ⇒ no lock, and no
 * in-use eviction race (only one work fn runs per shard at a time). */
static int gr_repo_acquire(struct gr_repo_cache *c, const char *path, git_repository **out)
{
    *out = NULL;
    struct gr_cached *e = NULL;
    if (c) HASH_FIND_STR(c->by_path, path, e);
    if (e) { *out = e->repo; return 0; }

    git_repository *repo = NULL;
    if (git_repository_open(&repo, path) != 0) return -1;
    *out = repo;
    if (!c) return 0;           /* defensive: no cache ⇒ uncached (shouldn't happen) */

    e = calloc(1, sizeof(*e));
    if (e) {
        snprintf(e->path, sizeof(e->path), "%s", path);
        e->repo = repo;
        HASH_ADD_STR(c->by_path, path, e);
        c->count++;
        if (c->max && c->count > c->max) {   /* over budget → evict the oldest */
            struct gr_cached *old = c->by_path;
            if (old && old != e) {
                HASH_DEL(c->by_path, old);
                git_repository_free(old->repo);
                free(old);
                c->count--;
            }
        }
    }
    return 0;
}

/* The cache owns the handle; releasing is a no-op (freed on eviction/teardown).
 * Kept for 1:1 symmetry with the old open/free shape so the work fns read
 * cleanly and can never accidentally free a cached handle. */
static void gr_repo_release(struct gr_repo_cache *c, git_repository *repo)
{
    (void)c; (void)repo;
}

/* Process-wide affine git executor (one per process, like sharded_storage's
 * shard set). `git_repo.commit_shards` (default 8) is the commit-parallelism
 * knob: N worker threads, each owning a disjoint set of repos. */
static struct yexec *git_exec(void)
{
    static struct yexec *exec = NULL;
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mu);
    if (!exec) {
        int n = 8;
        struct picomesh_engine *e = picomesh_active_engine();
        const struct yconfig *cfg = e ? picomesh_engine_config(e) : NULL;
        if (cfg) {
            struct yconfig_node_ptr_result r = yconfig_get(cfg, "git_repo.commit_shards");
            if (PICOMESH_IS_OK(r) && r.value) n = (int)yconfig_node_as_int(r.value, 8);
            else if (PICOMESH_IS_ERR(r)) picomesh_error_destroy(r.error);
        }
        if (n < 1) n = 1;
        exec = yexec_create(n, gr_cache_init, gr_cache_free, NULL);
    }
    pthread_mutex_unlock(&mu);
    return exec;
}

/* Route a libgit2 work fn to the shard owning `key_path` (its on-disk repo
 * dir), suspending this coroutine until it completes. Replaces the old generic
 * libuv-pool offload so the per-shard handle cache can stay lock-free. Outside
 * a coroutine, yexec_submit runs the fn inline on a throwaway cache. */
static void gr_run(const char *key_path, void (*fn)(void *shard_state, void *arg), void *work)
{
    struct picomesh_engine *e = picomesh_active_engine();
    struct yloop *loop = e ? picomesh_engine_loop(e) : NULL;
    yexec_submit(git_exec(), loop, picomesh_fnv1a32(key_path), fn, work);
}

struct git_read_work {
    char repo_path[1024];
    char ref[128];
    char path[1024];
    char *out;        /* malloc'd NUL-terminated; ownership → caller */
    size_t out_len;
    int rc;           /* 0 = ok */
};

/* Resolve <ref> (default "HEAD") to its tree. Non-zero return means the
 * ref doesn't resolve — for an empty repo (unborn HEAD) that's expected,
 * and read_tree treats it as "no entries" rather than an error. */
static int resolve_tree(git_repository *repo, const char *ref, git_tree **out_tree)
{
    const char *spec = (ref && *ref) ? ref : "HEAD";
    git_object *obj = NULL;
    if (git_revparse_single(&obj, repo, spec) != 0) return -1;
    git_object *peeled = NULL;
    int rc = git_object_peel(&peeled, obj, GIT_OBJECT_TREE);
    git_object_free(obj);
    if (rc != 0) return rc;
    *out_tree = (git_tree *)peeled;
    return 0;
}

static void git_read_tree_work_fn(void *shard_state, void *ud)
{
    struct gr_repo_cache *cache = shard_state;
    struct git_read_work *w = ud;
    w->out = NULL; w->out_len = 0; w->rc = 0;

    git_repository *repo = NULL;
    if (gr_repo_acquire(cache, w->repo_path, &repo) != 0) { w->rc = -1; return; }

    git_tree *tree = NULL;
    if (resolve_tree(repo, w->ref, &tree) != 0) {
        /* Empty/unborn repo → empty listing, not an error. */
        gr_repo_release(cache, repo);
        w->out = strdup("");
        w->rc = w->out ? 0 : -1;
        return;
    }

    /* Descend into a subdirectory when path is non-empty. */
    git_tree *subtree = NULL;
    const git_tree *cur = tree;
    if (w->path[0]) {
        git_tree_entry *te = NULL;
        if (git_tree_entry_bypath(&te, tree, w->path) != 0 ||
            git_tree_entry_type(te) != GIT_OBJECT_TREE) {
            if (te) git_tree_entry_free(te);
            git_tree_free(tree); gr_repo_release(cache, repo);
            w->rc = -2; /* path is not a directory */
            return;
        }
        int lr = git_tree_lookup(&subtree, repo, git_tree_entry_id(te));
        git_tree_entry_free(te);
        if (lr != 0) { git_tree_free(tree); gr_repo_release(cache, repo); w->rc = -3; return; }
        cur = subtree;
    }

    /* One entry per line: "<type>\t<name>\n", type ∈ {tree,blob}. git
     * returns entries name-sorted already. */
    size_t n = git_tree_entrycount(cur);
    size_t cap = 256, len = 0;
    char *buf = malloc(cap);
    if (buf) {
        buf[0] = 0;
        for (size_t i = 0; i < n; i++) {
            const git_tree_entry *e = git_tree_entry_byindex(cur, i);
            const char *nm = git_tree_entry_name(e);
            const char *ty = git_tree_entry_type(e) == GIT_OBJECT_TREE ? "tree" : "blob";
            size_t need = strlen(ty) + 1 + strlen(nm) + 1;
            if (len + need + 1 > cap) {
                while (len + need + 1 > cap) cap *= 2;
                char *nb = realloc(buf, cap);
                if (!nb) { free(buf); buf = NULL; break; }
                buf = nb;
            }
            len += (size_t)snprintf(buf + len, cap - len, "%s\t%s\n", ty, nm);
        }
    }
    if (buf) { buf[len] = 0; w->out = buf; w->out_len = len; w->rc = 0; }
    else w->rc = -1;

    if (subtree) git_tree_free(subtree);
    git_tree_free(tree);
    gr_repo_release(cache, repo);
}

static void git_read_file_work_fn(void *shard_state, void *ud)
{
    struct gr_repo_cache *cache = shard_state;
    struct git_read_work *w = ud;
    w->out = NULL; w->out_len = 0; w->rc = 0;

    git_repository *repo = NULL;
    if (gr_repo_acquire(cache, w->repo_path, &repo) != 0) { w->rc = -1; return; }

    git_tree *tree = NULL;
    if (resolve_tree(repo, w->ref, &tree) != 0) { gr_repo_release(cache, repo); w->rc = -2; return; }

    git_tree_entry *te = NULL;
    if (git_tree_entry_bypath(&te, tree, w->path) != 0 ||
        git_tree_entry_type(te) != GIT_OBJECT_BLOB) {
        if (te) git_tree_entry_free(te);
        git_tree_free(tree); gr_repo_release(cache, repo);
        w->rc = -3; /* not a file */
        return;
    }
    git_blob *blob = NULL;
    int lr = git_blob_lookup(&blob, repo, git_tree_entry_id(te));
    git_tree_entry_free(te);
    if (lr != 0) { git_tree_free(tree); gr_repo_release(cache, repo); w->rc = -4; return; }

    size_t sz = (size_t)git_blob_rawsize(blob);
    const void *raw = git_blob_rawcontent(blob);
    char *out = malloc(sz + 1);
    if (out) { memcpy(out, raw, sz); out[sz] = 0; w->out = out; w->out_len = sz; w->rc = 0; }
    else w->rc = -1;

    git_blob_free(blob);
    git_tree_free(tree);
    gr_repo_release(cache, repo);
}

struct git_put_work {
    char repo_path[1024];
    char path[1024];
    const char *content;   /* borrowed; alive while the worker runs */
    size_t content_len;
    char message[512];
    char author_name[128];
    char author_email[128];
    char out_oid[GIT_OID_HEXSZ + 1];  /* hex commit id on success */
    int rc;
};

/* Insert blob_oid at comps[idx..n-1] under `base` (NULL = empty tree),
 * writing the resulting tree id to *out. One path component per level,
 * preserving sibling entries of any existing subtree. */
static int put_insert(git_repository *repo, const git_tree *base,
                      char **comps, int idx, int n,
                      const git_oid *blob_oid, git_oid *out)
{
    git_treebuilder *bld = NULL;
    if (git_treebuilder_new(&bld, repo, base) != 0) return -1;

    int rc;
    if (idx == n - 1) {
        rc = git_treebuilder_insert(NULL, bld, comps[idx], blob_oid, GIT_FILEMODE_BLOB);
    } else {
        git_tree *sub = NULL;
        if (base) {
            const git_tree_entry *e = git_treebuilder_get(bld, comps[idx]);
            if (e && git_tree_entry_type(e) == GIT_OBJECT_TREE)
                git_tree_lookup(&sub, repo, git_tree_entry_id(e));
        }
        git_oid sub_oid;
        rc = put_insert(repo, sub, comps, idx + 1, n, blob_oid, &sub_oid);
        if (sub) git_tree_free(sub);
        if (rc == 0)
            rc = git_treebuilder_insert(NULL, bld, comps[idx], &sub_oid, GIT_FILEMODE_TREE);
    }
    if (rc == 0) rc = git_treebuilder_write(out, bld);
    git_treebuilder_free(bld);
    return rc;
}

static void git_put_file_work_fn(void *shard_state, void *ud)
{
    struct gr_repo_cache *cache = shard_state;
    struct git_put_work *w = ud;
    w->rc = 0; w->out_oid[0] = 0;

    git_repository *repo = NULL;
    if (gr_repo_acquire(cache, w->repo_path, &repo) != 0) { w->rc = -1; return; }

    /* 1) content → blob */
    git_oid blob_oid;
    if (git_blob_create_from_buffer(&blob_oid, repo, w->content, w->content_len) != 0) {
        gr_repo_release(cache, repo); w->rc = -2; return;
    }

    /* 2) the branch HEAD points at + its tip commit + that commit's tree */
    char branch_ref[256];
    snprintf(branch_ref, sizeof(branch_ref), "refs/heads/master");
    git_reference *head = NULL;
    if (git_reference_lookup(&head, repo, "HEAD") == 0) {
        if (git_reference_type(head) == GIT_REFERENCE_SYMBOLIC) {
            const char *t = git_reference_symbolic_target(head);
            if (t) snprintf(branch_ref, sizeof(branch_ref), "%s", t);
        }
        git_reference_free(head);
    }

    git_commit *parent = NULL;
    git_tree *base = NULL;
    git_oid parent_oid;
    int has_parent = (git_reference_name_to_id(&parent_oid, repo, "HEAD") == 0);
    if (has_parent && git_commit_lookup(&parent, repo, &parent_oid) == 0)
        git_commit_tree(&base, parent);

    /* 3) split path into components, build the new root tree */
    char pathbuf[1024];
    snprintf(pathbuf, sizeof(pathbuf), "%s", w->path);
    char *comps[64];
    int n = 0;
    for (char *tok = strtok(pathbuf, "/"); tok && n < 64; tok = strtok(NULL, "/"))
        comps[n++] = tok;
    if (n == 0) {
        if (base) git_tree_free(base);
        if (parent) git_commit_free(parent);
        gr_repo_release(cache, repo);
        w->rc = -3; return;
    }

    git_oid tree_oid;
    int rc = put_insert(repo, base, comps, 0, n, &blob_oid, &tree_oid);
    if (base) git_tree_free(base);
    if (rc != 0) {
        if (parent) git_commit_free(parent);
        gr_repo_release(cache, repo);
        w->rc = -4; return;
    }

    git_tree *new_tree = NULL;
    if (git_tree_lookup(&new_tree, repo, &tree_oid) != 0) {
        if (parent) git_commit_free(parent);
        gr_repo_release(cache, repo);
        w->rc = -5; return;
    }

    /* 4) signature + commit (updates branch_ref, so HEAD advances) */
    git_signature *sig = NULL;
    const char *an = w->author_name[0] ? w->author_name : "picoforge";
    const char *ae = w->author_email[0] ? w->author_email : "picoforge@localhost";
    if (git_signature_now(&sig, an, ae) != 0) {
        git_tree_free(new_tree);
        if (parent) git_commit_free(parent);
        gr_repo_release(cache, repo);
        w->rc = -6; return;
    }

    git_oid commit_oid;
    const git_commit *parents[1] = { parent };
    rc = git_commit_create(&commit_oid, repo, branch_ref, sig, sig, NULL,
                           w->message[0] ? w->message : "update", new_tree,
                           has_parent ? 1 : 0, has_parent ? parents : NULL);
    if (rc == 0) git_oid_tostr(w->out_oid, sizeof(w->out_oid), &commit_oid);
    else w->rc = -7;

    git_signature_free(sig);
    git_tree_free(new_tree);
    if (parent) git_commit_free(parent);
    gr_repo_release(cache, repo);
}

PICOMESH_CLASS_ANNOTATE("override@git_repo:git_repo:git_repo_make")
struct picomesh_uint32_result git_repo_git_repo_make_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                      uint32_t owner_id,
                                                      const char *owner_name,
                                                      const char *repo_name)
{
    (void)ctx; (void)obj;
    /* owner_name is the owning NAMESPACE PATH — a personal namespace (`alice`)
     * or a nested group namespace (`acme/platform`). repo_name is a single
     * segment. */
    if (!path_ok(owner_name) || !path_segment_ok(repo_name))
        return PICOMESH_ERR(picomesh_uint32, "git_repo_make: invalid owner_name/repo_name");
    /* A repo may NOT be named after a URL route word (issues/runs/edit/new/
     * settings): the webapp resolves `<namespace>/<repo>/<verb>` by treating a
     * trailing route word as the verb, so a repo with such a name would be
     * unbrowseable in a nested namespace (issue #30). */
    {
        static const char *const reserved[] = {"issues", "runs", "edit", "new", "settings"};
        for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i)
            if (strcmp(repo_name, reserved[i]) == 0)
                return PICOMESH_ERR(picomesh_uint32, "git_repo_make: reserved repo name");
    }

    /* The owning namespace must already exist in the canonical namespaces table
     * (issue #30): a repo references a real namespace_id, it does not implicitly
     * create one. Resolved BEFORE any on-disk work so a bad request creates
     * nothing. */
    struct picomesh_int64_result nsr = gr_resolve_namespace(hdrs, owner_name);
    if (PICOMESH_IS_ERR(nsr)) return PICOMESH_ERR(picomesh_uint32, "git_repo_make: namespace resolve failed", nsr);
    if (nsr.value <= 0) return PICOMESH_ERR(picomesh_uint32, "git_repo_make: owning namespace does not exist");
    uint32_t namespace_id = (uint32_t)nsr.value;

    /* Service-local authz (FAIL CLOSED, issue #30): require developer+ on the
     * target namespace, a site admin, or the trusted internal capability (the
     * gateway's /repos/new bootstrap, which presents a signed system token). A
     * credential-less caller is denied — the backend never assumes it was only
     * reached through the boundary. */
    if (!repo_caller_has_role(hdrs, owner_name, "developer"))
        return PICOMESH_ERR(picomesh_uint32, "git_repo_make: forbidden (insufficient namespace role)");

    /* The creator uid is taken from the VERIFIED auth context, never the
     * caller-supplied `owner_id` argument (issue #30). Otherwise a developer on
     * `acme` could pass another user's uid and poison that user's owner index
     * (their /repos would enumerate a repo they may not read). The only caller
     * permitted to set `owner_id` to an arbitrary uid is the trusted internal
     * capability (the gateway's /repos/new bootstrap, which mints a system
     * token carrying the real creator's uid). */
    {
        struct picomesh_authctx creator;
        picomesh_authctx_from_headers(hdrs, picomesh_active_engine(), &creator);
        if (!creator.authenticated)
            return PICOMESH_ERR(picomesh_uint32, "git_repo_make: authentication required");
        if (!picomesh_groups_contains(creator.groups_csv, PICOMESH_GROUP_SYSTEM))
            owner_id = creator.uid;
    }

    /* Id is derived from the names (FNV-1a, see repo_hash) so the gateway and
     * every service agree on it without a lookup. */
    uint32_t repo_id = repo_hash(owner_name, repo_name);

    struct gr_storage_result sr = gr_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_uint32, "git_repo_make: storage open failed", sr);
    struct gr_storage h = sr.value;

    /* Reject a duplicate up front (same id ⇒ same owner/name). A backend
     * read failure here is propagated, not treated as "no duplicate". */
    struct repo_rec existing;
    struct picomesh_int_result dup = repo_load(&h, hdrs, repo_id, &existing);
    if (PICOMESH_IS_ERR(dup)) return PICOMESH_ERR(picomesh_uint32, "git_repo_make: duplicate check failed", dup);
    if (dup.value) {
        /* The repo already exists. A PRIOR create may have committed the repo
         * row but failed before updating an index (the owner/namespace indexes
         * are written after the row). Repair them idempotently so the namespace
         * page lists the repo on a retry, then report the duplicate. */
        char full_dup[192];
        snprintf(full_dup, sizeof(full_dup), "%s/%s", existing.owner_name, existing.repo_name);
        (void)owner_list_add(&h, hdrs, existing.owner_id, full_dup);
        char nsk_dup[200];
        ns_index_key(nsk_dup, sizeof(nsk_dup), existing.owner_name);
        (void)repo_index_add(&h, hdrs, nsk_dup, existing.repo_name);
        return PICOMESH_ERR(picomesh_uint32, "git_repo_make: repo already exists");
    }

    /* On-disk bare repo (libgit2) FIRST: if it fails, no metadata has been
     * written, so there is nothing to roll back (an orphaned on-disk dir is
     * harmless; orphaned metadata with no repo would not be). Needed for real
     * git push/clone, but crippling slow on the in-browser emulated disk, so
     * the blocking work runs on the libuv worker pool (the loop keeps serving)
     * and is gated by `git_repo.disk_init`. */
    if (repo_disk_init_enabled()) {
        if (!ensure_libgit2())
            return PICOMESH_ERR(picomesh_uint32, "git_repo_make: libgit2 init failed");
        struct git_init_work work = {0};
        if (repo_dir_build(owner_name, repo_name, work.path, sizeof(work.path)) != 0)
            return PICOMESH_ERR(picomesh_uint32, "git_repo_make: repos_dir not configured or path too long");
        const char *root = resolve_repos_dir();
        if (!root)
            return PICOMESH_ERR(picomesh_uint32, "git_repo_make: required config 'git_repo.repos_dir' missing");
        int pn = snprintf(work.parent, sizeof(work.parent), "%s/%s", root, owner_name);
        if (pn <= 0 || (size_t)pn >= sizeof(work.parent))
            return PICOMESH_ERR(picomesh_uint32, "git_repo_make: path too long");
        gr_run(work.path, git_init_work_fn, &work);
        if (work.mkdir_errno != 0) {
            ywarn("git_repo: cannot create per-user parent %s: %s",
                  work.parent, strerror(work.mkdir_errno));
            return PICOMESH_ERR(picomesh_uint32, "git_repo_make: mkdir parent failed");
        }
        if (work.git_rc < 0) {
            ywarn("git_repo: init(%s) failed: %s", work.path, work.git_errmsg);
            return PICOMESH_ERR(picomesh_uint32, "git_repo_make: libgit2 init failed");
        }
        yinfo("git_repo: created repo=%u %s/%s at %s", repo_id, owner_name, repo_name, work.path);
    } else {
        yinfo("git_repo: recorded repo=%u %s/%s (disk_init off)", repo_id, owner_name, repo_name);
    }

    /* Metadata → shared storage. put_if_absent ELECTS exactly one creator
     * for this repo_id (deterministic from the names): only the winner bumps
     * the global count and appends to the owner index, so two concurrent
     * creates of the same repo can't double-count or duplicate the index
     * entry. The earlier repo_load check is a best-effort fast reject; this
     * is the authoritative atomic guard. */
    struct repo_rec rec = {.owner_id = owner_id, .is_public = 0, .namespace_id = namespace_id};
    snprintf(rec.owner_name, sizeof(rec.owner_name), "%s", owner_name);
    snprintf(rec.repo_name,  sizeof(rec.repo_name),  "%s", repo_name);
    char rk[40], rv[192];
    snprintf(rk, sizeof(rk), "repo:%u", repo_id);
    snprintf(rv, sizeof(rv), "%u\t%s\t%s\t%d\t%u",
             rec.owner_id, rec.owner_name, rec.repo_name, rec.is_public, rec.namespace_id);
    struct picomesh_int_result ins =
        sharded_storage_db_put_if_absent(&h.c, h.obj, hdrs, GIT_REPO_CTX, rk, rv);
    if (PICOMESH_IS_ERR(ins))
        return PICOMESH_ERR(picomesh_uint32, "git_repo_make: storage write failed", ins);
    if (ins.value == 0)
        return PICOMESH_ERR(picomesh_uint32, "git_repo_make: repo already exists");
    struct picomesh_int64_result cinc = gr_incr(&h, hdrs, "count", 1);
    if (PICOMESH_IS_ERR(cinc)) return PICOMESH_ERR(picomesh_uint32, "git_repo_make: bump count failed", cinc);
    /* The owner index keys by the creator's uid but stores the repo's FULL PATH
     * (<namespace>/<repo>), so "your repositories" links resolve correctly even
     * for a repo created in a GROUP namespace (where the path != the username). */
    char full_path[192];
    snprintf(full_path, sizeof(full_path), "%s/%s", owner_name, repo_name);
    struct picomesh_void_result oadd = owner_list_add(&h, hdrs, owner_id, full_path);
    if (PICOMESH_IS_ERR(oadd)) return PICOMESH_ERR(picomesh_uint32, "git_repo_make: owner index append failed", oadd);
    /* Also index by the owning NAMESPACE PATH (value = bare repo name, since the
     * path is the key) so the repo is enumerable from its namespace page. */
    char nsk[200];
    ns_index_key(nsk, sizeof(nsk), owner_name);
    struct picomesh_void_result nsadd = repo_index_add(&h, hdrs, nsk, repo_name);
    if (PICOMESH_IS_ERR(nsadd)) return PICOMESH_ERR(picomesh_uint32, "git_repo_make: namespace index append failed", nsadd);
    return PICOMESH_OK(picomesh_uint32, repo_id);
}

PICOMESH_CLASS_ANNOTATE("override@git_repo:git_repo:git_repo_delete")
struct picomesh_int_result git_repo_git_repo_delete_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                   uint32_t repo_id)
{
    (void)ctx; (void)obj;
    struct gr_storage_result sr = gr_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_int, "git_repo_delete: storage open failed", sr);
    struct gr_storage h = sr.value;

    struct repo_rec rec;
    struct picomesh_int_result lr = repo_load(&h, hdrs, repo_id, &rec);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_int, "git_repo_delete: load failed", lr);
    if (lr.value == 0) return PICOMESH_OK(picomesh_int, 0);  /* no such repo */

    /* Deleting a repo is a maintainer-grade operation on its namespace
     * (inherited), or a site admin / trusted internal capability (issue #30).
     * Fail closed: a credential-less caller is denied. */
    if (!repo_caller_has_role(hdrs, rec.owner_name, "maintainer"))
        return PICOMESH_ERR(picomesh_int, "git_repo_delete: forbidden (insufficient namespace role)");

    /* The row delete is the AUTHORITATIVE point: db_del returns 1 only for
     * the caller that actually removed it. Gate the on-disk rm, the count
     * decrement and the owner-index removal on that, so a backend failure
     * propagates and two racing deletes can't both decrement the count. */
    char k[40];
    snprintf(k, sizeof(k), "repo:%u", repo_id);
    struct picomesh_int_result del = sharded_storage_db_del(&h.c, h.obj, hdrs, GIT_REPO_CTX, k);
    if (PICOMESH_IS_ERR(del)) return PICOMESH_ERR(picomesh_int, "git_repo_delete: row delete failed", del);
    if (del.value == 0) return PICOMESH_OK(picomesh_int, 0);  /* another caller removed it first */

    /* Names from the metadata row tell us exactly which on-disk dir to drop. */
    char path[1024];
    if (repo_dir_build(rec.owner_name, rec.repo_name, path, sizeof(path)) == 0) {
        if (rm_rf(path) != 0 && errno != ENOENT)
            ywarn("git_repo: rm_rf(%s) failed: %s", path, strerror(errno));
    }

    struct picomesh_int64_result cdec = gr_incr(&h, hdrs, "count", -1);
    if (PICOMESH_IS_ERR(cdec)) return PICOMESH_ERR(picomesh_int, "git_repo_delete: count update failed", cdec);
    char full_del[192];
    snprintf(full_del, sizeof(full_del), "%s/%s", rec.owner_name, rec.repo_name);
    struct picomesh_void_result orem = owner_list_remove(&h, hdrs, rec.owner_id, full_del);
    if (PICOMESH_IS_ERR(orem)) return PICOMESH_ERR(picomesh_int, "git_repo_delete: owner index update failed", orem);
    char nsk[200];
    ns_index_key(nsk, sizeof(nsk), rec.owner_name);
    struct picomesh_void_result nsrem = repo_index_remove(&h, hdrs, nsk, rec.repo_name);
    if (PICOMESH_IS_ERR(nsrem)) return PICOMESH_ERR(picomesh_int, "git_repo_delete: namespace index update failed", nsrem);
    return PICOMESH_OK(picomesh_int, 1);
}

PICOMESH_CLASS_ANNOTATE("override@git_repo:git_repo:git_repo_owner_of")
struct picomesh_uint32_result git_repo_git_repo_owner_of_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                        uint32_t repo_id)
{
    (void)ctx; (void)obj;
    struct gr_storage_result sr = gr_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_uint32, "git_repo_owner_of: storage open failed", sr);
    struct gr_storage h = sr.value;
    struct repo_rec rec;
    struct picomesh_int_result lr = repo_load(&h, hdrs, repo_id, &rec);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_uint32, "git_repo_owner_of: load failed", lr);
    return PICOMESH_OK(picomesh_uint32, lr.value ? rec.owner_id : 0);
}

/* The repo's owning NAMESPACE PATH (issue #30). For a personal repo this is the
 * owner's username (`alice`); for a group repo it is the group path
 * (`acme/platform`). The policy authorizer resolves repo_id → this path, then
 * computes the caller's inherited namespace role against it. Returns "" for an
 * unknown repo (the authorizer fails such a lookup closed). */
PICOMESH_CLASS_ANNOTATE("override@git_repo:git_repo:git_repo_namespace_of")
struct picomesh_string_result git_repo_git_repo_namespace_of_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                                  uint32_t repo_id)
{
    (void)ctx; (void)obj;
    struct gr_storage_result sr = gr_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_string, "git_repo_namespace_of: storage open failed", sr);
    struct gr_storage h = sr.value;
    struct repo_rec rec;
    struct picomesh_int_result lr = repo_load(&h, hdrs, repo_id, &rec);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_string, "git_repo_namespace_of: load failed", lr);
    char *out = strdup(lr.value ? rec.owner_name : "");
    if (!out) return PICOMESH_ERR(picomesh_string, "git_repo_namespace_of: out of memory");
    return PICOMESH_OK(picomesh_string, out);
}

PICOMESH_CLASS_ANNOTATE("override@git_repo:git_repo:git_repo_count_for_owner")
struct picomesh_size_result git_repo_git_repo_count_for_owner_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                             uint32_t owner_id)
{
    (void)ctx; (void)obj;
    if (!repo_caller_may_list(hdrs, owner_id))
        return PICOMESH_ERR(picomesh_size, "git_repo_count_for_owner: forbidden (cannot list another namespace)");
    struct gr_storage_result sr = gr_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_size, "git_repo_count_for_owner: storage open failed", sr);
    struct gr_storage h = sr.value;
    struct picomesh_string_result lr = owner_list_get(&h, hdrs, owner_id);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_size, "git_repo_count_for_owner: read failed", lr);
    char *list = lr.value;
    size_t n = 0;
    for (const char *p = list; p && *p; ++p) if (*p == '\n') n++;
    free(list);
    return PICOMESH_OK(picomesh_size, n);
}

PICOMESH_CLASS_ANNOTATE("override@git_repo:git_repo:git_repo_count_total")
struct picomesh_size_result git_repo_git_repo_count_total_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct gr_storage_result sr = gr_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_size, "git_repo_count_total: storage open failed", sr);
    struct gr_storage h = sr.value;
    struct picomesh_int64_result cr = gr_count_get(&h, hdrs);
    if (PICOMESH_IS_ERR(cr)) return PICOMESH_ERR(picomesh_size, "git_repo_count_total: read failed", cr);
    return PICOMESH_OK(picomesh_size, (size_t)cr.value);
}

/* List the repo names owned by `owner_id`, newline-separated (empty
 * string if none). The caller (gateway namespace/repos pages) splits on
 * '\n' and renders each as a link — without this there was no way to
 * enumerate a user's repos by NAME, so created repos never showed up in
 * any listing. Heap string; the caller owns and frees it (picomesh_string
 * contract). */
PICOMESH_CLASS_ANNOTATE("override@git_repo:git_repo:git_repo_list_for_owner")
struct picomesh_string_result git_repo_git_repo_list_for_owner_impl(struct ctx *ctx, struct object *obj,
                                                              struct yheaders *hdrs, uint32_t owner_id)
{
    (void)ctx; (void)obj;
    if (!repo_caller_may_list(hdrs, owner_id))
        return PICOMESH_ERR(picomesh_string, "git_repo_list_for_owner: forbidden (cannot list another namespace)");
    struct gr_storage_result sr = gr_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_string, "git_repo_list_for_owner: storage open failed", sr);
    struct gr_storage h = sr.value;
    /* The owner index is already the newline-joined name list the caller
     * wants — hand it back verbatim (empty string when the user owns none).
     * A backend read failure is propagated, never shown as "no repos". */
    struct picomesh_string_result lr = owner_list_get(&h, hdrs, owner_id);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_string, "git_repo_list_for_owner: read failed", lr);
    char *list = lr.value;
    if (!list) {
        list = strdup("");
        if (!list) return PICOMESH_ERR(picomesh_string, "git_repo_list_for_owner: out of memory");
    }
    return PICOMESH_OK(picomesh_string, list);
}

/* The repo names owned by NAMESPACE PATH `path`, newline-separated (issue #30).
 * This is the namespace-based discovery the webapp namespace page uses, so a
 * group's repos (filed under the creator's uid in the owner index) are still
 * found by path. Read authz: reporter+ on the namespace (inherited) or a site
 * admin — fail closed. */
PICOMESH_CLASS_ANNOTATE("override@git_repo:git_repo:git_repo_list_for_namespace")
struct picomesh_string_result git_repo_git_repo_list_for_namespace_impl(struct ctx *ctx, struct object *obj,
                                                                        struct yheaders *hdrs, const char *path)
{
    (void)ctx; (void)obj;
    if (!path_ok(path)) return PICOMESH_ERR(picomesh_string, "git_repo_list_for_namespace: invalid namespace path");
    if (!repo_caller_has_role(hdrs, path, "reporter"))
        return PICOMESH_ERR(picomesh_string, "git_repo_list_for_namespace: forbidden (insufficient namespace role)");
    struct gr_storage_result sr = gr_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_string, "git_repo_list_for_namespace: storage open failed", sr);
    struct gr_storage h = sr.value;
    char nsk[200];
    ns_index_key(nsk, sizeof(nsk), path);
    struct picomesh_string_result lr = repo_index_get(&h, hdrs, nsk);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_string, "git_repo_list_for_namespace: read failed", lr);
    char *list = lr.value ? lr.value : strdup("");
    if (!list) return PICOMESH_ERR(picomesh_string, "git_repo_list_for_namespace: out of memory");
    return PICOMESH_OK(picomesh_string, list);
}

/* Count of repos owned by NAMESPACE PATH `path` (same authz as the listing). */
PICOMESH_CLASS_ANNOTATE("override@git_repo:git_repo:git_repo_count_for_namespace")
struct picomesh_size_result git_repo_git_repo_count_for_namespace_impl(struct ctx *ctx, struct object *obj,
                                                                      struct yheaders *hdrs, const char *path)
{
    (void)ctx; (void)obj;
    if (!path_ok(path)) return PICOMESH_ERR(picomesh_size, "git_repo_count_for_namespace: invalid namespace path");
    if (!repo_caller_has_role(hdrs, path, "reporter"))
        return PICOMESH_ERR(picomesh_size, "git_repo_count_for_namespace: forbidden (insufficient namespace role)");
    struct gr_storage_result sr = gr_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_size, "git_repo_count_for_namespace: storage open failed", sr);
    struct gr_storage h = sr.value;
    char nsk[200];
    ns_index_key(nsk, sizeof(nsk), path);
    struct picomesh_string_result lr = repo_index_get(&h, hdrs, nsk);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_size, "git_repo_count_for_namespace: read failed", lr);
    char *list = lr.value;
    size_t n = 0;
    for (const char *p = list; p && *p; ++p) if (*p == '\n') n++;
    free(list);
    return PICOMESH_OK(picomesh_size, n);
}

/* ---- tree/blob/commit public methods ------------------------------ */

/* List a directory in the repo tree. `ref` defaults to HEAD; `path` ""
 * is the root. Returns "<type>\t<name>\n" lines (type ∈ tree|blob); an
 * empty repo yields an empty string. */
PICOMESH_CLASS_ANNOTATE("override@git_repo:git_repo:git_repo_read_tree")
struct picomesh_string_result git_repo_git_repo_read_tree_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                            uint32_t repo_id, const char *ref, const char *path)
{
    (void)ctx; (void)obj;
    struct gr_storage_result sr = gr_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_string, "git_repo_read_tree: storage open failed", sr);
    struct gr_storage h = sr.value;
    struct repo_rec rec;
    struct picomesh_int_result lr = repo_load(&h, hdrs, repo_id, &rec);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_string, "git_repo_read_tree: load failed", lr);
    if (lr.value == 0) return PICOMESH_ERR(picomesh_string, "git_repo_read_tree: no such repo");
    /* Read authz (resource-level, namespace RBAC — issue #30): a public repo is
     * world-readable; a private one needs at least `reporter` on the repo's
     * owning namespace (inherited from parent namespaces), or a site admin.
     * Identity + roles come from the verified JWT the gateway placed in the
     * headers, not a bare uid. */
    if (!rec.is_public && !repo_caller_has_role(hdrs, rec.owner_name, "reporter"))
        return PICOMESH_ERR(picomesh_string, "git_repo_read_tree: forbidden (insufficient namespace role)");
    if (!ensure_libgit2()) return PICOMESH_ERR(picomesh_string, "git_repo_read_tree: libgit2 init failed");

    struct git_read_work w;
    memset(&w, 0, sizeof(w));
    if (repo_dir_build(rec.owner_name, rec.repo_name, w.repo_path, sizeof(w.repo_path)) != 0)
        return PICOMESH_ERR(picomesh_string, "git_repo_read_tree: path too long");
    snprintf(w.ref, sizeof(w.ref), "%s", ref ? ref : "");
    snprintf(w.path, sizeof(w.path), "%s", path ? path : "");

    gr_run(w.repo_path, git_read_tree_work_fn, &w);
    if (w.rc != 0 || !w.out) { free(w.out); return PICOMESH_ERR(picomesh_string, "git_repo_read_tree: not a directory or git error"); }
    return PICOMESH_OK(picomesh_string, w.out);
}

/* Read a file's contents from the repo tree. `ref` defaults to HEAD.
 * Text-oriented: the result is NUL-terminated, so an embedded NUL in a
 * binary blob would truncate downstream consumers. */
PICOMESH_CLASS_ANNOTATE("override@git_repo:git_repo:git_repo_read_file")
struct picomesh_string_result git_repo_git_repo_read_file_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                            uint32_t repo_id, const char *ref, const char *path)
{
    (void)ctx; (void)obj;
    if (!path || !*path) return PICOMESH_ERR(picomesh_string, "git_repo_read_file: path required");
    struct gr_storage_result sr = gr_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_string, "git_repo_read_file: storage open failed", sr);
    struct gr_storage h = sr.value;
    struct repo_rec rec;
    struct picomesh_int_result lr = repo_load(&h, hdrs, repo_id, &rec);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_string, "git_repo_read_file: load failed", lr);
    if (lr.value == 0) return PICOMESH_ERR(picomesh_string, "git_repo_read_file: no such repo");
    /* Same read authz as read_tree: public → anyone, private → reporter+ on the
     * repo's namespace (inherited) or a site admin. */
    if (!rec.is_public && !repo_caller_has_role(hdrs, rec.owner_name, "reporter"))
        return PICOMESH_ERR(picomesh_string, "git_repo_read_file: forbidden (insufficient namespace role)");
    if (!ensure_libgit2()) return PICOMESH_ERR(picomesh_string, "git_repo_read_file: libgit2 init failed");

    struct git_read_work w;
    memset(&w, 0, sizeof(w));
    if (repo_dir_build(rec.owner_name, rec.repo_name, w.repo_path, sizeof(w.repo_path)) != 0)
        return PICOMESH_ERR(picomesh_string, "git_repo_read_file: path too long");
    snprintf(w.ref, sizeof(w.ref), "%s", ref ? ref : "");
    snprintf(w.path, sizeof(w.path), "%s", path);

    gr_run(w.repo_path, git_read_file_work_fn, &w);
    if (w.rc != 0 || !w.out) { free(w.out); return PICOMESH_ERR(picomesh_string, "git_repo_read_file: not a file or git error"); }
    return PICOMESH_OK(picomesh_string, w.out);
}

/* Create or overwrite a file at `path` (nested dirs are created in the
 * tree as needed — git has no standalone mkdir) and commit it on the
 * branch HEAD points at. Works on an empty repo (first commit). Returns
 * the new commit's hex id. */
PICOMESH_CLASS_ANNOTATE("override@git_repo:git_repo:git_repo_put_file")
struct picomesh_string_result git_repo_git_repo_put_file_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                           uint32_t repo_id, const char *path, const char *content,
                                                           const char *message, const char *author_name,
                                                           const char *author_email)
{
    (void)ctx; (void)obj;
    if (!path || !*path) return PICOMESH_ERR(picomesh_string, "git_repo_put_file: path required");
    struct gr_storage_result sr = gr_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_string, "git_repo_put_file: storage open failed", sr);
    struct gr_storage h = sr.value;
    struct repo_rec rec;
    struct picomesh_int_result lr = repo_load(&h, hdrs, repo_id, &rec);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_string, "git_repo_put_file: load failed", lr);
    if (lr.value == 0) return PICOMESH_ERR(picomesh_string, "git_repo_put_file: no such repo");
    /* Write authz (resource-level namespace RBAC, on top of the gateway policy
     * gate — issue #30): the verified caller must hold at least `developer` on
     * the repo's owning namespace (inherited from parent namespaces), OR be a
     * site admin. Roles come from the signed JWT claims the gateway placed in
     * the headers, never a bare uid; anonymous is always refused. */
    if (!repo_caller_has_role(hdrs, rec.owner_name, "developer"))
        return PICOMESH_ERR(picomesh_string, "git_repo_put_file: forbidden (insufficient namespace role)");
    if (!ensure_libgit2()) return PICOMESH_ERR(picomesh_string, "git_repo_put_file: libgit2 init failed");

    struct git_put_work w;
    memset(&w, 0, sizeof(w));
    if (repo_dir_build(rec.owner_name, rec.repo_name, w.repo_path, sizeof(w.repo_path)) != 0)
        return PICOMESH_ERR(picomesh_string, "git_repo_put_file: path too long");
    snprintf(w.path, sizeof(w.path), "%s", path);
    w.content = content ? content : "";
    w.content_len = content ? strlen(content) : 0;
    snprintf(w.message, sizeof(w.message), "%s", message ? message : "");
    snprintf(w.author_name, sizeof(w.author_name), "%s", author_name ? author_name : "");
    snprintf(w.author_email, sizeof(w.author_email), "%s", author_email ? author_email : "");

    gr_run(w.repo_path, git_put_file_work_fn, &w);
    if (w.rc != 0 || !w.out_oid[0]) return PICOMESH_ERR(picomesh_string, "git_repo_put_file: git error");
    char *oid = strdup(w.out_oid);
    if (!oid) return PICOMESH_ERR(picomesh_string, "git_repo_put_file: out of memory");
    return PICOMESH_OK(picomesh_string, oid);
}

/* ---- visibility (public/private flag) ----------------------------- */

/* Read the repo's visibility: 1 = public, 0 = private. No authz — the
 * flag itself isn't a secret (whether a repo exists/its name already
 * leaks via list_for_owner), and the gateway needs it to decide how to
 * render. The CONTENTS stay gated by read_tree/read_file. Returns 0 for
 * an unknown repo (treated as not-public). */
PICOMESH_CLASS_ANNOTATE("override@git_repo:git_repo:git_repo_is_public")
struct picomesh_int_result git_repo_git_repo_is_public_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                         uint32_t repo_id)
{
    (void)ctx; (void)obj;
    struct gr_storage_result sr = gr_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_int, "git_repo_is_public: storage open failed", sr);
    struct gr_storage h = sr.value;
    struct repo_rec rec;
    struct picomesh_int_result lr = repo_load(&h, hdrs, repo_id, &rec);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_int, "git_repo_is_public: load failed", lr);
    return PICOMESH_OK(picomesh_int, (lr.value && rec.is_public) ? 1 : 0);
}

/* Set the repo's visibility (1 = public, 0 = private). Owner-only;
 * anonymous (uid 0) is refused. Returns 1 on success. */
PICOMESH_CLASS_ANNOTATE("override@git_repo:git_repo:git_repo_set_public")
struct picomesh_int_result git_repo_git_repo_set_public_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                          uint32_t repo_id, int is_public)
{
    (void)ctx; (void)obj;
    struct gr_storage_result sr = gr_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_int, "git_repo_set_public: storage open failed", sr);
    struct gr_storage h = sr.value;
    struct repo_rec rec;
    struct picomesh_int_result lr = repo_load(&h, hdrs, repo_id, &rec);
    if (PICOMESH_IS_ERR(lr)) return PICOMESH_ERR(picomesh_int, "git_repo_set_public: load failed", lr);
    if (lr.value == 0) return PICOMESH_ERR(picomesh_int, "git_repo_set_public: no such repo");
    /* Changing visibility is a maintainer-grade operation on the repo's
     * namespace (inherited), or a site admin (issue #30). */
    if (!repo_caller_has_role(hdrs, rec.owner_name, "maintainer"))
        return PICOMESH_ERR(picomesh_int, "git_repo_set_public: forbidden (insufficient namespace role)");
    rec.is_public = is_public ? 1 : 0;
    struct picomesh_void_result w = repo_store_row(&h, hdrs, repo_id, &rec);
    if (PICOMESH_IS_ERR(w)) return PICOMESH_ERR(picomesh_int, "git_repo_set_public: write failed", w);
    yinfo("git_repo: repo=%u visibility -> %s", repo_id, rec.is_public ? "public" : "private");
    return PICOMESH_OK(picomesh_int, 1);
}

/* List ALL repos' stored entries as a JSON array (gh#15): the generic
 * "show every object" the console renders as a table — not per-owner, not a
 * count. Delegates to the storage namespace scan. */
PICOMESH_CLASS_ANNOTATE("override@git_repo:git_repo:git_repo_list")
struct picomesh_json_result git_repo_git_repo_list_impl(struct ctx *ctx, struct object *obj,
                                                     struct yheaders *hdrs,
                                                     int64_t offset, int64_t limit)
{
    (void)ctx; (void)obj;
    struct gr_storage_result sr = gr_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_json, "git_repo_list: storage open failed", sr);
    struct gr_storage h = sr.value;
    return sharded_storage_db_list(&h.c, h.obj, hdrs, GIT_REPO_CTX, "repo:", offset, limit);
}

/* Unbounded variant — every repo. Use with care on large deployments. */
PICOMESH_CLASS_ANNOTATE("override@git_repo:git_repo:git_repo_list_all")
struct picomesh_json_result git_repo_git_repo_list_all_impl(struct ctx *ctx, struct object *obj,
                                                            struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    struct gr_storage_result sr = gr_open();
    if (PICOMESH_IS_ERR(sr)) return PICOMESH_ERR(picomesh_json, "git_repo_list_all: storage open failed", sr);
    struct gr_storage h = sr.value;
    return sharded_storage_db_list_all(&h.c, h.obj, hdrs, GIT_REPO_CTX, "repo:");
}

#include "store.gen.c"
