/* config — hierarchical YAML configuration.
 *
 * The interesting bits (and where the design is documented):
 *   - config_node       in-memory tree shape (scalar / list / map).
 *   - parse_yaml_file    libyaml event stream → config_node *.
 *   - merge_into         deep merge: maps merge key-by-key, scalars
 *                        and lists replace.
 *   - subst_env          recursive ${VAR} / ${VAR:default} expansion.
 *   - lookup_dot         dot-path resolver with parent-chain fallback.
 *
 * Lifetime: one `config` owns its root node and every string it
 * allocated. Callers borrow `config_node *` from the public API. */

#include <picomesh/config/config.h>
#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>

#include <yaml.h>

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct kv {
    char *key;
    struct config_node *value;
};

struct config_node {
    enum config_kind kind;
    union {
        int        b;
        int64_t    i;
        double     f;
        char      *s;
        struct {
            struct config_node **items;
            size_t count;
            size_t cap;
        } list;
        struct {
            struct kv *entries;
            size_t count;
            size_t cap;
        } map;
    } u;
};

struct config {
    struct config_node *root;
};

/* ---------------- node alloc / free -------------------------------- */

static struct config_node *node_new(enum config_kind kind)
{
    struct config_node *node = calloc(1, sizeof(*node));
    if (node) node->kind = kind;
    return node;
}

static void node_free(struct config_node *node);

static void map_free(struct config_node *node)
{
    for (size_t i = 0; i < node->u.map.count; ++i) {
        free(node->u.map.entries[i].key);
        node_free(node->u.map.entries[i].value);
    }
    free(node->u.map.entries);
}

static void list_free(struct config_node *node)
{
    for (size_t i = 0; i < node->u.list.count; ++i) {
        node_free(node->u.list.items[i]);
    }
    free(node->u.list.items);
}

static void node_free(struct config_node *node)
{
    if (!node) return;
    switch (node->kind) {
    case CONFIG_STRING: free(node->u.s); break;
    case CONFIG_LIST:   list_free(node); break;
    case CONFIG_MAP:    map_free(node);  break;
    default: break;
    }
    free(node);
}

static struct config_node *node_new_string(const char *str)
{
    struct config_node *node = node_new(CONFIG_STRING);
    if (!node) return NULL;
    node->u.s = strdup(str ? str : "");
    if (!node->u.s) { free(node); return NULL; }
    return node;
}

static int map_set(struct config_node *map, const char *key, struct config_node *value)
{
    /* Replace if exists. */
    for (size_t i = 0; i < map->u.map.count; ++i) {
        if (strcmp(map->u.map.entries[i].key, key) == 0) {
            node_free(map->u.map.entries[i].value);
            map->u.map.entries[i].value = value;
            return 0;
        }
    }
    if (map->u.map.count == map->u.map.cap) {
        size_t new_cap = map->u.map.cap ? map->u.map.cap * 2 : 8;
        struct kv *new_entries = realloc(map->u.map.entries, new_cap * sizeof(*new_entries));
        if (!new_entries) return -1;
        map->u.map.entries = new_entries;
        map->u.map.cap = new_cap;
    }
    map->u.map.entries[map->u.map.count].key = strdup(key);
    if (!map->u.map.entries[map->u.map.count].key) return -1;
    map->u.map.entries[map->u.map.count].value = value;
    map->u.map.count++;
    return 0;
}

static struct config_node *map_get(const struct config_node *map, const char *key)
{
    if (!map || map->kind != CONFIG_MAP) return NULL;
    for (size_t i = 0; i < map->u.map.count; ++i) {
        if (strcmp(map->u.map.entries[i].key, key) == 0) {
            return map->u.map.entries[i].value;
        }
    }
    return NULL;
}

static int list_push(struct config_node *list, struct config_node *item)
{
    if (list->u.list.count == list->u.list.cap) {
        size_t new_cap = list->u.list.cap ? list->u.list.cap * 2 : 8;
        struct config_node **new_items = realloc(list->u.list.items, new_cap * sizeof(*new_items));
        if (!new_items) return -1;
        list->u.list.items = new_items;
        list->u.list.cap = new_cap;
    }
    list->u.list.items[list->u.list.count++] = item;
    return 0;
}

/* ---------------- scalar parsing ----------------------------------- */

static int parse_int64(const char *str, int64_t *out)
{
    if (!str || !*str) return -1;
    char *end;
    errno = 0;
    long long value = strtoll(str, &end, 10);
    if (errno || *end != '\0') return -1;
    *out = (int64_t)value;
    return 0;
}

static int parse_float(const char *str, double *out)
{
    if (!str || !*str) return -1;
    char *end;
    errno = 0;
    double value = strtod(str, &end);
    if (errno || *end != '\0') return -1;
    *out = value;
    return 0;
}

/* Implicit-tag scalar typing — same rough rules as YAML 1.1 core
 * schema. libyaml hands us the plain/quoted style; we promote unquoted
 * scalars to bool/int/float when their text matches. */
static struct config_node *node_from_scalar(const yaml_event_t *ev)
{
    const char *val = (const char *)ev->data.scalar.value;
    size_t len = ev->data.scalar.length;
    int quoted = ev->data.scalar.style == YAML_SINGLE_QUOTED_SCALAR_STYLE ||
                 ev->data.scalar.style == YAML_DOUBLE_QUOTED_SCALAR_STYLE;
    /* Plain (unquoted) scalars get type promotion. Quoted always = string. */
    if (!quoted) {
        if (len == 0 || (len == 1 && val[0] == '~') ||
            (len == 4 && strncmp(val, "null", 4) == 0) ||
            (len == 4 && strncmp(val, "Null", 4) == 0) ||
            (len == 4 && strncmp(val, "NULL", 4) == 0)) {
            return node_new(CONFIG_NULL);
        }
        if ((len == 4 && (strncmp(val, "true", 4) == 0 || strncmp(val, "True", 4) == 0)) ||
            (len == 3 && (strncmp(val, "yes",  3) == 0 || strncmp(val, "Yes",  3) == 0)) ||
            (len == 2 && (strncmp(val, "on",   2) == 0 || strncmp(val, "On",   2) == 0))) {
            struct config_node *node = node_new(CONFIG_BOOL);
            if (node) node->u.b = 1;
            return node;
        }
        if ((len == 5 && (strncmp(val, "false", 5) == 0 || strncmp(val, "False", 5) == 0)) ||
            (len == 2 && (strncmp(val, "no",   2) == 0 || strncmp(val, "No",   2) == 0))) {
            struct config_node *node = node_new(CONFIG_BOOL);
            if (node) node->u.b = 0;
            return node;
        }
        char buf[64];
        if (len < sizeof(buf)) {
            memcpy(buf, val, len);
            buf[len] = 0;
            int64_t int_value;
            double float_value;
            if (parse_int64(buf, &int_value) == 0) {
                struct config_node *node = node_new(CONFIG_INT);
                if (node) node->u.i = int_value;
                return node;
            }
            if (parse_float(buf, &float_value) == 0) {
                struct config_node *node = node_new(CONFIG_FLOAT);
                if (node) node->u.f = float_value;
                return node;
            }
        }
    }
    struct config_node *node = node_new(CONFIG_STRING);
    if (!node) return NULL;
    node->u.s = malloc(len + 1);
    if (!node->u.s) { free(node); return NULL; }
    memcpy(node->u.s, val, len);
    node->u.s[len] = 0;
    return node;
}

/* ---------------- libyaml event-driven parser ---------------------- */
/*
 * Stack-based event consumer. STREAM-START / DOCUMENT-START open the
 * outermost mapping/sequence; nested MAPPING-START / SEQUENCE-START
 * push containers; SCALAR / END-events fill them in. Keys are pulled
 * out as the "previous scalar" — the standard event-stream pattern. */

struct frame {
    struct config_node *container; /* MAP or LIST */
    char *pending_key;              /* MAP only: NULL when expecting a key */
};

static int push_frame(struct frame **stack, size_t *depth, size_t *cap,
                      struct config_node *container)
{
    if (*depth == *cap) {
        size_t new_cap = *cap ? *cap * 2 : 16;
        struct frame *new_stack = realloc(*stack, new_cap * sizeof(*new_stack));
        if (!new_stack) return -1;
        *stack = new_stack;
        *cap = new_cap;
    }
    (*stack)[*depth].container = container;
    (*stack)[*depth].pending_key = NULL;
    (*depth)++;
    return 0;
}

static int attach(struct frame *top, struct config_node *child)
{
    if (top->container->kind == CONFIG_LIST) {
        return list_push(top->container, child);
    }
    /* MAP: alternate key / value. */
    if (!top->pending_key) {
        if (child->kind != CONFIG_STRING) {
            ywarn("config: non-string map key, coercing");
            char buf[64];
            switch (child->kind) {
            case CONFIG_INT:   snprintf(buf, sizeof(buf), "%" PRId64, child->u.i); break;
            case CONFIG_FLOAT: snprintf(buf, sizeof(buf), "%g", child->u.f); break;
            case CONFIG_BOOL:  snprintf(buf, sizeof(buf), "%s", child->u.b ? "true" : "false"); break;
            default:            snprintf(buf, sizeof(buf), "<key>"); break;
            }
            top->pending_key = strdup(buf);
        } else {
            top->pending_key = strdup(child->u.s);
        }
        node_free(child);
        return top->pending_key ? 0 : -1;
    }
    int result_code = map_set(top->container, top->pending_key, child);
    free(top->pending_key);
    top->pending_key = NULL;
    return result_code;
}

static struct config_node *parse_stream(yaml_parser_t *parser)
{
    struct config_node *root = NULL;
    struct frame *stack = NULL;
    size_t depth = 0, cap = 0;
    yaml_event_t event;

    /* Wait for the first STREAM-START, then proceed until STREAM-END.
     * Multiple documents are not supported — the first one wins, the
     * rest are warned about. */
    int saw_doc = 0;
    int result_code = -1;

    for (;;) {
        if (!yaml_parser_parse(parser, &event)) {
            yerror("config: yaml parser error: %s",
                   parser->problem ? parser->problem : "(unknown)");
            goto out;
        }

        switch (event.type) {
        case YAML_STREAM_START_EVENT:
            break;
        case YAML_DOCUMENT_START_EVENT:
            if (saw_doc) {
                ywarn("config: multi-doc YAML — using first only");
            }
            break;
        case YAML_DOCUMENT_END_EVENT:
            saw_doc = 1;
            break;
        case YAML_STREAM_END_EVENT:
            result_code = 0;
            yaml_event_delete(&event);
            goto out;

        case YAML_MAPPING_START_EVENT: {
            struct config_node *map = node_new(CONFIG_MAP);
            if (!map) goto err;
            if (!root) root = map;
            else if (attach(&stack[depth - 1], map) < 0) goto err;
            if (push_frame(&stack, &depth, &cap, map) < 0) goto err;
            break;
        }
        case YAML_SEQUENCE_START_EVENT: {
            struct config_node *list = node_new(CONFIG_LIST);
            if (!list) goto err;
            if (!root) root = list;
            else if (attach(&stack[depth - 1], list) < 0) goto err;
            if (push_frame(&stack, &depth, &cap, list) < 0) goto err;
            break;
        }
        case YAML_MAPPING_END_EVENT:
        case YAML_SEQUENCE_END_EVENT:
            if (depth == 0) {
                ywarn("config: container end without matching start");
                goto err;
            }
            free(stack[depth - 1].pending_key);
            depth--;
            break;

        case YAML_SCALAR_EVENT: {
            struct config_node *scalar = node_from_scalar(&event);
            if (!scalar) goto err;
            if (!root) {
                root = scalar;
            } else if (attach(&stack[depth - 1], scalar) < 0) {
                node_free(scalar);
                goto err;
            }
            break;
        }

        case YAML_ALIAS_EVENT:
        case YAML_NO_EVENT:
        default:
            break;
        }
        yaml_event_delete(&event);
        continue;
err:
        yaml_event_delete(&event);
        node_free(root);
        root = NULL;
        goto out;
    }
out:
    while (depth > 0) {
        free(stack[--depth].pending_key);
    }
    free(stack);
    if (result_code != 0 && root) {
        node_free(root);
        root = NULL;
    }
    return root;
}

static struct config_node *parse_yaml_file(const char *path)
{
    FILE *file_handle = fopen(path, "rb");
    if (!file_handle) {
        ydebug("config: %s: %s", path, strerror(errno));
        return NULL;
    }
    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        fclose(file_handle);
        yerror("config: yaml_parser_initialize failed");
        return NULL;
    }
    yaml_parser_set_input_file(&parser, file_handle);
    struct config_node *root = parse_stream(&parser);
    yaml_parser_delete(&parser);
    fclose(file_handle);
    /* yaapp's convention: an empty file parses to {} (empty map), not NULL.
     * Match that so downstream merge code can rely on a map at top level. */
    if (!root) {
        root = node_new(CONFIG_MAP);
    } else if (root->kind != CONFIG_MAP) {
        ywarn("config: %s: top-level is not a map (got kind=%d), ignoring", path, root->kind);
        node_free(root);
        root = node_new(CONFIG_MAP);
    }
    return root;
}

/* ---------------- merge -------------------------------------------- */

static struct config_node *node_clone(const struct config_node *src);

static struct config_node *clone_map(const struct config_node *map)
{
    struct config_node *out = node_new(CONFIG_MAP);
    if (!out) return NULL;
    for (size_t i = 0; i < map->u.map.count; ++i) {
        struct config_node *value = node_clone(map->u.map.entries[i].value);
        if (!value) { node_free(out); return NULL; }
        if (map_set(out, map->u.map.entries[i].key, value) < 0) {
            node_free(value);
            node_free(out);
            return NULL;
        }
    }
    return out;
}

static struct config_node *clone_list(const struct config_node *list)
{
    struct config_node *out = node_new(CONFIG_LIST);
    if (!out) return NULL;
    for (size_t i = 0; i < list->u.list.count; ++i) {
        struct config_node *value = node_clone(list->u.list.items[i]);
        if (!value) { node_free(out); return NULL; }
        if (list_push(out, value) < 0) { node_free(value); node_free(out); return NULL; }
    }
    return out;
}

static struct config_node *node_clone(const struct config_node *src)
{
    if (!src) return NULL;
    switch (src->kind) {
    case CONFIG_NULL:   return node_new(CONFIG_NULL);
    case CONFIG_BOOL: {
        struct config_node *node = node_new(CONFIG_BOOL); if (node) node->u.b = src->u.b; return node;
    }
    case CONFIG_INT: {
        struct config_node *node = node_new(CONFIG_INT); if (node) node->u.i = src->u.i; return node;
    }
    case CONFIG_FLOAT: {
        struct config_node *node = node_new(CONFIG_FLOAT); if (node) node->u.f = src->u.f; return node;
    }
    case CONFIG_STRING:
        return node_new_string(src->u.s);
    case CONFIG_LIST:
        return clone_list(src);
    case CONFIG_MAP:
        return clone_map(src);
    }
    return NULL;
}

/* Deep-merge `over` into `base`. Both must be maps. Sub-maps recurse;
 * everything else replaces. Consumes neither side (callers own them). */
static int merge_into(struct config_node *base, const struct config_node *over)
{
    if (!base || !over || base->kind != CONFIG_MAP || over->kind != CONFIG_MAP) return -1;
    for (size_t i = 0; i < over->u.map.count; ++i) {
        const char *key = over->u.map.entries[i].key;
        const struct config_node *value = over->u.map.entries[i].value;
        struct config_node *existing = map_get(base, key);
        if (existing && existing->kind == CONFIG_MAP && value->kind == CONFIG_MAP) {
            if (merge_into(existing, value) < 0) return -1;
        } else {
            struct config_node *clone = node_clone(value);
            if (!clone) return -1;
            if (map_set(base, key, clone) < 0) {
                node_free(clone);
                return -1;
            }
        }
    }
    return 0;
}

/* ---------------- env var substitution ----------------------------- */

/* Walk every string node in the tree and replace ${VAR} / ${VAR:default}.
 * `${VAR}` with no default and unset env → empty string (matches yaapp).
 * Substitution is non-recursive on the replacement string. */
static char *subst_env_str(const char *src)
{
    size_t cap = strlen(src) + 32;
    char *out = malloc(cap);
    if (!out) return NULL;
    size_t out_len = 0;

    for (const char *cursor = src; *cursor;) {
        if (cursor[0] == '$' && cursor[1] == '{') {
            const char *close_brace = strchr(cursor + 2, '}');
            if (!close_brace) { out[out_len++] = *cursor++; continue; }
            const char *name_start = cursor + 2;
            const char *colon = NULL;
            for (const char *scan = name_start; scan < close_brace; ++scan) {
                if (*scan == ':') { colon = scan; break; }
            }
            char name[128];
            const char *default_start = "";
            size_t name_len;
            if (colon) {
                name_len = (size_t)(colon - name_start);
                default_start = colon + 1;
                /* default_start runs to `close_brace` */
            } else {
                name_len = (size_t)(close_brace - name_start);
            }
            if (name_len >= sizeof(name)) { out[out_len++] = *cursor++; continue; }
            memcpy(name, name_start, name_len);
            name[name_len] = 0;
            const char *env_value = getenv(name);
            const char *replacement;
            char def_buf[256];
            if (env_value) {
                replacement = env_value;
            } else if (colon) {
                size_t default_len = (size_t)(close_brace - default_start);
                if (default_len >= sizeof(def_buf)) default_len = sizeof(def_buf) - 1;
                memcpy(def_buf, default_start, default_len);
                def_buf[default_len] = 0;
                replacement = def_buf;
            } else {
                replacement = "";
            }
            size_t rep_len = strlen(replacement);
            while (out_len + rep_len + 1 > cap) {
                size_t new_cap = cap * 2;
                char *new_out = realloc(out, new_cap);
                if (!new_out) { free(out); return NULL; }
                out = new_out;
                cap = new_cap;
            }
            memcpy(out + out_len, replacement, rep_len);
            out_len += rep_len;
            cursor = close_brace + 1;
        } else {
            if (out_len + 1 >= cap) {
                size_t new_cap = cap * 2;
                char *new_out = realloc(out, new_cap);
                if (!new_out) { free(out); return NULL; }
                out = new_out;
                cap = new_cap;
            }
            out[out_len++] = *cursor++;
        }
    }
    out[out_len] = 0;
    return out;
}

static void subst_env(struct config_node *node)
{
    if (!node) return;
    switch (node->kind) {
    case CONFIG_STRING: {
        char *expanded = subst_env_str(node->u.s);
        if (expanded) {
            free(node->u.s);
            node->u.s = expanded;
        }
        break;
    }
    case CONFIG_LIST:
        for (size_t i = 0; i < node->u.list.count; ++i) subst_env(node->u.list.items[i]);
        break;
    case CONFIG_MAP:
        for (size_t i = 0; i < node->u.map.count; ++i) subst_env(node->u.map.entries[i].value);
        break;
    default: break;
    }
}

/* ---------------- "key=value" CLI overrides → map merge ------------ */

/* Parse a `dotted.key=raw_value` into a one-entry nested map and merge
 * it into `root`. `raw_value` is treated as a scalar — env-subst will
 * run later. The split point is the first `=`. */
/* Type a CLI-override value the way an unquoted YAML scalar would be typed, so
 * `--config a.b=8091` yields an INT (matching `b: 8091` in the file) instead of
 * the string "8091" that numeric consumers (port checks, etc.) reject. Mirrors
 * node_from_scalar's promotion rules for plain scalars. */
static struct config_node *node_from_cli_value(const char *val)
{
    if (!val || !*val) return node_new(CONFIG_NULL);
    if (strcmp(val, "~") == 0 || strcmp(val, "null") == 0 ||
        strcmp(val, "Null") == 0 || strcmp(val, "NULL") == 0)
        return node_new(CONFIG_NULL);
    if (strcmp(val, "true") == 0 || strcmp(val, "True") == 0 ||
        strcmp(val, "yes") == 0 || strcmp(val, "Yes") == 0 ||
        strcmp(val, "on") == 0 || strcmp(val, "On") == 0) {
        struct config_node *node = node_new(CONFIG_BOOL);
        if (node) node->u.b = 1;
        return node;
    }
    if (strcmp(val, "false") == 0 || strcmp(val, "False") == 0 ||
        strcmp(val, "no") == 0 || strcmp(val, "No") == 0 ||
        strcmp(val, "off") == 0 || strcmp(val, "Off") == 0) {
        struct config_node *node = node_new(CONFIG_BOOL);
        if (node) node->u.b = 0;
        return node;
    }
    int64_t int_value;
    double float_value;
    if (parse_int64(val, &int_value) == 0) {
        struct config_node *node = node_new(CONFIG_INT);
        if (node) node->u.i = int_value;
        return node;
    }
    if (parse_float(val, &float_value) == 0) {
        struct config_node *node = node_new(CONFIG_FLOAT);
        if (node) node->u.f = float_value;
        return node;
    }
    return node_new_string(val);
}

static int apply_cli_override(struct config_node *root, const char *kv)
{
    const char *equals = strchr(kv, '=');
    if (!equals) {
        ywarn("config: --config '%s' has no '=' — ignoring", kv);
        return 0;
    }
    size_t key_len = (size_t)(equals - kv);
    if (key_len == 0) {
        ywarn("config: empty key in --config '%s' — ignoring", kv);
        return 0;
    }
    char path[256];
    if (key_len >= sizeof(path)) {
        ywarn("config: --config key too long, ignoring");
        return 0;
    }
    memcpy(path, kv, key_len);
    path[key_len] = 0;

    /* Build {part1: {part2: ... : value}}. */
    const char *rest = path;
    struct config_node *leaf = node_from_cli_value(equals + 1);
    if (!leaf) return -1;
    struct config_node *current = leaf;

    /* Reverse-build: tokenise then wrap from innermost out. */
    char *tokens[16];
    size_t tcount = 0;
    char buf[256];
    size_t rest_len = strlen(rest);
    if (rest_len >= sizeof(buf)) { node_free(leaf); return 0; }
    memcpy(buf, rest, rest_len + 1);
    for (char *tok = strtok(buf, "."); tok; tok = strtok(NULL, ".")) {
        if (tcount >= sizeof(tokens) / sizeof(tokens[0])) break;
        tokens[tcount++] = tok;
    }
    for (size_t i = tcount; i-- > 0; ) {
        struct config_node *map = node_new(CONFIG_MAP);
        if (!map) { node_free(current); return -1; }
        if (map_set(map, tokens[i], current) < 0) {
            node_free(map); node_free(current); return -1;
        }
        current = map;
    }
    int result_code = merge_into(root, current);
    node_free(current);
    return result_code;
}

/* ---------------- filesystem search -------------------------------- */

static int file_exists(const char *path)
{
    struct stat st;
    return path && *path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static char *xdg_path(const char *app_name)
{
    const char *home = getenv("HOME");
    if (!home) return NULL;
    size_t len = strlen(home) + strlen(app_name) * 2 + 32;
    char *path = malloc(len);
    if (!path) return NULL;
    snprintf(path, len, "%s/.config/%s/%s.yaml", home, app_name, app_name);
    return path;
}

static char *git_root_path(const char *app_name)
{
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) return NULL;
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s", cwd);
    for (;;) {
        char dotgit[4200];
        snprintf(dotgit, sizeof(dotgit), "%s/.git", buf);
        struct stat st;
        if (stat(dotgit, &st) == 0) {
            char *cfg = malloc(strlen(buf) + strlen(app_name) + 16);
            if (!cfg) return NULL;
            sprintf(cfg, "%s/%s.yaml", buf, app_name);
            return cfg;
        }
        char *slash = strrchr(buf, '/');
        if (!slash || slash == buf) break;
        *slash = 0;
    }
    return NULL;
}

/* ---------------- public create / destroy -------------------------- */

struct config_ptr_result config_create(const struct config_create_args *args)
{
    const char *app = (args && args->app_name) ? args->app_name : "picomesh";

    struct config *config = calloc(1, sizeof(*config));
    if (!config) return PICOMESH_ERR(config_ptr, "config_create: calloc failed");
    config->root = node_new(CONFIG_MAP);
    if (!config->root) {
        free(config);
        return PICOMESH_ERR(config_ptr, "config_create: root alloc failed");
    }

    /* Precedence, low → high: filesystem search (XDG, git-root, cwd), then the
     * explicit --config-file, then --config CLI overrides applied LAST (below).
     * A `--config key=value` therefore truly OVERRIDES the file — what its name
     * ("config override") promises, and what per-instance overrides need (e.g.
     * a second mesh started with `--config mesh.nodes_dir=/tmp/picoforge2/nodes`).
     * yaapp applied these as defaults instead, which cannot override a file. */
    if (!args || !args->no_filesystem_search) {
        /* 1. XDG. */
        char *xdg = xdg_path(app);
        if (xdg && file_exists(xdg)) {
            ydebug("config: loading XDG %s", xdg);
            struct config_node *file = parse_yaml_file(xdg);
            if (file) { merge_into(config->root, file); node_free(file); }
        }
        free(xdg);

        /* 2. Git repo root. */
        char *git_root = git_root_path(app);
        if (git_root && file_exists(git_root)) {
            ydebug("config: loading git-root %s", git_root);
            struct config_node *file = parse_yaml_file(git_root);
            if (file) { merge_into(config->root, file); node_free(file); }
        }
        free(git_root);

        /* 3. cwd. */
        char cwd_path[64];
        snprintf(cwd_path, sizeof(cwd_path), "%s.yaml", app);
        if (file_exists(cwd_path)) {
            ydebug("config: loading cwd %s", cwd_path);
            struct config_node *file = parse_yaml_file(cwd_path);
            if (file) { merge_into(config->root, file); node_free(file); }
        }
    }

    /* 4. Explicit --config-file (overrides the filesystem search). */
    if (args && args->config_file && file_exists(args->config_file)) {
        ydebug("config: loading explicit %s", args->config_file);
        struct config_node *file = parse_yaml_file(args->config_file);
        if (file) { merge_into(config->root, file); node_free(file); }
    }

    /* 5. --config CLI overrides — applied LAST, so they win over every file.
     *    This is what makes one base config + per-instance `--config a.b=c`
     *    overrides work. */
    if (args && args->cli_overrides) {
        for (size_t i = 0; i < args->cli_override_count; ++i) {
            if (apply_cli_override(config->root, args->cli_overrides[i]) < 0) {
                config_destroy(config);
                return PICOMESH_ERR(config_ptr, "config_create: cli override merge failed");
            }
        }
    }

    /* Env-var substitution happens last so every layer's strings get
     * expanded uniformly. */
    subst_env(config->root);
    return PICOMESH_OK(config_ptr, config);
}

void config_destroy(struct config *config)
{
    if (!config) return;
    node_free(config->root);
    free(config);
}

const struct config_node *config_root(const struct config *config)
{
    return config ? config->root : NULL;
}

struct picomesh_void_result config_promote_subtree(struct config *config, const char *dot_path)
{
    if (!config || !dot_path) {
        return PICOMESH_ERR(picomesh_void, "config_promote_subtree: NULL args");
    }
    char buf[256];
    size_t path_len = strlen(dot_path);
    if (path_len >= sizeof(buf)) {
        return PICOMESH_ERR(picomesh_void, "config_promote_subtree: path too long");
    }
    memcpy(buf, dot_path, path_len + 1);

    const struct config_node *subtree = config->root;
    for (char *tok = strtok(buf, "."); tok; tok = strtok(NULL, ".")) {
        if (!subtree || subtree->kind != CONFIG_MAP) {
            subtree = NULL;
            break;
        }
        subtree = map_get(subtree, tok);
    }
    /* Subtree absent or not a map → no projection, but not an error.
     * The caller (engine) projects optimistically; missing entries
     * just mean "no service-local config to flatten". */
    if (!subtree || subtree->kind != CONFIG_MAP) {
        return PICOMESH_OK_VOID();
    }
    if (merge_into(config->root, subtree) < 0) {
        return PICOMESH_ERR(picomesh_void, "config_promote_subtree: merge failed");
    }
    return PICOMESH_OK_VOID();
}

/* ---------------- dot-path lookup ---------------------------------- */

static const struct config_node *walk_path(const struct config_node *node, const char *path)
{
    if (!node || !path) return NULL;
    char buf[256];
    size_t len = strlen(path);
    if (len >= sizeof(buf)) return NULL;
    memcpy(buf, path, len + 1);

    const struct config_node *current = node;
    for (char *tok = strtok(buf, "."); tok; tok = strtok(NULL, ".")) {
        if (!current || current->kind != CONFIG_MAP) return NULL;
        current = map_get(current, tok);
        if (!current) return NULL;
    }
    return current;
}

/* Resolve a dot-path against the root, with one-level inheritance fallback
 * (`storage.host` → `host`). Returns NULL when the path is absent everywhere.
 * The single non-Result resolution primitive the public getters are built on. */
static const struct config_node *resolve_path(const struct config *config, const char *dot_path)
{
    if (!config || !dot_path) return NULL;
    const struct config_node *direct = walk_path(config->root, dot_path);
    if (direct) return direct;

    /* Inheritance fallback: peel the first segment and retry against the
     * root. So `storage.host` falls back to `host` if `storage` doesn't
     * define one. This walks once — multi-level fallback would mean
     * `a.b.c` → `b.c` → `c`, which yaapp's ConfigNode supports. Repeat. */
    char buf[256];
    size_t len = strlen(dot_path);
    if (len >= sizeof(buf)) return NULL;
    memcpy(buf, dot_path, len + 1);
    char *dot = strchr(buf, '.');
    while (dot) {
        const struct config_node *hit = walk_path(config->root, dot + 1);
        if (hit) return hit;
        dot = strchr(dot + 1, '.');
    }
    return NULL;
}

struct config_node_ptr_result config_get(const struct config *config, const char *dot_path)
{
    if (!config || !dot_path) {
        return PICOMESH_ERR(config_node_ptr, "config_get: NULL args");
    }
    return PICOMESH_OK(config_node_ptr, resolve_path(config, dot_path));
}

const struct config_node *config_get_node(const struct config *config, const char *dot_path)
{
    return resolve_path(config, dot_path);
}

struct config_node_ptr_result config_require(const struct config *config, const char *dot_path)
{
    if (!config || !dot_path)
        return PICOMESH_ERR(config_node_ptr, "config_require: NULL args");
    const struct config_node *node = resolve_path(config, dot_path);
    if (!node) {
        char msg[300];
        snprintf(msg, sizeof(msg), "config: required key '%s' is missing", dot_path);
        return PICOMESH_ERR(config_node_ptr, msg);
    }
    return PICOMESH_OK(config_node_ptr, node);
}

/* Default-aware scalar getters: a missing key is NOT an error — the caller
 * supplied a fallback — so these return the value directly (no Result). Only a
 * required value (no fallback) errors on absence; that path is config_require. */
const char *config_get_string(const struct config *config, const char *dot_path, const char *fallback)
{
    return config_node_as_string(resolve_path(config, dot_path), fallback);
}

int64_t config_get_int(const struct config *config, const char *dot_path, int64_t fallback)
{
    return config_node_as_int(resolve_path(config, dot_path), fallback);
}

int config_get_bool(const struct config *config, const char *dot_path, int fallback)
{
    return config_node_as_bool(resolve_path(config, dot_path), fallback);
}

const struct config_node *config_section(const struct config *config, const char *name)
{
    return map_get(config ? config->root : NULL, name);
}

/* ---------------- typed accessors --------------------------------- */

enum config_kind config_node_kind(const struct config_node *node)
{
    return node ? node->kind : CONFIG_NULL;
}

size_t config_node_size(const struct config_node *node)
{
    if (!node) return 0;
    if (node->kind == CONFIG_MAP)  return node->u.map.count;
    if (node->kind == CONFIG_LIST) return node->u.list.count;
    return 0;
}

const char *config_node_as_string(const struct config_node *node, const char *fallback)
{
    return (node && node->kind == CONFIG_STRING) ? node->u.s : fallback;
}

int64_t config_node_as_int(const struct config_node *node, int64_t fallback)
{
    if (!node) return fallback;
    if (node->kind == CONFIG_INT)   return node->u.i;
    if (node->kind == CONFIG_FLOAT) return (int64_t)node->u.f;
    if (node->kind == CONFIG_BOOL)  return (int64_t)node->u.b;
    if (node->kind == CONFIG_STRING) {
        int64_t value;
        if (parse_int64(node->u.s, &value) == 0) return value;
    }
    return fallback;
}

double config_node_as_float(const struct config_node *node, double fallback)
{
    if (!node) return fallback;
    if (node->kind == CONFIG_FLOAT) return node->u.f;
    if (node->kind == CONFIG_INT)   return (double)node->u.i;
    if (node->kind == CONFIG_STRING) {
        double value;
        if (parse_float(node->u.s, &value) == 0) return value;
    }
    return fallback;
}

int config_node_as_bool(const struct config_node *node, int fallback)
{
    if (!node) return fallback;
    if (node->kind == CONFIG_BOOL) return node->u.b;
    if (node->kind == CONFIG_INT)  return node->u.i != 0;
    if (node->kind == CONFIG_STRING) {
        const char *str = node->u.s;
        if (!str || !*str) return fallback;
        if (strcasecmp(str, "true") == 0 || strcasecmp(str, "yes") == 0 ||
            strcasecmp(str, "on") == 0 || strcmp(str, "1") == 0) return 1;
        if (strcasecmp(str, "false") == 0 || strcasecmp(str, "no") == 0 ||
            strcasecmp(str, "off") == 0 || strcmp(str, "0") == 0) return 0;
    }
    return fallback;
}

int config_node_for_each(const struct config_node *node,
                          int (*cb)(const char *key, const struct config_node *val, void *ud),
                          void *ud)
{
    if (!node || node->kind != CONFIG_MAP || !cb) return 0;
    for (size_t i = 0; i < node->u.map.count; ++i) {
        int result_code = cb(node->u.map.entries[i].key, node->u.map.entries[i].value, ud);
        if (result_code) return result_code;
    }
    return 0;
}

const struct config_node *config_node_at(const struct config_node *node, size_t idx)
{
    if (!node || node->kind != CONFIG_LIST || idx >= node->u.list.count) return NULL;
    return node->u.list.items[idx];
}

const struct config_node *config_node_get(const struct config_node *node, const char *key)
{
    if (!node || node->kind != CONFIG_MAP || !key) return NULL;
    for (size_t i = 0; i < node->u.map.count; ++i)
        if (strcmp(node->u.map.entries[i].key, key) == 0) return node->u.map.entries[i].value;
    return NULL;
}

/* ---------------- pretty-print ------------------------------------ */

static int dump_node(const struct config_node *n, char *buf, size_t cap, size_t off, int indent);

static int append(char *buf, size_t cap, size_t off, const char *str)
{
    size_t len = strlen(str);
    if (off + len >= cap) return (int)off;
    memcpy(buf + off, str, len);
    return (int)(off + len);
}

static int dump_indent(char *buf, size_t cap, size_t off, int indent)
{
    for (int i = 0; i < indent && off + 1 < cap; ++i) {
        buf[off++] = ' '; buf[off++] = ' ';
    }
    return (int)off;
}

static int dump_node(const struct config_node *node, char *buf, size_t cap, size_t off, int indent)
{
    if (!node) return (int)off;
    char tmp[64];
    switch (node->kind) {
    case CONFIG_NULL:   off = (size_t)append(buf, cap, off, "null"); break;
    case CONFIG_BOOL:   off = (size_t)append(buf, cap, off, node->u.b ? "true" : "false"); break;
    case CONFIG_INT:    snprintf(tmp, sizeof(tmp), "%" PRId64, node->u.i); off = (size_t)append(buf, cap, off, tmp); break;
    case CONFIG_FLOAT:  snprintf(tmp, sizeof(tmp), "%g", node->u.f);       off = (size_t)append(buf, cap, off, tmp); break;
    case CONFIG_STRING: off = (size_t)append(buf, cap, off, "\"");
                         off = (size_t)append(buf, cap, off, node->u.s);
                         off = (size_t)append(buf, cap, off, "\"");
                         break;
    case CONFIG_LIST:
        off = (size_t)append(buf, cap, off, "[");
        for (size_t i = 0; i < node->u.list.count; ++i) {
            if (i) off = (size_t)append(buf, cap, off, ", ");
            off = (size_t)dump_node(node->u.list.items[i], buf, cap, off, indent);
        }
        off = (size_t)append(buf, cap, off, "]");
        break;
    case CONFIG_MAP:
        off = (size_t)append(buf, cap, off, "{\n");
        for (size_t i = 0; i < node->u.map.count; ++i) {
            off = (size_t)dump_indent(buf, cap, off, indent + 1);
            off = (size_t)append(buf, cap, off, node->u.map.entries[i].key);
            off = (size_t)append(buf, cap, off, ": ");
            off = (size_t)dump_node(node->u.map.entries[i].value, buf, cap, off, indent + 1);
            off = (size_t)append(buf, cap, off, ",\n");
        }
        off = (size_t)dump_indent(buf, cap, off, indent);
        off = (size_t)append(buf, cap, off, "}");
        break;
    }
    return (int)off;
}

size_t config_dump(const struct config *config, char *buf, size_t bufsize)
{
    if (!buf || bufsize == 0) return 0;
    int dumped_len = dump_node(config ? config->root : NULL, buf, bufsize, 0, 0);
    if ((size_t)dumped_len >= bufsize) dumped_len = (int)bufsize - 1;
    buf[dumped_len] = 0;
    return (size_t)dumped_len;
}

size_t config_node_dump(const struct config_node *node, char *buf, size_t bufsize)
{
    if (!buf || bufsize == 0) return 0;
    int written_len = dump_node(node, buf, bufsize, 0, 0);
    if ((size_t)written_len >= bufsize) written_len = (int)bufsize - 1;
    buf[written_len] = 0;
    return (size_t)written_len;
}
