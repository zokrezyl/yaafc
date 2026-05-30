#ifndef YAAFC_YCORE_RESULT_H
#define YAAFC_YCORE_RESULT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error info — head of a heap-linked chain. The top-of-chain error lives by
 * value inside a Result; deeper levels (the `cause` chain) are heap-allocated.
 *
 * Location: `file`, `line`, `func` are captured at the YAAFC_ERR call site
 * via __FILE__ / __LINE__ / __func__ — string literals from the binary's
 * read-only segment, never freed.
 *
 * Ownership: when a callee returns a Result with an error, the immediate
 * caller owns that error's `cause` chain. Two ways to discharge ownership:
 *   1) Wrap and forward: YAAFC_ERR(type, "outer", inner_res) transfers the
 *      cause chain into the new error.
 *   2) Drop: yaafc_error_destroy(inner_res.error) frees the chain. */
struct yaafc_error {
    const char *msg;
    const char *file;
    const char *func;
    int line;
    struct yaafc_error *cause;
};

#define YAAFC_RESULT_DECLARE(type, value_type)                                                     \
    struct type##_result {                                                                         \
        int ok;                                                                                    \
        union {                                                                                    \
            value_type value;                                                                      \
            struct yaafc_error error;                                                              \
        };                                                                                         \
    }

YAAFC_RESULT_DECLARE(yaafc_void, int);
YAAFC_RESULT_DECLARE(yaafc_int, int);
YAAFC_RESULT_DECLARE(yaafc_size, size_t);
YAAFC_RESULT_DECLARE(yaafc_int64, int64_t);
YAAFC_RESULT_DECLARE(yaafc_uint32, uint32_t);
/* Owned, heap-allocated, NUL-terminated string (or opaque bytes with a
 * trailing NUL). The OK consumer takes ownership of `.value` and must
 * free() it. Used by the storage `get` methods, whose values are
 * strings/bytes rather than fixed-width integers. */
YAAFC_RESULT_DECLARE(yaafc_string, char *);

struct yaafc_error *yaafc_error_chain(struct yaafc_error prev);
void yaafc_error_destroy(struct yaafc_error err);
void yaafc_error_print(FILE *out, const char *headline, struct yaafc_error err);
size_t yaafc_error_snprint(char *buf, size_t bufsize, struct yaafc_error err);

#define YAAFC_OK_VOID() ((struct yaafc_void_result){.ok = 1, .value = 0})
#define YAAFC_OK(type, val) ((struct type##_result){.ok = 1, .value = (val)})

#define YAAFC_ERR(...) YAAFC_ERR_DISPATCH(__VA_ARGS__, YAAFC_ERR_3, YAAFC_ERR_2)(__VA_ARGS__)
#define YAAFC_ERR_DISPATCH(_1, _2, _3, NAME, ...) NAME

#define YAAFC_ERR_2(type, err_msg)                                                                 \
    ((struct type##_result){.ok = 0,                                                               \
                            .error = {.msg = (err_msg),                                            \
                                      .file = __FILE__,                                            \
                                      .func = __func__,                                            \
                                      .line = __LINE__,                                            \
                                      .cause = NULL}})

#define YAAFC_ERR_3(type, err_msg, prev_res)                                                       \
    ((struct type##_result){.ok = 0,                                                               \
                            .error = {.msg = (err_msg),                                            \
                                      .file = __FILE__,                                            \
                                      .func = __func__,                                            \
                                      .line = __LINE__,                                            \
                                      .cause = yaafc_error_chain((prev_res).error)}})

#define YAAFC_IS_OK(res) ((res).ok)
#define YAAFC_IS_ERR(res) (!(res).ok)

#if defined(__clang__) || defined(__GNUC__)
#define YAAFC_EXTERNAL_CALLBACK __attribute__((annotate("yaafc_external_callback")))
#else
#define YAAFC_EXTERNAL_CALLBACK
#endif

#define YAAFC_RETURN_IF_ERR(type, res, msg)                                                        \
    do {                                                                                           \
        if (YAAFC_IS_ERR(res)) {                                                                   \
            return YAAFC_ERR(type, msg, (res));                                                    \
        }                                                                                          \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* YAAFC_YCORE_RESULT_H */
