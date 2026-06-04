/* yargv — CLI option / cmd-chain parser.
 *
 * Port of yaapp's `lib/argv/cmd_chain.py`. The execution model is:
 *
 *   picomesh [--global-options...] <subcommand> [<sub-args>...]
 *
 * The root command parses every recognised long/short option until it
 * hits the first operand, which becomes the subcommand name. Anything
 * after that goes into `cmd_argv` (raw, not parsed) — the subcommand
 * picks it apart itself. This is the "command-into-command" shape
 * yaapp uses.
 *
 * Option types (mirrors yaapp's `value:` field):
 *
 *   YARGV_BOOL        no value (flag). `--verbose`.
 *   YARGV_VALUE       single string value. `--port 8000`.
 *   YARGV_KEY_VALUE   `--config foo.bar=baz`. Can repeat with
 *                     `multiple=1`; accumulated into one merged list
 *                     of `key=value` strings.
 *
 * Results are stored as parsed values you read off the cmd struct.
 * For YARGV_KEY_VALUE+multiple options, you get an array of strings
 * (the unparsed `key=value` form, ready to hand to yconfig). */

#ifndef PICOMESH_YARGV_YARGV_H
#define PICOMESH_YARGV_YARGV_H

#include <picomesh/ycore/result.h>

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum yargv_kind {
    YARGV_BOOL,
    YARGV_VALUE,
    YARGV_KEY_VALUE,
};

struct yargv_option_def {
    const char *long_name;   /* e.g. "--port" (without value); NULL = no long form */
    const char *short_name;  /* e.g. "-p"; NULL = no short form */
    const char *dest;        /* destination key. Defaults to long_name minus "--". */
    const char *help;        /* shown in --help */
    enum yargv_kind kind;
    int multiple;            /* repeatable? key_value usually wants this */
};

struct yargv_cmd;
struct yargv_chain;

PICOMESH_RESULT_DECLARE(yargv_chain_ptr, struct yargv_chain *);

struct yargv_chain_ptr_result yargv_parse(const struct yargv_option_def *defs,
                                          size_t def_count, int argc, char **argv);
void yargv_chain_destroy(struct yargv_chain *c);

/* Print one aligned line per option in `defs` to `out`: long/short forms, a
 * value placeholder chosen by kind, and the option's `help` text. This is the
 * SINGLE source of CLI documentation — a program builds its `--help` from the
 * very table it passes to yargv_parse(), so the help can never drift from the
 * flags that are actually parsed. The caller prints its own usage/synopsis
 * line and subcommand list around this options block. */
void yargv_print_options(const struct yargv_option_def *defs, size_t def_count, FILE *out);

/* Accessors against the root cmd (where every recognised option lives). */
const char *yargv_get_string(const struct yargv_chain *c, const char *dest, const char *fallback);
int yargv_get_bool(const struct yargv_chain *c, const char *dest, int fallback);
int64_t yargv_get_int(const struct yargv_chain *c, const char *dest, int64_t fallback);

/* Multi-value (KEY_VALUE with multiple=1) accessor: writes pointers to
 * the parsed `key=value` strings into `out` (up to `out_cap`), returns
 * the count (clamped to out_cap). Pointers are valid for the lifetime
 * of the chain. */
size_t yargv_get_kv_list(const struct yargv_chain *c, const char *dest,
                         const char **out, size_t out_cap);

/* Subcommand name (NULL if none was given) and its remaining tokens. */
const char *yargv_subcommand(const struct yargv_chain *c);
int yargv_sub_argc(const struct yargv_chain *c);
char *const *yargv_sub_argv(const struct yargv_chain *c);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_YARGV_YARGV_H */
