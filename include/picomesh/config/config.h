/* config — hierarchical YAML configuration.
 *
 * Port of yaapp's `lib/config.py`. Same precedence + same lookup
 * semantics; libyaml under the hood.
 *
 * Precedence, lowest to highest (later entries override earlier
 * entries for the same key):
 *
 *   1. defaults provided at create time (a dict, by convention the
 *      CLI `--config K=V` overrides — they're applied first, then
 *      every file can override them; this matches the yaapp call
 *      `Config.create(defaults=config_overrides, path=config_file)`).
 *   2. $HOME/.config/picomesh/picoforge.yaml
 *   3. <git_repo_root>/picoforge.yaml
 *   4. ./picoforge.yaml
 *   5. explicit path passed to config_create.
 *
 * String values are post-processed with ${VAR} / ${VAR:default}
 * environment-variable substitution, applied recursively.
 *
 * Lookups use dot-notation paths (`server.host`, `storage.backend`).
 * If a key is missing in a nested subtree, the lookup falls back to
 * the parent dict at the *root* level — yaapp's
 * "ConfigNode inheritance". E.g. `storage.host` will return the
 * top-level `host` if `storage` doesn't have one.
 *
 * Every fallible entry point returns a Result. */

#ifndef PICOMESH_CONFIG_CONFIG_H
#define PICOMESH_CONFIG_CONFIG_H

#include <picomesh/core/result.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct config;
struct config_node;

PICOMESH_RESULT_DECLARE(config_ptr, struct config *);
PICOMESH_RESULT_DECLARE(config_node_ptr, const struct config_node *);

enum config_kind {
    CONFIG_NULL,
    CONFIG_BOOL,
    CONFIG_INT,
    CONFIG_FLOAT,
    CONFIG_STRING,
    CONFIG_LIST,
    CONFIG_MAP,
};

struct config_create_args {
    /* Optional explicit config file (highest precedence). */
    const char *config_file;

    /* Optional list of "key=value" CLI overrides (lowest precedence —
     * applied as defaults, then every file can override). */
    const char *const *cli_overrides;
    size_t cli_override_count;

    /* App name decides the XDG path: ~/.config/<app_name>/<app_name>.yaml
     * and the project-root filename (<app_name>.yaml). Defaults to "picomesh"
     * when NULL. */
    const char *app_name;

    /* Skip the host-filesystem search (XDG / project / cwd). Useful for
     * unit tests that want a hermetic config from `defaults` + an explicit
     * `config_file` only. */
    int no_filesystem_search;
};

struct config_ptr_result config_create(const struct config_create_args *args);
void config_destroy(struct config *c);

/* --- lookups --------------------------------------------------------- */

/* Return the root node (the whole config tree). Owned by the config
 * instance — do not free. */
const struct config_node *config_root(const struct config *c);

/* Resolve a dot-path against the root. Inheritance kicks in: when
 * `<head>.<rest>` is missing, retry as `<rest>` against the root.
 * Returns NULL inside the result (not an error) when the path is not
 * present anywhere. Prefer the typed getters below for new code. */
struct config_node_ptr_result config_get(const struct config *c, const char *dot_path);

/* Optional dot-path lookup: the node, or NULL when absent (NULL is the
 * "default" — absence is not an error here). Non-Result by design. */
const struct config_node *config_get_node(const struct config *c, const char *dot_path);

/* REQUIRED dot-path lookup: a missing key IS an error and the cause chain says
 * which key. Use this when there is no sensible default. */
struct config_node_ptr_result config_require(const struct config *c, const char *dot_path);

/* Default-aware scalar getters. Because the caller supplies a fallback, a
 * missing key is not an error — these return the value (or the fallback)
 * directly, no Result. For a required value with no default, use
 * config_require and read the node. */
const char *config_get_string(const struct config *c, const char *dot_path, const char *fallback);
int64_t config_get_int(const struct config *c, const char *dot_path, int64_t fallback);
int config_get_bool(const struct config *c, const char *dot_path, int fallback);

/* Sub-tree shortcut for plugins:
 *
 *   const struct config_node *sub = config_section(c, "storage");
 *
 * Returns the top-level `<name>` subtree or NULL if absent. Equivalent
 * to `yaapp_engine.get_config('<name>')`. */
const struct config_node *config_section(const struct config *c, const char *name);

/* Deep-merge the subtree at `dot_path` ONTO the root. After this,
 * every key inside that subtree also resolves at the root, with
 * subtree values winning over any pre-existing root values.
 *
 * The engine uses this for the "service projection" — once a process
 * knows it's running as service X (via --name X), it promotes
 * `mesh.services.X.config` onto the root so plugins see their config
 * at natural paths (`storage.db_path` rather than the long form).
 *
 * No-op if the path is missing or not a map. Returns Ok in either
 * case; only out-of-memory failures error out. */
struct picomesh_void_result config_promote_subtree(struct config *c, const char *dot_path);

/* --- typed accessors ------------------------------------------------- */

enum config_kind config_node_kind(const struct config_node *n);
size_t config_node_size(const struct config_node *n); /* map/list count */

/* Scalar getters. They walk the inheritance chain only when called
 * via config_get; once you have a node, they read straight off it.
 * Each returns the supplied `fallback` for kind mismatch / NULL node. */
const char *config_node_as_string(const struct config_node *n, const char *fallback);
int64_t config_node_as_int(const struct config_node *n, int64_t fallback);
double config_node_as_float(const struct config_node *n, double fallback);
int config_node_as_bool(const struct config_node *n, int fallback);

/* Map iteration — invokes cb for each (key, value) pair in source
 * order. Stop early by returning non-zero from cb. */
int config_node_for_each(const struct config_node *n,
                          int (*cb)(const char *key, const struct config_node *val, void *ud),
                          void *ud);

/* Look up `key` directly in a MAP node (no dot-path traversal, so the key may
 * itself contain dots — e.g. a policy keyed by "service.class.method"). Returns
 * the child node or NULL when `n` is not a map or the key is absent. */
const struct config_node *config_node_get(const struct config_node *n, const char *key);

/* List iteration. */
const struct config_node *config_node_at(const struct config_node *n, size_t idx);

/* Pretty-print for debugging. Returns the number of bytes written (no
 * trailing NUL). */
size_t config_dump(const struct config *c, char *buf, size_t bufsize);

/* Serialize a single node (any kind) to `buf` as re-parseable YAML/JSON
 * flow text — the same emitter `config_dump` uses for the whole tree.
 * Used by the mesh to split its per-node `config:` subtrees into
 * standalone node config files. NUL-terminated; returns bytes written. */
size_t config_node_dump(const struct config_node *n, char *buf, size_t bufsize);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_CONFIG_CONFIG_H */
