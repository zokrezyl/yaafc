/* base64url (RFC 4648 §5) — the URL-safe alphabet with no '=' padding,
 * which is exactly the encoding JWT uses for its three segments. */

#ifndef PICOMESH_SECURITY_BASE64_H
#define PICOMESH_SECURITY_BASE64_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Encode `len` bytes into base64url (no padding) at `out`. Returns the
 * number of characters written (excluding the NUL it also writes), or 0 if
 * `out_cap` is too small. The encoded length is ((len + 2) / 3) * 4 minus
 * padding; pass out_cap >= len*4/3 + 2 to be safe. */
size_t picomesh_base64url_encode(const void *data, size_t len, char *out,
                                 size_t out_cap);

/* Decode base64url text (NUL-terminated `text`, no padding required) into
 * `out`. Returns the number of bytes written, or (size_t)-1 on malformed
 * input or insufficient `out_cap`. */
size_t picomesh_base64url_decode(const char *text, uint8_t *out,
                                 size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_SECURITY_BASE64_H */
