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

static const char *def_dest(const struct yargv_option_def *def, char *scratch, size_t scratch_len)
{
    if (def->dest) return def->dest;
    if (def->long_name && strncmp(def->long_name, "--", 2) == 0) {
        size_t len = strlen(def->long_name + 2);
        if (len + 1 > scratch_len) return def->long_name + 2;
        memcpy(scratch, def->long_name + 2, len + 1);
        for (size_t i = 0; i < len; ++i) if (scratch[i] == '-') scratch[i] = '_';
        return scratch;
    }
    return def->long_name ? def->long_name : (def->short_name ? def->short_name : "");
}

static struct yargv_value_slot *slot_by_dest(struct yargv_chain *chain, const char *dest)
{
    for (size_t i = 0; i < chain->def_count; ++i) {
        char scratch[64];
        const char *candidate = def_dest(&chain->defs[i], scratch, sizeof(scratch));
        if (strcmp(candidate, dest) == 0) return &chain->slots[i];
    }
    return NULL;
}

static const struct yargv_value_slot *slot_by_dest_const(const struct yargv_chain *chain, const char *dest)
{
    return slot_by_dest((struct yargv_chain *)chain, dest);
}

static int kv_push(struct yargv_value_slot *slot, const char *kv)
{
    if (slot->kv_count == slot->kv_cap) {
        size_t new_cap = slot->kv_cap ? slot->kv_cap * 2 : 4;
        char **new_list = realloc(slot->kv_list, new_cap * sizeof(*new_list));
        if (!new_list) return -1;
        slot->kv_list = new_list;
        slot->kv_cap = new_cap;
    }
    slot->kv_list[slot->kv_count] = strdup(kv);
    if (!slot->kv_list[slot->kv_count]) return -1;
    slot->kv_count++;
    return 0;
}

static struct yargv_value_slot *match(struct yargv_chain *chain, const char *tok)
{
    if (!tok) return NULL;
    for (size_t i = 0; i < chain->def_count; ++i) {
        const struct yargv_option_def *def = &chain->defs[i];
        if (def->long_name && strcmp(def->long_name, tok) == 0) return &chain->slots[i];
        if (def->short_name && strcmp(def->short_name, tok) == 0) return &chain->slots[i];
    }
    return NULL;
}

struct yargv_chain_ptr_result yargv_parse(const struct yargv_option_def *defs,
                                          size_t def_count, int argc, char **argv)
{
    struct yargv_chain *chain = calloc(1, sizeof(*chain));
    if (!chain) return PICOMESH_ERR(yargv_chain_ptr, "yargv_parse: calloc failed");
    chain->defs = defs;
    chain->def_count = def_count;
    chain->slots = calloc(def_count ? def_count : 1, sizeof(*chain->slots));
    if (def_count && !chain->slots) {
        free(chain);
        return PICOMESH_ERR(yargv_chain_ptr, "yargv_parse: slots alloc failed");
    }
    for (size_t i = 0; i < def_count; ++i) chain->slots[i].def = &defs[i];

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
            struct yargv_value_slot *slot = match(chain, tok);
            if (!slot) {
                /* Unknown option — stop parsing, leave for subcommand. */
                break;
            }
            switch (slot->def->kind) {
            case YARGV_BOOL:
                slot->present = 1;
                slot->bool_val = 1;
                ++i;
                break;
            case YARGV_VALUE:
                if (i + 1 >= argc) {
                    yargv_chain_destroy(chain);
                    return PICOMESH_ERR(yargv_chain_ptr, "yargv_parse: missing value for option");
                }
                slot->present = 1;
                free(slot->string_val);
                slot->string_val = strdup(argv[i + 1]);
                if (!slot->string_val) {
                    yargv_chain_destroy(chain);
                    return PICOMESH_ERR(yargv_chain_ptr, "yargv_parse: strdup failed");
                }
                i += 2;
                break;
            case YARGV_KEY_VALUE:
                if (i + 1 >= argc) {
                    yargv_chain_destroy(chain);
                    return PICOMESH_ERR(yargv_chain_ptr, "yargv_parse: missing key=value for option");
                }
                slot->present = 1;
                if (kv_push(slot, argv[i + 1]) < 0) {
                    yargv_chain_destroy(chain);
                    return PICOMESH_ERR(yargv_chain_ptr, "yargv_parse: kv push failed");
                }
                i += 2;
                break;
            }
            continue;
        }
        /* First operand becomes the subcommand. The rest is the
         * subcommand's own argv (raw — no further option parsing here). */
        if (!chain->subcommand) {
            chain->subcommand = strdup(tok);
            if (!chain->subcommand) {
                yargv_chain_destroy(chain);
                return PICOMESH_ERR(yargv_chain_ptr, "yargv_parse: strdup failed");
            }
            ++i;
            chain->sub_argc = argc - i;
            chain->sub_argv = (char **)(argv + i);
            break;
        }
        ++i;
    }
    return PICOMESH_OK(yargv_chain_ptr, chain);
}

void yargv_chain_destroy(struct yargv_chain *chain)
{
    if (!chain) return;
    for (size_t i = 0; i < chain->def_count; ++i) {
        free(chain->slots[i].string_val);
        for (size_t j = 0; j < chain->slots[i].kv_count; ++j) free(chain->slots[i].kv_list[j]);
        free(chain->slots[i].kv_list);
    }
    free(chain->slots);
    free(chain->subcommand);
    free(chain);
}

const char *yargv_get_string(const struct yargv_chain *chain, const char *dest, const char *fallback)
{
    const struct yargv_value_slot *slot = slot_by_dest_const(chain, dest);
    if (slot && slot->present && slot->string_val) return slot->string_val;
    return fallback;
}

int yargv_get_bool(const struct yargv_chain *chain, const char *dest, int fallback)
{
    const struct yargv_value_slot *slot = slot_by_dest_const(chain, dest);
    if (slot && slot->present) return slot->bool_val;
    return fallback;
}

int64_t yargv_get_int(const struct yargv_chain *chain, const char *dest, int64_t fallback)
{
    const struct yargv_value_slot *slot = slot_by_dest_const(chain, dest);
    if (slot && slot->present && slot->string_val) return strtoll(slot->string_val, NULL, 10);
    return fallback;
}

size_t yargv_get_kv_list(const struct yargv_chain *chain, const char *dest,
                         const char **out, size_t out_cap)
{
    const struct yargv_value_slot *slot = slot_by_dest_const(chain, dest);
    if (!slot || !slot->kv_count) return 0;
    size_t copy_count = slot->kv_count < out_cap ? slot->kv_count : out_cap;
    for (size_t i = 0; i < copy_count; ++i) out[i] = slot->kv_list[i];
    return copy_count;
}

const char *yargv_subcommand(const struct yargv_chain *chain)
{
    return chain ? chain->subcommand : NULL;
}

int yargv_sub_argc(const struct yargv_chain *chain)
{
    return chain ? chain->sub_argc : 0;
}

char *const *yargv_sub_argv(const struct yargv_chain *chain)
{
    return chain ? chain->sub_argv : NULL;
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
        int synopsis_len = yargv_opt_synopsis(&defs[i], buf, sizeof(buf));
        if (synopsis_len > width) width = synopsis_len;
    }
    for (size_t i = 0; i < def_count; ++i) {
        char buf[96];
        yargv_opt_synopsis(&defs[i], buf, sizeof(buf));
        fprintf(out, "  %-*s  %s\n", width, buf, defs[i].help ? defs[i].help : "");
    }
}
