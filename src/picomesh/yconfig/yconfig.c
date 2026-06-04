/* yconfig — hierarchical YAML configuration.
 *
 * The interesting bits (and where the design is documented):
 *   - yconfig_node       in-memory tree shape (scalar / list / map).
 *   - parse_yaml_file    libyaml event stream → yconfig_node *.
 *   - merge_into         deep merge: maps merge key-by-key, scalars
 *                        and lists replace.
 *   - subst_env          recursive ${VAR} / ${VAR:default} expansion.
 *   - lookup_dot         dot-path resolver with parent-chain fallback.
 *
 * Lifetime: one `yconfig` owns its root node and every string it
 * allocated. Callers borrow `yconfig_node *` from the public API. */

#include <picomesh/yconfig/yconfig.h>
#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>

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
    struct yconfig_node *value;
};

struct yconfig_node {
    enum yconfig_kind kind;
    union {
        int        b;
        int64_t    i;
        double     f;
        char      *s;
        struct {
            struct yconfig_node **items;
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

struct yconfig {
    struct yconfig_node *root;
};

/* ---------------- node alloc / free -------------------------------- */

static struct yconfig_node *node_new(enum yconfig_kind k)
{
    struct yconfig_node *n = calloc(1, sizeof(*n));
    if (n) n->kind = k;
    return n;
}

static void node_free(struct yconfig_node *n);

static void map_free(struct yconfig_node *n)
{
    for (size_t i = 0; i < n->u.map.count; ++i) {
        free(n->u.map.entries[i].key);
        node_free(n->u.map.entries[i].value);
    }
    free(n->u.map.entries);
}

static void list_free(struct yconfig_node *n)
{
    for (size_t i = 0; i < n->u.list.count; ++i) {
        node_free(n->u.list.items[i]);
    }
    free(n->u.list.items);
}

static void node_free(struct yconfig_node *n)
{
    if (!n) return;
    switch (n->kind) {
    case YCONFIG_STRING: free(n->u.s); break;
    case YCONFIG_LIST:   list_free(n); break;
    case YCONFIG_MAP:    map_free(n);  break;
    default: break;
    }
    free(n);
}

static struct yconfig_node *node_new_string(const char *s)
{
    struct yconfig_node *n = node_new(YCONFIG_STRING);
    if (!n) return NULL;
    n->u.s = strdup(s ? s : "");
    if (!n->u.s) { free(n); return NULL; }
    return n;
}

static int map_set(struct yconfig_node *m, const char *key, struct yconfig_node *value)
{
    /* Replace if exists. */
    for (size_t i = 0; i < m->u.map.count; ++i) {
        if (strcmp(m->u.map.entries[i].key, key) == 0) {
            node_free(m->u.map.entries[i].value);
            m->u.map.entries[i].value = value;
            return 0;
        }
    }
    if (m->u.map.count == m->u.map.cap) {
        size_t ncap = m->u.map.cap ? m->u.map.cap * 2 : 8;
        struct kv *na = realloc(m->u.map.entries, ncap * sizeof(*na));
        if (!na) return -1;
        m->u.map.entries = na;
        m->u.map.cap = ncap;
    }
    m->u.map.entries[m->u.map.count].key = strdup(key);
    if (!m->u.map.entries[m->u.map.count].key) return -1;
    m->u.map.entries[m->u.map.count].value = value;
    m->u.map.count++;
    return 0;
}

static struct yconfig_node *map_get(const struct yconfig_node *m, const char *key)
{
    if (!m || m->kind != YCONFIG_MAP) return NULL;
    for (size_t i = 0; i < m->u.map.count; ++i) {
        if (strcmp(m->u.map.entries[i].key, key) == 0) {
            return m->u.map.entries[i].value;
        }
    }
    return NULL;
}

static int list_push(struct yconfig_node *l, struct yconfig_node *item)
{
    if (l->u.list.count == l->u.list.cap) {
        size_t ncap = l->u.list.cap ? l->u.list.cap * 2 : 8;
        struct yconfig_node **na = realloc(l->u.list.items, ncap * sizeof(*na));
        if (!na) return -1;
        l->u.list.items = na;
        l->u.list.cap = ncap;
    }
    l->u.list.items[l->u.list.count++] = item;
    return 0;
}

/* ---------------- scalar parsing ----------------------------------- */

static int parse_int64(const char *s, int64_t *out)
{
    if (!s || !*s) return -1;
    char *end;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (errno || *end != '\0') return -1;
    *out = (int64_t)v;
    return 0;
}

static int parse_float(const char *s, double *out)
{
    if (!s || !*s) return -1;
    char *end;
    errno = 0;
    double v = strtod(s, &end);
    if (errno || *end != '\0') return -1;
    *out = v;
    return 0;
}

/* Implicit-tag scalar typing — same rough rules as YAML 1.1 core
 * schema. libyaml hands us the plain/quoted style; we promote unquoted
 * scalars to bool/int/float when their text matches. */
static struct yconfig_node *node_from_scalar(const yaml_event_t *ev)
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
            return node_new(YCONFIG_NULL);
        }
        if ((len == 4 && (strncmp(val, "true", 4) == 0 || strncmp(val, "True", 4) == 0)) ||
            (len == 3 && (strncmp(val, "yes",  3) == 0 || strncmp(val, "Yes",  3) == 0)) ||
            (len == 2 && (strncmp(val, "on",   2) == 0 || strncmp(val, "On",   2) == 0))) {
            struct yconfig_node *n = node_new(YCONFIG_BOOL);
            if (n) n->u.b = 1;
            return n;
        }
        if ((len == 5 && (strncmp(val, "false", 5) == 0 || strncmp(val, "False", 5) == 0)) ||
            (len == 2 && (strncmp(val, "no",   2) == 0 || strncmp(val, "No",   2) == 0))) {
            struct yconfig_node *n = node_new(YCONFIG_BOOL);
            if (n) n->u.b = 0;
            return n;
        }
        char buf[64];
        if (len < sizeof(buf)) {
            memcpy(buf, val, len);
            buf[len] = 0;
            int64_t iv;
            double fv;
            if (parse_int64(buf, &iv) == 0) {
                struct yconfig_node *n = node_new(YCONFIG_INT);
                if (n) n->u.i = iv;
                return n;
            }
            if (parse_float(buf, &fv) == 0) {
                struct yconfig_node *n = node_new(YCONFIG_FLOAT);
                if (n) n->u.f = fv;
                return n;
            }
        }
    }
    struct yconfig_node *n = node_new(YCONFIG_STRING);
    if (!n) return NULL;
    n->u.s = malloc(len + 1);
    if (!n->u.s) { free(n); return NULL; }
    memcpy(n->u.s, val, len);
    n->u.s[len] = 0;
    return n;
}

/* ---------------- libyaml event-driven parser ---------------------- */
/*
 * Stack-based event consumer. STREAM-START / DOCUMENT-START open the
 * outermost mapping/sequence; nested MAPPING-START / SEQUENCE-START
 * push containers; SCALAR / END-events fill them in. Keys are pulled
 * out as the "previous scalar" — the standard event-stream pattern. */

struct frame {
    struct yconfig_node *container; /* MAP or LIST */
    char *pending_key;              /* MAP only: NULL when expecting a key */
};

static int push_frame(struct frame **stack, size_t *sp, size_t *cap,
                      struct yconfig_node *c)
{
    if (*sp == *cap) {
        size_t nc = *cap ? *cap * 2 : 16;
        struct frame *ns = realloc(*stack, nc * sizeof(*ns));
        if (!ns) return -1;
        *stack = ns;
        *cap = nc;
    }
    (*stack)[*sp].container = c;
    (*stack)[*sp].pending_key = NULL;
    (*sp)++;
    return 0;
}

static int attach(struct frame *top, struct yconfig_node *child)
{
    if (top->container->kind == YCONFIG_LIST) {
        return list_push(top->container, child);
    }
    /* MAP: alternate key / value. */
    if (!top->pending_key) {
        if (child->kind != YCONFIG_STRING) {
            ywarn("yconfig: non-string map key, coercing");
            char buf[64];
            switch (child->kind) {
            case YCONFIG_INT:   snprintf(buf, sizeof(buf), "%" PRId64, child->u.i); break;
            case YCONFIG_FLOAT: snprintf(buf, sizeof(buf), "%g", child->u.f); break;
            case YCONFIG_BOOL:  snprintf(buf, sizeof(buf), "%s", child->u.b ? "true" : "false"); break;
            default:            snprintf(buf, sizeof(buf), "<key>"); break;
            }
            top->pending_key = strdup(buf);
        } else {
            top->pending_key = strdup(child->u.s);
        }
        node_free(child);
        return top->pending_key ? 0 : -1;
    }
    int rc = map_set(top->container, top->pending_key, child);
    free(top->pending_key);
    top->pending_key = NULL;
    return rc;
}

static struct yconfig_node *parse_stream(yaml_parser_t *parser)
{
    struct yconfig_node *root = NULL;
    struct frame *stack = NULL;
    size_t sp = 0, cap = 0;
    yaml_event_t ev;

    /* Wait for the first STREAM-START, then proceed until STREAM-END.
     * Multiple documents are not supported — the first one wins, the
     * rest are warned about. */
    int saw_doc = 0;
    int rc = -1;

    for (;;) {
        if (!yaml_parser_parse(parser, &ev)) {
            yerror("yconfig: yaml parser error: %s",
                   parser->problem ? parser->problem : "(unknown)");
            goto out;
        }

        switch (ev.type) {
        case YAML_STREAM_START_EVENT:
            break;
        case YAML_DOCUMENT_START_EVENT:
            if (saw_doc) {
                ywarn("yconfig: multi-doc YAML — using first only");
            }
            break;
        case YAML_DOCUMENT_END_EVENT:
            saw_doc = 1;
            break;
        case YAML_STREAM_END_EVENT:
            rc = 0;
            yaml_event_delete(&ev);
            goto out;

        case YAML_MAPPING_START_EVENT: {
            struct yconfig_node *m = node_new(YCONFIG_MAP);
            if (!m) goto err;
            if (!root) root = m;
            else if (attach(&stack[sp - 1], m) < 0) goto err;
            if (push_frame(&stack, &sp, &cap, m) < 0) goto err;
            break;
        }
        case YAML_SEQUENCE_START_EVENT: {
            struct yconfig_node *l = node_new(YCONFIG_LIST);
            if (!l) goto err;
            if (!root) root = l;
            else if (attach(&stack[sp - 1], l) < 0) goto err;
            if (push_frame(&stack, &sp, &cap, l) < 0) goto err;
            break;
        }
        case YAML_MAPPING_END_EVENT:
        case YAML_SEQUENCE_END_EVENT:
            if (sp == 0) {
                ywarn("yconfig: container end without matching start");
                goto err;
            }
            free(stack[sp - 1].pending_key);
            sp--;
            break;

        case YAML_SCALAR_EVENT: {
            struct yconfig_node *s = node_from_scalar(&ev);
            if (!s) goto err;
            if (!root) {
                root = s;
            } else if (attach(&stack[sp - 1], s) < 0) {
                node_free(s);
                goto err;
            }
            break;
        }

        case YAML_ALIAS_EVENT:
        case YAML_NO_EVENT:
        default:
            break;
        }
        yaml_event_delete(&ev);
        continue;
err:
        yaml_event_delete(&ev);
        node_free(root);
        root = NULL;
        goto out;
    }
out:
    while (sp > 0) {
        free(stack[--sp].pending_key);
    }
    free(stack);
    if (rc != 0 && root) {
        node_free(root);
        root = NULL;
    }
    return root;
}

static struct yconfig_node *parse_yaml_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ydebug("yconfig: %s: %s", path, strerror(errno));
        return NULL;
    }
    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        fclose(f);
        yerror("yconfig: yaml_parser_initialize failed");
        return NULL;
    }
    yaml_parser_set_input_file(&parser, f);
    struct yconfig_node *root = parse_stream(&parser);
    yaml_parser_delete(&parser);
    fclose(f);
    /* yaapp's convention: an empty file parses to {} (empty map), not NULL.
     * Match that so downstream merge code can rely on a map at top level. */
    if (!root) {
        root = node_new(YCONFIG_MAP);
    } else if (root->kind != YCONFIG_MAP) {
        ywarn("yconfig: %s: top-level is not a map (got kind=%d), ignoring", path, root->kind);
        node_free(root);
        root = node_new(YCONFIG_MAP);
    }
    return root;
}

/* ---------------- merge -------------------------------------------- */

static struct yconfig_node *node_clone(const struct yconfig_node *src);

static struct yconfig_node *clone_map(const struct yconfig_node *m)
{
    struct yconfig_node *out = node_new(YCONFIG_MAP);
    if (!out) return NULL;
    for (size_t i = 0; i < m->u.map.count; ++i) {
        struct yconfig_node *v = node_clone(m->u.map.entries[i].value);
        if (!v) { node_free(out); return NULL; }
        if (map_set(out, m->u.map.entries[i].key, v) < 0) {
            node_free(v);
            node_free(out);
            return NULL;
        }
    }
    return out;
}

static struct yconfig_node *clone_list(const struct yconfig_node *l)
{
    struct yconfig_node *out = node_new(YCONFIG_LIST);
    if (!out) return NULL;
    for (size_t i = 0; i < l->u.list.count; ++i) {
        struct yconfig_node *v = node_clone(l->u.list.items[i]);
        if (!v) { node_free(out); return NULL; }
        if (list_push(out, v) < 0) { node_free(v); node_free(out); return NULL; }
    }
    return out;
}

static struct yconfig_node *node_clone(const struct yconfig_node *src)
{
    if (!src) return NULL;
    switch (src->kind) {
    case YCONFIG_NULL:   return node_new(YCONFIG_NULL);
    case YCONFIG_BOOL: {
        struct yconfig_node *n = node_new(YCONFIG_BOOL); if (n) n->u.b = src->u.b; return n;
    }
    case YCONFIG_INT: {
        struct yconfig_node *n = node_new(YCONFIG_INT); if (n) n->u.i = src->u.i; return n;
    }
    case YCONFIG_FLOAT: {
        struct yconfig_node *n = node_new(YCONFIG_FLOAT); if (n) n->u.f = src->u.f; return n;
    }
    case YCONFIG_STRING:
        return node_new_string(src->u.s);
    case YCONFIG_LIST:
        return clone_list(src);
    case YCONFIG_MAP:
        return clone_map(src);
    }
    return NULL;
}

/* Deep-merge `over` into `base`. Both must be maps. Sub-maps recurse;
 * everything else replaces. Consumes neither side (callers own them). */
static int merge_into(struct yconfig_node *base, const struct yconfig_node *over)
{
    if (!base || !over || base->kind != YCONFIG_MAP || over->kind != YCONFIG_MAP) return -1;
    for (size_t i = 0; i < over->u.map.count; ++i) {
        const char *k = over->u.map.entries[i].key;
        const struct yconfig_node *v = over->u.map.entries[i].value;
        struct yconfig_node *existing = map_get(base, k);
        if (existing && existing->kind == YCONFIG_MAP && v->kind == YCONFIG_MAP) {
            if (merge_into(existing, v) < 0) return -1;
        } else {
            struct yconfig_node *clone = node_clone(v);
            if (!clone) return -1;
            if (map_set(base, k, clone) < 0) {
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
    size_t off = 0;

    for (const char *p = src; *p;) {
        if (p[0] == '$' && p[1] == '{') {
            const char *end = strchr(p + 2, '}');
            if (!end) { out[off++] = *p++; continue; }
            const char *name_start = p + 2;
            const char *colon = NULL;
            for (const char *q = name_start; q < end; ++q) {
                if (*q == ':') { colon = q; break; }
            }
            char name[128];
            const char *def = "";
            size_t name_len;
            if (colon) {
                name_len = (size_t)(colon - name_start);
                def = colon + 1;
                /* def runs to `end` */
            } else {
                name_len = (size_t)(end - name_start);
            }
            if (name_len >= sizeof(name)) { out[off++] = *p++; continue; }
            memcpy(name, name_start, name_len);
            name[name_len] = 0;
            const char *val = getenv(name);
            const char *rep;
            char def_buf[256];
            if (val) {
                rep = val;
            } else if (colon) {
                size_t dl = (size_t)(end - def);
                if (dl >= sizeof(def_buf)) dl = sizeof(def_buf) - 1;
                memcpy(def_buf, def, dl);
                def_buf[dl] = 0;
                rep = def_buf;
            } else {
                rep = "";
            }
            size_t rep_len = strlen(rep);
            while (off + rep_len + 1 > cap) {
                size_t nc = cap * 2;
                char *no = realloc(out, nc);
                if (!no) { free(out); return NULL; }
                out = no;
                cap = nc;
            }
            memcpy(out + off, rep, rep_len);
            off += rep_len;
            p = end + 1;
        } else {
            if (off + 1 >= cap) {
                size_t nc = cap * 2;
                char *no = realloc(out, nc);
                if (!no) { free(out); return NULL; }
                out = no;
                cap = nc;
            }
            out[off++] = *p++;
        }
    }
    out[off] = 0;
    return out;
}

static void subst_env(struct yconfig_node *n)
{
    if (!n) return;
    switch (n->kind) {
    case YCONFIG_STRING: {
        char *expanded = subst_env_str(n->u.s);
        if (expanded) {
            free(n->u.s);
            n->u.s = expanded;
        }
        break;
    }
    case YCONFIG_LIST:
        for (size_t i = 0; i < n->u.list.count; ++i) subst_env(n->u.list.items[i]);
        break;
    case YCONFIG_MAP:
        for (size_t i = 0; i < n->u.map.count; ++i) subst_env(n->u.map.entries[i].value);
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
static struct yconfig_node *node_from_cli_value(const char *val)
{
    if (!val || !*val) return node_new(YCONFIG_NULL);
    if (strcmp(val, "~") == 0 || strcmp(val, "null") == 0 ||
        strcmp(val, "Null") == 0 || strcmp(val, "NULL") == 0)
        return node_new(YCONFIG_NULL);
    if (strcmp(val, "true") == 0 || strcmp(val, "True") == 0 ||
        strcmp(val, "yes") == 0 || strcmp(val, "Yes") == 0 ||
        strcmp(val, "on") == 0 || strcmp(val, "On") == 0) {
        struct yconfig_node *n = node_new(YCONFIG_BOOL);
        if (n) n->u.b = 1;
        return n;
    }
    if (strcmp(val, "false") == 0 || strcmp(val, "False") == 0 ||
        strcmp(val, "no") == 0 || strcmp(val, "No") == 0 ||
        strcmp(val, "off") == 0 || strcmp(val, "Off") == 0) {
        struct yconfig_node *n = node_new(YCONFIG_BOOL);
        if (n) n->u.b = 0;
        return n;
    }
    int64_t iv;
    double fv;
    if (parse_int64(val, &iv) == 0) {
        struct yconfig_node *n = node_new(YCONFIG_INT);
        if (n) n->u.i = iv;
        return n;
    }
    if (parse_float(val, &fv) == 0) {
        struct yconfig_node *n = node_new(YCONFIG_FLOAT);
        if (n) n->u.f = fv;
        return n;
    }
    return node_new_string(val);
}

static int apply_cli_override(struct yconfig_node *root, const char *kv)
{
    const char *eq = strchr(kv, '=');
    if (!eq) {
        ywarn("yconfig: --config '%s' has no '=' — ignoring", kv);
        return 0;
    }
    size_t klen = (size_t)(eq - kv);
    if (klen == 0) {
        ywarn("yconfig: empty key in --config '%s' — ignoring", kv);
        return 0;
    }
    char path[256];
    if (klen >= sizeof(path)) {
        ywarn("yconfig: --config key too long, ignoring");
        return 0;
    }
    memcpy(path, kv, klen);
    path[klen] = 0;

    /* Build {part1: {part2: ... : value}}. */
    const char *rest = path;
    struct yconfig_node *leaf = node_from_cli_value(eq + 1);
    if (!leaf) return -1;
    struct yconfig_node *cur = leaf;

    /* Reverse-build: tokenise then wrap from innermost out. */
    char *tokens[16];
    size_t tcount = 0;
    char buf[256];
    size_t bl = strlen(rest);
    if (bl >= sizeof(buf)) { node_free(leaf); return 0; }
    memcpy(buf, rest, bl + 1);
    for (char *tok = strtok(buf, "."); tok; tok = strtok(NULL, ".")) {
        if (tcount >= sizeof(tokens) / sizeof(tokens[0])) break;
        tokens[tcount++] = tok;
    }
    for (size_t i = tcount; i-- > 0; ) {
        struct yconfig_node *m = node_new(YCONFIG_MAP);
        if (!m) { node_free(cur); return -1; }
        if (map_set(m, tokens[i], cur) < 0) {
            node_free(m); node_free(cur); return -1;
        }
        cur = m;
    }
    int rc = merge_into(root, cur);
    node_free(cur);
    return rc;
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
    char *p = malloc(len);
    if (!p) return NULL;
    snprintf(p, len, "%s/.config/%s/%s.yaml", home, app_name, app_name);
    return p;
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

struct yconfig_ptr_result yconfig_create(const struct yconfig_create_args *args)
{
    const char *app = (args && args->app_name) ? args->app_name : "picomesh";

    struct yconfig *c = calloc(1, sizeof(*c));
    if (!c) return PICOMESH_ERR(yconfig_ptr, "yconfig_create: calloc failed");
    c->root = node_new(YCONFIG_MAP);
    if (!c->root) {
        free(c);
        return PICOMESH_ERR(yconfig_ptr, "yconfig_create: root alloc failed");
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
            ydebug("yconfig: loading XDG %s", xdg);
            struct yconfig_node *file = parse_yaml_file(xdg);
            if (file) { merge_into(c->root, file); node_free(file); }
        }
        free(xdg);

        /* 2. Git repo root. */
        char *gr = git_root_path(app);
        if (gr && file_exists(gr)) {
            ydebug("yconfig: loading git-root %s", gr);
            struct yconfig_node *file = parse_yaml_file(gr);
            if (file) { merge_into(c->root, file); node_free(file); }
        }
        free(gr);

        /* 3. cwd. */
        char cwd_path[64];
        snprintf(cwd_path, sizeof(cwd_path), "%s.yaml", app);
        if (file_exists(cwd_path)) {
            ydebug("yconfig: loading cwd %s", cwd_path);
            struct yconfig_node *file = parse_yaml_file(cwd_path);
            if (file) { merge_into(c->root, file); node_free(file); }
        }
    }

    /* 4. Explicit --config-file (overrides the filesystem search). */
    if (args && args->config_file && file_exists(args->config_file)) {
        ydebug("yconfig: loading explicit %s", args->config_file);
        struct yconfig_node *file = parse_yaml_file(args->config_file);
        if (file) { merge_into(c->root, file); node_free(file); }
    }

    /* 5. --config CLI overrides — applied LAST, so they win over every file.
     *    This is what makes one base config + per-instance `--config a.b=c`
     *    overrides work. */
    if (args && args->cli_overrides) {
        for (size_t i = 0; i < args->cli_override_count; ++i) {
            if (apply_cli_override(c->root, args->cli_overrides[i]) < 0) {
                yconfig_destroy(c);
                return PICOMESH_ERR(yconfig_ptr, "yconfig_create: cli override merge failed");
            }
        }
    }

    /* Env-var substitution happens last so every layer's strings get
     * expanded uniformly. */
    subst_env(c->root);
    return PICOMESH_OK(yconfig_ptr, c);
}

void yconfig_destroy(struct yconfig *c)
{
    if (!c) return;
    node_free(c->root);
    free(c);
}

const struct yconfig_node *yconfig_root(const struct yconfig *c)
{
    return c ? c->root : NULL;
}

struct picomesh_void_result yconfig_promote_subtree(struct yconfig *c, const char *dot_path)
{
    if (!c || !dot_path) {
        return PICOMESH_ERR(picomesh_void, "yconfig_promote_subtree: NULL args");
    }
    char buf[256];
    size_t l = strlen(dot_path);
    if (l >= sizeof(buf)) {
        return PICOMESH_ERR(picomesh_void, "yconfig_promote_subtree: path too long");
    }
    memcpy(buf, dot_path, l + 1);

    const struct yconfig_node *sub = c->root;
    for (char *tok = strtok(buf, "."); tok; tok = strtok(NULL, ".")) {
        if (!sub || sub->kind != YCONFIG_MAP) {
            sub = NULL;
            break;
        }
        sub = map_get(sub, tok);
    }
    /* Subtree absent or not a map → no projection, but not an error.
     * The caller (engine) projects optimistically; missing entries
     * just mean "no service-local config to flatten". */
    if (!sub || sub->kind != YCONFIG_MAP) {
        return PICOMESH_OK_VOID();
    }
    if (merge_into(c->root, sub) < 0) {
        return PICOMESH_ERR(picomesh_void, "yconfig_promote_subtree: merge failed");
    }
    return PICOMESH_OK_VOID();
}

/* ---------------- dot-path lookup ---------------------------------- */

static const struct yconfig_node *walk_path(const struct yconfig_node *n, const char *path)
{
    if (!n || !path) return NULL;
    char buf[256];
    size_t len = strlen(path);
    if (len >= sizeof(buf)) return NULL;
    memcpy(buf, path, len + 1);

    const struct yconfig_node *cur = n;
    for (char *tok = strtok(buf, "."); tok; tok = strtok(NULL, ".")) {
        if (!cur || cur->kind != YCONFIG_MAP) return NULL;
        cur = map_get(cur, tok);
        if (!cur) return NULL;
    }
    return cur;
}

struct yconfig_node_ptr_result yconfig_get(const struct yconfig *c, const char *dot_path)
{
    if (!c || !dot_path) {
        return PICOMESH_ERR(yconfig_node_ptr, "yconfig_get: NULL args");
    }
    const struct yconfig_node *direct = walk_path(c->root, dot_path);
    if (direct) return PICOMESH_OK(yconfig_node_ptr, direct);

    /* Inheritance fallback: peel the first segment and retry against the
     * root. So `storage.host` falls back to `host` if `storage` doesn't
     * define one. This walks once — multi-level fallback would mean
     * `a.b.c` → `b.c` → `c`, which yaapp's ConfigNode supports. Repeat. */
    char buf[256];
    size_t len = strlen(dot_path);
    if (len >= sizeof(buf)) return PICOMESH_OK(yconfig_node_ptr, NULL);
    memcpy(buf, dot_path, len + 1);
    char *dot = strchr(buf, '.');
    while (dot) {
        const struct yconfig_node *hit = walk_path(c->root, dot + 1);
        if (hit) return PICOMESH_OK(yconfig_node_ptr, hit);
        dot = strchr(dot + 1, '.');
    }
    return PICOMESH_OK(yconfig_node_ptr, NULL);
}

const struct yconfig_node *yconfig_section(const struct yconfig *c, const char *name)
{
    return map_get(c ? c->root : NULL, name);
}

/* ---------------- typed accessors --------------------------------- */

enum yconfig_kind yconfig_node_kind(const struct yconfig_node *n)
{
    return n ? n->kind : YCONFIG_NULL;
}

size_t yconfig_node_size(const struct yconfig_node *n)
{
    if (!n) return 0;
    if (n->kind == YCONFIG_MAP)  return n->u.map.count;
    if (n->kind == YCONFIG_LIST) return n->u.list.count;
    return 0;
}

const char *yconfig_node_as_string(const struct yconfig_node *n, const char *fallback)
{
    return (n && n->kind == YCONFIG_STRING) ? n->u.s : fallback;
}

int64_t yconfig_node_as_int(const struct yconfig_node *n, int64_t fallback)
{
    if (!n) return fallback;
    if (n->kind == YCONFIG_INT)   return n->u.i;
    if (n->kind == YCONFIG_FLOAT) return (int64_t)n->u.f;
    if (n->kind == YCONFIG_BOOL)  return (int64_t)n->u.b;
    if (n->kind == YCONFIG_STRING) {
        int64_t v;
        if (parse_int64(n->u.s, &v) == 0) return v;
    }
    return fallback;
}

double yconfig_node_as_float(const struct yconfig_node *n, double fallback)
{
    if (!n) return fallback;
    if (n->kind == YCONFIG_FLOAT) return n->u.f;
    if (n->kind == YCONFIG_INT)   return (double)n->u.i;
    if (n->kind == YCONFIG_STRING) {
        double v;
        if (parse_float(n->u.s, &v) == 0) return v;
    }
    return fallback;
}

int yconfig_node_as_bool(const struct yconfig_node *n, int fallback)
{
    if (!n) return fallback;
    if (n->kind == YCONFIG_BOOL) return n->u.b;
    if (n->kind == YCONFIG_INT)  return n->u.i != 0;
    if (n->kind == YCONFIG_STRING) {
        const char *s = n->u.s;
        if (!s || !*s) return fallback;
        if (strcasecmp(s, "true") == 0 || strcasecmp(s, "yes") == 0 ||
            strcasecmp(s, "on") == 0 || strcmp(s, "1") == 0) return 1;
        if (strcasecmp(s, "false") == 0 || strcasecmp(s, "no") == 0 ||
            strcasecmp(s, "off") == 0 || strcmp(s, "0") == 0) return 0;
    }
    return fallback;
}

int yconfig_node_for_each(const struct yconfig_node *n,
                          int (*cb)(const char *key, const struct yconfig_node *val, void *ud),
                          void *ud)
{
    if (!n || n->kind != YCONFIG_MAP || !cb) return 0;
    for (size_t i = 0; i < n->u.map.count; ++i) {
        int r = cb(n->u.map.entries[i].key, n->u.map.entries[i].value, ud);
        if (r) return r;
    }
    return 0;
}

const struct yconfig_node *yconfig_node_at(const struct yconfig_node *n, size_t idx)
{
    if (!n || n->kind != YCONFIG_LIST || idx >= n->u.list.count) return NULL;
    return n->u.list.items[idx];
}

const struct yconfig_node *yconfig_node_get(const struct yconfig_node *n, const char *key)
{
    if (!n || n->kind != YCONFIG_MAP || !key) return NULL;
    for (size_t i = 0; i < n->u.map.count; ++i)
        if (strcmp(n->u.map.entries[i].key, key) == 0) return n->u.map.entries[i].value;
    return NULL;
}

/* ---------------- pretty-print ------------------------------------ */

static int dump_node(const struct yconfig_node *n, char *buf, size_t cap, size_t off, int indent);

static int append(char *buf, size_t cap, size_t off, const char *s)
{
    size_t l = strlen(s);
    if (off + l >= cap) return (int)off;
    memcpy(buf + off, s, l);
    return (int)(off + l);
}

static int dump_indent(char *buf, size_t cap, size_t off, int indent)
{
    for (int i = 0; i < indent && off + 1 < cap; ++i) {
        buf[off++] = ' '; buf[off++] = ' ';
    }
    return (int)off;
}

static int dump_node(const struct yconfig_node *n, char *buf, size_t cap, size_t off, int indent)
{
    if (!n) return (int)off;
    char tmp[64];
    switch (n->kind) {
    case YCONFIG_NULL:   off = (size_t)append(buf, cap, off, "null"); break;
    case YCONFIG_BOOL:   off = (size_t)append(buf, cap, off, n->u.b ? "true" : "false"); break;
    case YCONFIG_INT:    snprintf(tmp, sizeof(tmp), "%" PRId64, n->u.i); off = (size_t)append(buf, cap, off, tmp); break;
    case YCONFIG_FLOAT:  snprintf(tmp, sizeof(tmp), "%g", n->u.f);       off = (size_t)append(buf, cap, off, tmp); break;
    case YCONFIG_STRING: off = (size_t)append(buf, cap, off, "\"");
                         off = (size_t)append(buf, cap, off, n->u.s);
                         off = (size_t)append(buf, cap, off, "\"");
                         break;
    case YCONFIG_LIST:
        off = (size_t)append(buf, cap, off, "[");
        for (size_t i = 0; i < n->u.list.count; ++i) {
            if (i) off = (size_t)append(buf, cap, off, ", ");
            off = (size_t)dump_node(n->u.list.items[i], buf, cap, off, indent);
        }
        off = (size_t)append(buf, cap, off, "]");
        break;
    case YCONFIG_MAP:
        off = (size_t)append(buf, cap, off, "{\n");
        for (size_t i = 0; i < n->u.map.count; ++i) {
            off = (size_t)dump_indent(buf, cap, off, indent + 1);
            off = (size_t)append(buf, cap, off, n->u.map.entries[i].key);
            off = (size_t)append(buf, cap, off, ": ");
            off = (size_t)dump_node(n->u.map.entries[i].value, buf, cap, off, indent + 1);
            off = (size_t)append(buf, cap, off, ",\n");
        }
        off = (size_t)dump_indent(buf, cap, off, indent);
        off = (size_t)append(buf, cap, off, "}");
        break;
    }
    return (int)off;
}

size_t yconfig_dump(const struct yconfig *c, char *buf, size_t bufsize)
{
    if (!buf || bufsize == 0) return 0;
    int n = dump_node(c ? c->root : NULL, buf, bufsize, 0, 0);
    if ((size_t)n >= bufsize) n = (int)bufsize - 1;
    buf[n] = 0;
    return (size_t)n;
}

size_t yconfig_node_dump(const struct yconfig_node *n, char *buf, size_t bufsize)
{
    if (!buf || bufsize == 0) return 0;
    int w = dump_node(n, buf, bufsize, 0, 0);
    if ((size_t)w >= bufsize) w = (int)bufsize - 1;
    buf[w] = 0;
    return (size_t)w;
}
