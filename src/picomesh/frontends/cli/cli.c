/* cli — one-shot local invoker.
 *
 * Drives one method call against a freshly-allocated local instance.
 * Argv layout (after parsing by argv):
 *
 *     picomesh invoke <plugin>_<class>_<method> [arg1 arg2 ...]
 *
 * Argument parsing for positionals is intentionally tiny:
 *   - "true" / "false" / "null"   → JSON literal
 *   - leading digit / sign + digits → JSON number (int or float)
 *   - anything else                  → JSON string
 *
 * That keeps shell quoting straightforward — `picomesh invoke
 * calculator_calc_add 6 7` sends `[6, 7]`. */

#include <picomesh/frontends/cli/cli.h>
#include <picomesh/engine/engine.h>
#include <picomesh/argv/argv.h>
#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/jinvoke.h>
#include <picomesh/json/json.h>
#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build a JSON args array from positional argv. Returns a freshly
 * allocated buffer (caller frees) containing JSON like `[1, "foo"]`. */
static char *build_args_json(int argc, char *const *argv, size_t *out_len)
{
    struct json_writer *writer = json_writer_new();
    json_writer_begin_array(writer);
    for (int i = 0; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "true") == 0) {
            json_writer_bool(writer, 1);
        } else if (strcmp(arg, "false") == 0) {
            json_writer_bool(writer, 0);
        } else if (strcmp(arg, "null") == 0) {
            json_writer_null(writer);
        } else {
            /* Numeric literal? Walk: optional sign, then digits, with
             * optional one '.' for float. */
            const char *scan = arg;
            int has_digit = 0, has_dot = 0, is_numeric = 1;
            if (*scan == '+' || *scan == '-') ++scan;
            for (; *scan; ++scan) {
                if (isdigit((unsigned char)*scan)) { has_digit = 1; }
                else if (*scan == '.' && !has_dot) { has_dot = 1; }
                else { is_numeric = 0; break; }
            }
            if (is_numeric && has_digit) {
                if (has_dot) json_writer_float(writer, strtod(arg, NULL));
                else         json_writer_int(writer, (int64_t)strtoll(arg, NULL, 10));
            } else {
                json_writer_string(writer, arg);
            }
        }
    }
    json_writer_end_array(writer);
    size_t len;
    const char *data = json_writer_data(writer, &len);
    char *copy = malloc(len + 1);
    if (!copy) { json_writer_free(writer); return NULL; }
    memcpy(copy, data, len + 1);
    if (out_len) *out_len = len;
    json_writer_free(writer);
    return copy;
}

/* Derive a class qname from a method qname by walking back from the
 * end to the second-to-last underscore split. Convention is
 * `<plugin>_<class>_<verb...>` where `<plugin>_<class>` is the class
 * qname. We don't know where the verb starts without metadata, so we
 * try progressively shorter suffixes until class_by_name finds one. */
static struct picomesh_string_result guess_class_qname(const char *method_qname)
{
    char buf[256];
    size_t len = strlen(method_qname);
    if (len >= sizeof(buf)) return PICOMESH_OK(picomesh_string, NULL);
    memcpy(buf, method_qname, len + 1);
    /* Walk from right to left, replacing each underscore with '\0'
     * and asking class_by_name if that prefix is a known class. */
    for (size_t i = len; i > 0; --i) {
        if (buf[i - 1] != '_') continue;
        buf[i - 1] = '\0';
        struct class_ptr_result class_res = class_by_name(buf);
        if (PICOMESH_IS_OK(class_res) && class_res.value) {
            return PICOMESH_OK(picomesh_string, strdup(buf));
        }
        /* A miss here is the normal probe outcome (this prefix isn't a class) —
         * try the next-shorter prefix, don't treat it as a real error. */
        if (PICOMESH_IS_ERR(class_res)) picomesh_error_destroy(class_res.error);
        buf[i - 1] = '_';
    }
    return PICOMESH_OK(picomesh_string, NULL);
}

struct picomesh_int_result picomesh_cli_dispatch(struct picomesh_engine *engine)
{
    struct argv_chain *cli = picomesh_engine_cli(engine);
    const char *sub = argv_subcommand(cli);
    if (!sub || strcmp(sub, "invoke") != 0) {
        fprintf(stderr, "picomesh cli: expected 'invoke' subcommand\n");
        return PICOMESH_OK(picomesh_int, 2);
    }
    int sargc = argv_sub_argc(cli);
    char *const *sargv = argv_sub_argv(cli);
    if (sargc < 1) {
        fprintf(stderr,
                "usage: picomesh invoke <plugin>_<class>_<method> [args...]\n");
        return PICOMESH_OK(picomesh_int, 2);
    }
    const char *method = sargv[0];

    struct picomesh_string_result class_qname_res = guess_class_qname(method);
    char *class_qname = class_qname_res.value;
    if (!class_qname) {
        fprintf(stderr,
                "cli: cannot find a class matching method '%s'.\n"
                "     Try `picomesh <method-qname> with the class linked in.\n",
                method);
        return PICOMESH_OK(picomesh_int, 1);
    }

    struct class_ptr_result class_res = class_by_name(class_qname);
    if (PICOMESH_IS_ERR(class_res)) {
        fprintf(stderr, "cli: class_by_name(%s) failed\n", class_qname);
        picomesh_error_print(stderr, "cli: class_by_name", class_res.error);
        picomesh_error_destroy(class_res.error);
        free(class_qname);
        return PICOMESH_OK(picomesh_int, 1);
    }
    struct object_ptr_result object_res = object_alloc(class_res.value);
    if (PICOMESH_IS_ERR(object_res)) {
        fprintf(stderr, "cli: object_alloc failed\n");
        picomesh_error_print(stderr, "cli: object_alloc", object_res.error);
        picomesh_error_destroy(object_res.error);
        free(class_qname);
        return PICOMESH_OK(picomesh_int, 1);
    }
    struct object *obj = object_res.value;

    /* Build args + parse them back via json so jinvoke sees a real
     * json_value*. The intermediate JSON text round-trip is wasteful
     * but keeps the jinvoke contract uniform with yttp's. */
    size_t args_len;
    char *args_json = build_args_json(sargc - 1, sargv + 1, &args_len);
    if (!args_json) {
        free(class_qname);
        object_free(obj);
        return PICOMESH_OK(picomesh_int, 1);
    }
    struct json_doc *args_doc = json_parse(args_json, args_len);
    if (!args_doc) {
        fprintf(stderr, "cli: failed to encode args: %s\n", json_last_error());
        free(args_json);
        free(class_qname);
        object_free(obj);
        return PICOMESH_OK(picomesh_int, 1);
    }

    jinvoke_fn invoke_fn = jinvoke_for(method);
    if (!invoke_fn) {
        fprintf(stderr, "cli: no jinvoke registered for '%s'\n", method);
        json_doc_free(args_doc);
        free(args_json);
        free(class_qname);
        object_free(obj);
        return PICOMESH_OK(picomesh_int, 1);
    }

    struct json_writer *writer = json_writer_new();
    char err[8192] = {0};
    /* Local dispatch: the cli owns the object in-process — NULL ctx and
     * NULL headers. */
    struct picomesh_void_result invoke_res =
        invoke_fn(NULL, obj, NULL, json_doc_root(args_doc), writer, err, sizeof(err));
    int failed = PICOMESH_IS_ERR(invoke_res);
    if (failed) {
        fprintf(stderr, "cli: %s\n", err[0] ? err : "invoke failed");
        picomesh_error_print(stderr, "cli: invoke", invoke_res.error);
        picomesh_error_destroy(invoke_res.error);
    } else {
        size_t out_len;
        const char *out = json_writer_data(writer, &out_len);
        printf("%.*s\n", (int)out_len, out);
    }
    json_writer_free(writer);
    json_doc_free(args_doc);
    free(args_json);
    free(class_qname);
    object_free(obj);
    return PICOMESH_OK(picomesh_int, failed ? 1 : 0);
}
