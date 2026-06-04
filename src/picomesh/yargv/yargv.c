/* yargv — CLI option / cmd-chain parser.
 *
 * Stays small on purpose. Each option in `defs` finds its match by
 * long_name (`--foo`) or short_name (`-f`). When matched:
 *   YARGV_BOOL       — set the flag, no value consumed.
 *   YARGV_VALUE      — consume the next argv token, store last value.
 *   YARGV_KEY_VALUE  — consume the next argv token, append to a list.
 *
 * The first unrecognised operand becomes the subcommand and stops
 * option parsing; everything from there is forwarded raw via
 * `sub_argv`. */

#include <picomesh/yargv/yargv.h>
#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>

#include <stdlib.h>
#include <string.h>

struct yargv_value_slot {
    const struct yargv_option_def *def;
    int present;         /* set at least once */
    int bool_val;        /* YARGV_BOOL */
    char *string_val;    /* YARGV_VALUE: last value wins */
    char **kv_list;      /* YARGV_KEY_VALUE+multiple: accumulated values */
    size_t kv_count;
    size_t kv_cap;
};

struct yargv_chain {
    const struct yargv_option_def *defs;
    size_t def_count;
    struct yargv_value_slot *slots; /* def_count entries */

    char *subcommand;   /* strdup'd, NULL if no subcommand */
    int sub_argc;
    char **sub_argv;    /* borrows pointers from the original argv */
};

static const char *def_dest(const struct yargv_option_def *d, char *scratch, size_t scratch_len)
{
    if (d->dest) return d->dest;
    if (d->long_name && strncmp(d->long_name, "--", 2) == 0) {
        size_t l = strlen(d->long_name + 2);
        if (l + 1 > scratch_len) return d->long_name + 2;
        memcpy(scratch, d->long_name + 2, l + 1);
        for (size_t i = 0; i < l; ++i) if (scratch[i] == '-') scratch[i] = '_';
        return scratch;
    }
    return d->long_name ? d->long_name : (d->short_name ? d->short_name : "");
}

static struct yargv_value_slot *slot_by_dest(struct yargv_chain *c, const char *dest)
{
    for (size_t i = 0; i < c->def_count; ++i) {
        char scratch[64];
        const char *d = def_dest(&c->defs[i], scratch, sizeof(scratch));
        if (strcmp(d, dest) == 0) return &c->slots[i];
    }
    return NULL;
}

static const struct yargv_value_slot *slot_by_dest_const(const struct yargv_chain *c, const char *dest)
{
    return slot_by_dest((struct yargv_chain *)c, dest);
}

static int kv_push(struct yargv_value_slot *s, const char *kv)
{
    if (s->kv_count == s->kv_cap) {
        size_t nc = s->kv_cap ? s->kv_cap * 2 : 4;
        char **na = realloc(s->kv_list, nc * sizeof(*na));
        if (!na) return -1;
        s->kv_list = na;
        s->kv_cap = nc;
    }
    s->kv_list[s->kv_count] = strdup(kv);
    if (!s->kv_list[s->kv_count]) return -1;
    s->kv_count++;
    return 0;
}

static struct yargv_value_slot *match(struct yargv_chain *c, const char *tok)
{
    if (!tok) return NULL;
    for (size_t i = 0; i < c->def_count; ++i) {
        const struct yargv_option_def *d = &c->defs[i];
        if (d->long_name && strcmp(d->long_name, tok) == 0) return &c->slots[i];
        if (d->short_name && strcmp(d->short_name, tok) == 0) return &c->slots[i];
    }
    return NULL;
}

struct yargv_chain_ptr_result yargv_parse(const struct yargv_option_def *defs,
                                          size_t def_count, int argc, char **argv)
{
    struct yargv_chain *c = calloc(1, sizeof(*c));
    if (!c) return PICOMESH_ERR(yargv_chain_ptr, "yargv_parse: calloc failed");
    c->defs = defs;
    c->def_count = def_count;
    c->slots = calloc(def_count ? def_count : 1, sizeof(*c->slots));
    if (def_count && !c->slots) {
        free(c);
        return PICOMESH_ERR(yargv_chain_ptr, "yargv_parse: slots alloc failed");
    }
    for (size_t i = 0; i < def_count; ++i) c->slots[i].def = &defs[i];

    /* argv[0] is the program name. Start at 1. */
    int i = 1;
    int options_ended = 0;
    while (i < argc) {
        const char *tok = argv[i];
        if (!options_ended && strcmp(tok, "--") == 0) {
            options_ended = 1;
            ++i;
            continue;
        }
        if (!options_ended && tok[0] == '-' && tok[1] != '\0') {
            struct yargv_value_slot *s = match(c, tok);
            if (!s) {
                /* Unknown option — stop parsing, leave for subcommand. */
                break;
            }
            switch (s->def->kind) {
            case YARGV_BOOL:
                s->present = 1;
                s->bool_val = 1;
                ++i;
                break;
            case YARGV_VALUE:
                if (i + 1 >= argc) {
                    yargv_chain_destroy(c);
                    return PICOMESH_ERR(yargv_chain_ptr, "yargv_parse: missing value for option");
                }
                s->present = 1;
                free(s->string_val);
                s->string_val = strdup(argv[i + 1]);
                if (!s->string_val) {
                    yargv_chain_destroy(c);
                    return PICOMESH_ERR(yargv_chain_ptr, "yargv_parse: strdup failed");
                }
                i += 2;
                break;
            case YARGV_KEY_VALUE:
                if (i + 1 >= argc) {
                    yargv_chain_destroy(c);
                    return PICOMESH_ERR(yargv_chain_ptr, "yargv_parse: missing key=value for option");
                }
                s->present = 1;
                if (kv_push(s, argv[i + 1]) < 0) {
                    yargv_chain_destroy(c);
                    return PICOMESH_ERR(yargv_chain_ptr, "yargv_parse: kv push failed");
                }
                i += 2;
                break;
            }
            continue;
        }
        /* First operand becomes the subcommand. The rest is the
         * subcommand's own argv (raw — no further option parsing here). */
        if (!c->subcommand) {
            c->subcommand = strdup(tok);
            if (!c->subcommand) {
                yargv_chain_destroy(c);
                return PICOMESH_ERR(yargv_chain_ptr, "yargv_parse: strdup failed");
            }
            ++i;
            c->sub_argc = argc - i;
            c->sub_argv = (char **)(argv + i);
            break;
        }
        ++i;
    }
    return PICOMESH_OK(yargv_chain_ptr, c);
}

void yargv_chain_destroy(struct yargv_chain *c)
{
    if (!c) return;
    for (size_t i = 0; i < c->def_count; ++i) {
        free(c->slots[i].string_val);
        for (size_t j = 0; j < c->slots[i].kv_count; ++j) free(c->slots[i].kv_list[j]);
        free(c->slots[i].kv_list);
    }
    free(c->slots);
    free(c->subcommand);
    free(c);
}

const char *yargv_get_string(const struct yargv_chain *c, const char *dest, const char *fallback)
{
    const struct yargv_value_slot *s = slot_by_dest_const(c, dest);
    if (s && s->present && s->string_val) return s->string_val;
    return fallback;
}

int yargv_get_bool(const struct yargv_chain *c, const char *dest, int fallback)
{
    const struct yargv_value_slot *s = slot_by_dest_const(c, dest);
    if (s && s->present) return s->bool_val;
    return fallback;
}

int64_t yargv_get_int(const struct yargv_chain *c, const char *dest, int64_t fallback)
{
    const struct yargv_value_slot *s = slot_by_dest_const(c, dest);
    if (s && s->present && s->string_val) return strtoll(s->string_val, NULL, 10);
    return fallback;
}

size_t yargv_get_kv_list(const struct yargv_chain *c, const char *dest,
                         const char **out, size_t out_cap)
{
    const struct yargv_value_slot *s = slot_by_dest_const(c, dest);
    if (!s || !s->kv_count) return 0;
    size_t n = s->kv_count < out_cap ? s->kv_count : out_cap;
    for (size_t i = 0; i < n; ++i) out[i] = s->kv_list[i];
    return n;
}

const char *yargv_subcommand(const struct yargv_chain *c)
{
    return c ? c->subcommand : NULL;
}

int yargv_sub_argc(const struct yargv_chain *c)
{
    return c ? c->sub_argc : 0;
}

char *const *yargv_sub_argv(const struct yargv_chain *c)
{
    return c ? c->sub_argv : NULL;
}

/* Build the left "  --long, -short <ARG>" column for one option. */
static int yargv_opt_synopsis(const struct yargv_option_def *def, char *buf, size_t cap)
{
    const char *arg = "";
    switch (def->kind) {
    case YARGV_VALUE:     arg = " VALUE"; break;
    case YARGV_KEY_VALUE: arg = " K=V";   break;
    case YARGV_BOOL:      arg = "";       break;
    }
    if (def->long_name && def->short_name)
        return snprintf(buf, cap, "%s, %s%s", def->long_name, def->short_name, arg);
    if (def->long_name)
        return snprintf(buf, cap, "%s%s", def->long_name, arg);
    if (def->short_name)
        return snprintf(buf, cap, "%s%s", def->short_name, arg);
    return snprintf(buf, cap, "%s", def->dest ? def->dest : "?");
}

void yargv_print_options(const struct yargv_option_def *defs, size_t def_count, FILE *out)
{
    if (!defs || !out) return;
    /* Two passes: measure the widest synopsis, then print aligned. */
    int width = 0;
    for (size_t i = 0; i < def_count; ++i) {
        char buf[96];
        int n = yargv_opt_synopsis(&defs[i], buf, sizeof(buf));
        if (n > width) width = n;
    }
    for (size_t i = 0; i < def_count; ++i) {
        char buf[96];
        yargv_opt_synopsis(&defs[i], buf, sizeof(buf));
        fprintf(out, "  %-*s  %s\n", width, buf, defs[i].help ? defs[i].help : "");
    }
}
