/* yheaders — generic request-header bag.
 *
 * A small ordered vector of (key, value) string pairs that travels with
 * every method call as an explicit argument (after ctx and obj), wholly
 * independent of the framework dispatch context (`struct ctx`, which is
 * only the `session`). Any service or frontend can inject any header;
 * well-known keys ("uid", "sid", "trace_id") are just conventions, not
 * baked-in fields.
 *
 * The FRAMEWORK serializes/deserializes this — not the per-method
 * codegen. The yrpc wire carries it as a length-prefixed section ahead
 * of the packed business args; the JSON `/_rpc` envelope carries it as a
 * `headers` object. The codegen only threads the `struct yheaders *`
 * argument through; it never inspects header contents. */

#ifndef PICOMESH_PICOCLASS_YHEADERS_H
#define PICOMESH_PICOCLASS_YHEADERS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yheaders;

/* Lifecycle. yheaders_new returns NULL only on OOM. */
struct yheaders *yheaders_new(void);
void yheaders_free(struct yheaders *h);
struct yheaders *yheaders_copy(const struct yheaders *h);

/* set replaces an existing key in place; otherwise appends. Returns 0
 * on success, -1 on OOM / bad args. Key and value are copied. */
int yheaders_set(struct yheaders *h, const char *key, const char *value);
/* get returns the stored value (owned by `h`) or NULL if absent. */
const char *yheaders_get(const struct yheaders *h, const char *key);

/* Typed convenience — stored/parsed as decimal strings so the wire
 * stays a uniform key→string vector. get_* return `fallback` when the
 * key is absent or unparseable. */
int yheaders_set_u32(struct yheaders *h, const char *key, uint32_t v);
uint32_t yheaders_get_u32(const struct yheaders *h, const char *key, uint32_t fallback);
int yheaders_set_u64(struct yheaders *h, const char *key, uint64_t v);
uint64_t yheaders_get_u64(const struct yheaders *h, const char *key, uint64_t fallback);

size_t yheaders_count(const struct yheaders *h);
void yheaders_for_each(const struct yheaders *h,
                       void (*cb)(const char *key, const char *value, void *ud),
                       void *ud);

/* ---- framework wire (de)serialization ----------------------------- */
/* Format: u16 count, then per pair { u16 klen, key[klen], u32 vlen,
 * val[vlen] }. Keys/values are raw bytes (NUL-terminated in memory but
 * length-framed on the wire). */

/* Bytes the serialized form will occupy. */
size_t yheaders_serialized_size(const struct yheaders *h);
/* Serialize into buf[cap]. Returns bytes written, or 0 if cap is too
 * small. A NULL `h` serializes as an empty bag (count 0). */
size_t yheaders_serialize(const struct yheaders *h, void *buf, size_t cap);
/* Parse `len` bytes; on success returns a new bag and (if non-NULL)
 * writes the number of bytes consumed to *consumed. NULL on malformed
 * input. */
struct yheaders *yheaders_parse(const void *buf, size_t len, size_t *consumed);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_PICOCLASS_YHEADERS_H */
