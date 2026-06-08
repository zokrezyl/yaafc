/* result.c — chain helpers for the Result error type. */

#include <picomesh/ycore/result.h>

#include <stdio.h>
#include <stdlib.h>

struct picomesh_error *picomesh_error_chain(struct picomesh_error prev)
{
    struct picomesh_error *node = malloc(sizeof(*node));
    if (!node) {
        /* OOM during error wrapping: drop the inner chain so we don't leak
         * it. The outer error still surfaces; debug context is lost. */
        picomesh_error_destroy(prev);
        return NULL;
    }
    *node = prev;
    return node;
}

void picomesh_error_destroy(struct picomesh_error err)
{
    struct picomesh_error *node = err.cause;
    while (node) {
        struct picomesh_error *next = node->cause;
        free(node);
        node = next;
    }
}

void picomesh_error_print(FILE *out, const char *headline, struct picomesh_error err)
{
    if (!out) {
        return;
    }
    if (headline) {
        fprintf(out, "%s: %s\n", headline, err.msg ? err.msg : "<no message>");
    } else {
        fprintf(out, "%s\n", err.msg ? err.msg : "<no message>");
    }
    fprintf(out, "    at %s:%d (%s)\n", err.file ? err.file : "<unknown>", err.line,
            err.func ? err.func : "<unknown>");
    for (const struct picomesh_error *cause = err.cause; cause; cause = cause->cause) {
        fprintf(out, "  caused by: %s\n", cause->msg ? cause->msg : "<no message>");
        fprintf(out, "    at %s:%d (%s)\n", cause->file ? cause->file : "<unknown>", cause->line,
                cause->func ? cause->func : "<unknown>");
    }
}

size_t picomesh_error_snprint(char *buf, size_t bufsize, struct picomesh_error err)
{
    if (!buf || bufsize == 0) {
        return 0;
    }
    int written =
        snprintf(buf, bufsize, "%s\n    at %s:%d (%s)", err.msg ? err.msg : "<no message>",
                 err.file ? err.file : "<unknown>", err.line, err.func ? err.func : "<unknown>");
    if (written < 0) {
        buf[0] = '\0';
        return 0;
    }
    size_t off = (size_t)written < bufsize ? (size_t)written : bufsize - 1;
    for (const struct picomesh_error *cause = err.cause; cause; cause = cause->cause) {
        if (off >= bufsize - 1) {
            break;
        }
        int cause_len = snprintf(buf + off, bufsize - off, "\n  caused by: %s\n    at %s:%d (%s)",
                                 cause->msg ? cause->msg : "<no message>",
                                 cause->file ? cause->file : "<unknown>", cause->line,
                                 cause->func ? cause->func : "<unknown>");
        if (cause_len < 0) {
            break;
        }
        off += (size_t)cause_len < bufsize - off ? (size_t)cause_len : bufsize - 1 - off;
    }
    return off;
}
