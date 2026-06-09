/* `policy` authorizer — role-based access control from an endpoint policy
 * table. Default-deny, the GitLab role ladder, account resolution against the
 * call args, the `anonymous`/`authenticated` shortcuts, and the
 * `site:maintainer+` bypass. Port of the yaapp policy authorizer.
 *
 * Config:
 *   authorizer:
 *     type: policy
 *     policy:
 *       _describe: anonymous
 *       accounts.accounts.list: { required_role: owner, account_from: site }
 *       git_repo.git_repo.read_tree:
 *         { required_role: reporter, account_from: "{kwargs.name:account}" }
 *
 * A bare string is shorthand: `foo: owner` == `{required_role: owner,
 * account_from: site}`. Endpoints absent from the table are DENIED. */

#include <picomesh/authorizers/base.h>
#include <picomesh/config/config.h>
#include <picomesh/json/json.h>
#include <picomesh/picoclass/jinvoke.h>
#include <picomesh/engine/engine.h>
#include <picomesh/security/jwt.h>
#include <picomesh/security/jwt_verifier.h>
#include <picomesh/plugin/git_repo/git_repo.h>
#include <picomesh/plugin/issues/issues.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct policy_state {
    struct picomesh_engine *engine;
    const struct config_node *policy; /* the `policy:` map (points into config) */
    struct picomesh_jwt_verifier *verifier;
};

static struct picomesh_void_ptr_result policy_create(struct picomesh_engine *engine,
                                                     const struct config_node *config)
{
    struct policy_state *state = calloc(1, sizeof(*state));
    if (!state) return PICOMESH_ERR(picomesh_void_ptr, "policy: out of memory");
    state->engine = engine;
    state->policy = config_node_get(config, "policy"); /* may be NULL → all default-deny */
    struct picomesh_void_ptr_result verifier = picomesh_jwt_verifier_create(engine);
    if (PICOMESH_IS_ERR(verifier)) { free(state); return PICOMESH_ERR(picomesh_void_ptr, "policy: verifier create failed", verifier); }
    state->verifier = verifier.value;
    return PICOMESH_OK(picomesh_void_ptr, state);
}

static struct picomesh_authz_decision decide(int allowed, int status, const char *reason)
{
    struct picomesh_authz_decision decision = {.allowed = allowed, .status = status};
    snprintf(decision.reason, sizeof(decision.reason), "%s", reason);
    return decision;
}

/* 1 if the claims `groups` array contains the exact membership string `group`
 * (e.g. "site:runner"). Used by `required_group` rules, which gate on an exact
 * group rather than the role ladder. */
static int groups_contains(const struct json_value *groups, const char *group)
{
    if (!groups || !json_is_array(groups) || !group) return 0;
    size_t count = json_array_size(groups);
    for (size_t i = 0; i < count; ++i) {
        const char *entry = json_as_string(json_array_at(groups, i), NULL);
        if (entry && strcmp(entry, group) == 0) return 1;
    }
    return 0;
}

/* Highest role rank held for `account` in the claims `groups` array, or -1. */
static int groups_best_rank(const struct json_value *groups, const char *account)
{
    if (!groups || !json_is_array(groups) || !account) return -1;
    size_t account_len = strlen(account);
    size_t count = json_array_size(groups);
    int best = -1;
    for (size_t i = 0; i < count; ++i) {
        const char *entry = json_as_string(json_array_at(groups, i), NULL);
        if (!entry) continue;
        const char *colon = strchr(entry, ':');
        if (!colon) continue;
        size_t slug_len = (size_t)(colon - entry);
        if (slug_len == account_len && memcmp(entry, account, account_len) == 0) {
            int rank = picomesh_role_rank(colon + 1);
            if (rank > best) best = rank;
        }
    }
    return best;
}

/* Effective role rank for a NAMESPACE PATH against the claims `groups` array,
 * honouring inheritance: the highest direct role on the path OR any ancestor.
 * `acme/platform/api` is checked as itself, then `acme/platform`, then `acme`,
 * so a parent-namespace grant satisfies a child resource (issue #30). */
static int groups_effective_rank(const struct json_value *groups, const char *path)
{
    if (!groups || !path || !*path) return -1;
    char prefix[256];
    int best = -1;
    for (const char *end = path + strlen(path); end > path; ) {
        size_t len = (size_t)(end - path);
        if (len < sizeof(prefix)) {
            memcpy(prefix, path, len);
            prefix[len] = 0;
            int rank = groups_best_rank(groups, prefix);
            if (rank > best) best = rank;
        }
        const char *slash = NULL;
        for (const char *p = end - 1; p >= path; --p)
            if (*p == '/') { slash = p; break; }
        if (!slash) break;
        end = slash;
    }
    return best;
}

/* Resolve `account_from` against the positional `args`, using the method's
 * declared parameter names to honour yaapp's `{kwargs.name}` form. Supports:
 *   site                  -> "site"
 *   {kwargs.NAME}         -> the string value of param NAME
 *   {kwargs.NAME:account} -> that value's account part (before '/')
 *   arg:N                 -> the Nth positional arg (positional escape hatch)
 *   <literal>             -> taken as a literal account slug
 * Returns 1 on success. */
static int resolve_account(const char *account_from, const char *endpoint,
                           const struct json_value *args, char *out, size_t cap)
{
    if (!account_from || !*account_from) return 0;
    if (strcmp(account_from, "site") == 0) { snprintf(out, cap, "site"); return 1; }

    long index = -1;
    int account_part = 0;
    if (strncmp(account_from, "{kwargs.", 8) == 0) {
        const char *field = account_from + 8;
        const char *close = strchr(field, '}');
        if (!close) return 0;
        const char *colon = memchr(field, ':', (size_t)(close - field));
        size_t field_len = colon ? (size_t)(colon - field) : (size_t)(close - field);
        if (colon && strncmp(colon + 1, "account}", 8) == 0) account_part = 1;
        /* Map the param NAME to its positional index via the method's metadata. */
        char qname[192];
        snprintf(qname, sizeof(qname), "%s", endpoint);
        for (char *p = qname; *p; ++p) if (*p == '.') *p = '_';
        const struct jinvoke_params *params = jinvoke_params_for(qname);
        if (!params) return 0;
        for (size_t i = 0; i < params->count; ++i) {
            if (strlen(params->items[i].name) == field_len &&
                strncmp(params->items[i].name, field, field_len) == 0) { index = (long)i; break; }
        }
        if (index < 0) return 0;
    } else if (strncmp(account_from, "arg:", 4) == 0) {
        index = strtol(account_from + 4, NULL, 10);
    } else {
        snprintf(out, cap, "%s", account_from); /* literal slug */
        return 1;
    }

    if (index < 0 || !args || !json_is_array(args)) return 0;
    const struct json_value *node = json_array_at(args, (size_t)index);
    const char *value = json_as_string(node, NULL);
    char numbuf[32];
    if (!value && json_is_int(node)) {
        /* repo_id and other ids arrive as JSON numbers, not strings — stringify
         * so `{kwargs.repo_id}` resolves the same way a string arg would. */
        snprintf(numbuf, sizeof(numbuf), "%lld", (long long)json_as_int(node, 0));
        value = numbuf;
    }
    if (!value || !*value) return 0;
    if (account_part) {
        const char *slash = strchr(value, '/');
        size_t len = slash ? (size_t)(slash - value) : strlen(value);
        if (len >= cap) len = cap - 1;
        memcpy(out, value, len);
        out[len] = 0;
    } else {
        snprintf(out, cap, "%s", value);
    }
    return 1;
}

/* Resolve a repo_id to its owning namespace path by calling
 * git_repo.git_repo.namespace_of over the mesh (issue #30). The gateway, the
 * only node that runs this authorizer, has git_repo as a remote. Returns 1 and
 * fills `out` on success; 0 on any failure, so the caller fails closed. The
 * lookup carries no caller credential — namespace_of returns non-secret repo
 * metadata, like owner_of. */
static struct picomesh_int_result resolve_repo_namespace(struct policy_state *state, uint32_t repo_id, char *out, size_t cap)
{
    struct ctx ctx = picomesh_engine_service_ctx(state->engine, "git_repo");
    struct object_ptr_result create_res = git_repo_git_repo_create(&ctx);
    PICOMESH_RETURN_IF_ERR(picomesh_int, create_res, "resolve_repo_namespace: repo create failed");
    struct picomesh_string_result namespace_res = git_repo_git_repo_namespace_of(&ctx, create_res.value, NULL, repo_id);
    PICOMESH_RETURN_IF_ERR(picomesh_int, namespace_res, "resolve_repo_namespace: namespace_of failed");
    int ok = (namespace_res.value && namespace_res.value[0]) ? 1 : 0;
    if (ok) snprintf(out, cap, "%s", namespace_res.value);
    free(namespace_res.value);
    return PICOMESH_OK(picomesh_int, ok);
}

/* Whether the repo `repo_id` is public, via git_repo.git_repo.is_public.
 * Returns 1 (public), 0 (private/unknown), or -1 on a backend error (the caller
 * treats -1 as not-public, i.e. fails closed to the role check). */
static struct picomesh_int_result resolve_repo_public(struct policy_state *state, uint32_t repo_id)
{
    struct ctx ctx = picomesh_engine_service_ctx(state->engine, "git_repo");
    struct object_ptr_result create_res = git_repo_git_repo_create(&ctx);
    PICOMESH_RETURN_IF_ERR(picomesh_int, create_res, "resolve_repo_public: repo create failed");
    struct picomesh_int_result public_res = git_repo_git_repo_is_public(&ctx, create_res.value, NULL, repo_id);
    PICOMESH_RETURN_IF_ERR(picomesh_int, public_res, "resolve_repo_public: is_public failed");
    return PICOMESH_OK(picomesh_int, public_res.value ? 1 : 0);
}

/* Resolve an issue_id to the namespace path of its repo by chaining
 * issues.repo_of -> git_repo.namespace_of (issue #30). Returns 1 and fills out
 * on success; 0 on any failure (caller fails closed). */
static struct picomesh_int_result resolve_issue_namespace(struct policy_state *state, uint32_t issue_id, char *out, size_t cap)
{
    struct ctx ctx = picomesh_engine_service_ctx(state->engine, "issues");
    struct object_ptr_result create_res = issues_issues_create(&ctx);
    PICOMESH_RETURN_IF_ERR(picomesh_int, create_res, "resolve_issue_namespace: issues create failed");
    struct picomesh_uint32_result repo_of_res = issues_issues_repo_of(&ctx, create_res.value, NULL, issue_id);
    PICOMESH_RETURN_IF_ERR(picomesh_int, repo_of_res, "resolve_issue_namespace: repo_of failed");
    if (repo_of_res.value == 0) return PICOMESH_OK(picomesh_int, 0);
    return resolve_repo_namespace(state, repo_of_res.value, out, cap);
}

static struct picomesh_authz_decision_result policy_authorize(void *state_ptr, const char *endpoint,
                                                       const struct json_value *args, const char *jwt)
{
    struct policy_state *state = state_ptr;

    /* Schema reads collapse to a single policy entry regardless of depth. */
    const char *lookup_key = endpoint;
    size_t endpoint_len = strlen(endpoint);
    if (strcmp(endpoint, "_describe") == 0 ||
        (endpoint_len >= 10 && strcmp(endpoint + endpoint_len - 10, "._describe") == 0))
        lookup_key = "_describe";
    else if (strcmp(endpoint, "_describe_tree") == 0 ||
             (endpoint_len >= 15 && strcmp(endpoint + endpoint_len - 15, "._describe_tree") == 0))
        lookup_key = "_describe_tree";

    const struct config_node *rule = state->policy ? config_node_get(state->policy, lookup_key) : NULL;
    if (!rule)
        return PICOMESH_OK(picomesh_authz_decision, decide(0, 403, "no policy entry for endpoint (default deny)"));

    /* A rule is a bare role string (account_from defaults to "site"), or a map
     * carrying {required_role, account_from} and/or {required_group}.
     *   required_role  — checked against the role ladder for an account.
     *   required_group — an EXACT group membership (e.g. "site:runner"), off
     *                    the ladder, for non-hierarchical roles like runners. */
    const char *required_role = NULL;
    const char *account_from = "site";
    const char *required_group = NULL;
    /* role_scope selects HOW the target of the role check is resolved:
     *   absent / "account"  — `account_from` resolves to a flat slug, checked
     *                         directly against the role ladder (no inheritance);
     *                         used for `site` admin gates.
     *   "repo_namespace"    — `resource_from` resolves to a repo_id; the repo's
     *                         owning namespace path is fetched and the caller's
     *                         INHERITED namespace role is compared.
     *   "namespace_path"    — `resource_from` resolves to a namespace path
     *                         directly; inherited namespace role is compared. */
    const char *role_scope = NULL;
    const char *resource_from = NULL;
    /* site_bypass: opt-in per rule — a site:maintainer+ caller bypasses the role
     * check ONLY for rules that set it (admin/repo-management surfaces), never
     * implicitly for every role-gated endpoint. allow_public: a repo_namespace
     * read this rule guards is permitted for ANYONE (even anonymous) when the
     * target repo is public. */
    int site_bypass = 0;
    int allow_public = 0;
    if (config_node_kind(rule) == CONFIG_MAP) {
        required_role = config_node_as_string(config_node_get(rule, "required_role"), NULL);
        account_from = config_node_as_string(config_node_get(rule, "account_from"), "site");
        required_group = config_node_as_string(config_node_get(rule, "required_group"), NULL);
        role_scope = config_node_as_string(config_node_get(rule, "role_scope"), NULL);
        resource_from = config_node_as_string(config_node_get(rule, "resource_from"), NULL);
        site_bypass = config_node_as_int(config_node_get(rule, "site_bypass"), 0) != 0;
        allow_public = config_node_as_int(config_node_get(rule, "allow_public"), 0) != 0;
    } else {
        required_role = config_node_as_string(rule, NULL);
    }
    int has_role = required_role && *required_role;
    int has_group = required_group && *required_group;
    if (!has_role && !has_group)
        return PICOMESH_OK(picomesh_authz_decision, decide(0, 403, "policy entry has neither required_role nor required_group"));

    if (has_role && strcmp(required_role, "anonymous") == 0)
        return PICOMESH_OK(picomesh_authz_decision, decide(1, 0, "anonymous"));

    /* Public-repo reads are allowed for anyone — including an anonymous caller
     * with no JWT — so this must be decided BEFORE the credential requirement
     * below. Only applies to a repo_namespace rule that opted in via
     * allow_public. */
    if (has_role && allow_public && role_scope && strcmp(role_scope, "repo_namespace") == 0) {
        char id_str[64];
        if (resolve_account(resource_from ? resource_from : account_from, endpoint, args, id_str, sizeof(id_str))) {
            uint32_t repo_id = (uint32_t)strtoul(id_str, NULL, 10);
            struct picomesh_int_result public_res = resolve_repo_public(state, repo_id);
            if (PICOMESH_IS_ERR(public_res)) {
                picomesh_error_print(stderr, "policy: resolve_repo_public", public_res.error);
                picomesh_error_destroy(public_res.error);
            } else if (public_res.value == 1)
                return PICOMESH_OK(picomesh_authz_decision, decide(1, 0, "public repo readable by anyone"));
        }
        /* Not public (or unresolved): fall through to require auth + role. */
    }

    if (!jwt || !*jwt)
        return PICOMESH_OK(picomesh_authz_decision, decide(0, 401, "authentication required"));

    struct picomesh_string_result claims_r = picomesh_jwt_verifier_verify(state->verifier, jwt);
    if (PICOMESH_IS_ERR(claims_r)) {
        /* A token that fails verification is a 401 denial (data), but log the
         * reason chain rather than discarding it silently. */
        picomesh_error_print(stderr, "policy: JWT verification", claims_r.error);
        picomesh_error_destroy(claims_r.error);
        return PICOMESH_OK(picomesh_authz_decision, decide(0, 401, "invalid or expired token"));
    }
    struct json_doc *claims_doc = json_parse(claims_r.value, strlen(claims_r.value));
    free(claims_r.value);
    if (!claims_doc) return PICOMESH_OK(picomesh_authz_decision, decide(0, 401, "token claims not parseable"));
    const struct json_value *groups = json_object_get(json_doc_root(claims_doc), "groups");

    struct picomesh_authz_decision result;

    /* required_group: exact membership. Must hold if specified. */
    if (has_group && !groups_contains(groups, required_group)) {
        result = decide(0, 403, "caller lacks the required group");
        goto done;
    }

    /* required_role: ladder check (skipped when only a group was required). */
    if (has_role) {
        if (strcmp(required_role, "authenticated") == 0) {
            result = decide(1, 0, "authenticated");
            goto done;
        }
        int need = picomesh_role_rank(required_role);
        if (need < 0) { result = decide(0, 403, "policy error: unknown required_role"); goto done; }

        /* Site-level bypass: maintainer+ on the synthetic `site` account — but
         * ONLY for rules that explicitly opt in (site_bypass: true). An admin
         * surface gated on `account_from: site` does not need this; it resolves
         * the role on the site account directly. */
        if (site_bypass && groups_best_rank(groups, "site") >= picomesh_role_rank("maintainer")) {
            result = decide(1, 0, "site-level bypass");
            goto done;
        }

        /* Namespace-scoped checks resolve a repo/namespace and compare the
         * caller's INHERITED role over the namespace tree (issue #30). */
        if (role_scope && strcmp(role_scope, "repo_namespace") == 0) {
            char id_str[64];
            if (!resolve_account(resource_from ? resource_from : account_from, endpoint, args, id_str, sizeof(id_str))) {
                result = decide(0, 403, "could not resolve repo_id from call args");
                goto done;
            }
            uint32_t repo_id = (uint32_t)strtoul(id_str, NULL, 10);
            char ns_path[256];
            struct picomesh_int_result ns_res = resolve_repo_namespace(state, repo_id, ns_path, sizeof(ns_path));
            if (PICOMESH_IS_ERR(ns_res) || !ns_res.value) {
                if (PICOMESH_IS_ERR(ns_res)) {
                    picomesh_error_print(stderr, "policy: resolve namespace", ns_res.error);
                    picomesh_error_destroy(ns_res.error);
                }
                result = decide(0, 403, "could not resolve repo namespace");
                goto done;
            }
            result = groups_effective_rank(groups, ns_path) >= need
                         ? decide(1, 0, "namespace role satisfies requirement")
                         : decide(0, 403, "insufficient namespace role");
            goto done;
        }
        if (role_scope && strcmp(role_scope, "namespace_path") == 0) {
            char ns_path[256];
            if (!resolve_account(resource_from ? resource_from : account_from, endpoint, args, ns_path, sizeof(ns_path))) {
                result = decide(0, 403, "could not resolve namespace path from call args");
                goto done;
            }
            result = groups_effective_rank(groups, ns_path) >= need
                         ? decide(1, 0, "namespace role satisfies requirement")
                         : decide(0, 403, "insufficient namespace role");
            goto done;
        }
        if (role_scope && strcmp(role_scope, "issue_namespace") == 0) {
            char id_str[64];
            if (!resolve_account(resource_from ? resource_from : account_from, endpoint, args, id_str, sizeof(id_str))) {
                result = decide(0, 403, "could not resolve issue_id from call args");
                goto done;
            }
            uint32_t issue_id = (uint32_t)strtoul(id_str, NULL, 10);
            char ns_path[256];
            struct picomesh_int_result ns_res = resolve_issue_namespace(state, issue_id, ns_path, sizeof(ns_path));
            if (PICOMESH_IS_ERR(ns_res) || !ns_res.value) {
                if (PICOMESH_IS_ERR(ns_res)) {
                    picomesh_error_print(stderr, "policy: resolve namespace", ns_res.error);
                    picomesh_error_destroy(ns_res.error);
                }
                result = decide(0, 403, "could not resolve issue namespace");
                goto done;
            }
            result = groups_effective_rank(groups, ns_path) >= need
                         ? decide(1, 0, "namespace role satisfies requirement")
                         : decide(0, 403, "insufficient namespace role");
            goto done;
        }

        char account[160];
        if (!resolve_account(account_from, endpoint, args, account, sizeof(account))) {
            result = decide(0, 403, "could not resolve account from call args");
            goto done;
        }
        result = groups_best_rank(groups, account) >= need
                     ? decide(1, 0, "role on account satisfies requirement")
                     : decide(0, 403, "insufficient role on account");
        goto done;
    }

    /* Only a required_group was specified, and it held. */
    result = decide(1, 0, "caller holds the required group");

done:
    json_doc_free(claims_doc);
    return PICOMESH_OK(picomesh_authz_decision, result);
}

static void policy_destroy(void *state_ptr)
{
    struct policy_state *state = state_ptr;
    if (!state) return;
    picomesh_jwt_verifier_destroy(state->verifier);
    free(state);
}

const struct picomesh_authorizer_ops *picomesh_authorizer_policy_ops(void)
{
    static const struct picomesh_authorizer_ops ops = {
        .type_name = "policy",
        .create = policy_create,
        .authorize = policy_authorize,
        .destroy = policy_destroy,
    };
    return &ops;
}
