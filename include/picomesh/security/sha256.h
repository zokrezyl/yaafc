/* sha256 — self-contained SHA-256 (FIPS 180-4) and HMAC-SHA256.
 *
 * No external crypto dependency: picomesh links no openssl/mbedtls, and the
 * JWT/HS256 path needs exactly these two primitives. Implementation is the
 * standard public-domain construction.
 *
 * picomesh_sha256 writes a 32-byte digest. picomesh_hmac_sha256 computes the
 * keyed MAC used to sign/verify HS256 JWTs. */

#ifndef PICOMESH_SECURITY_SHA256_H
#define PICOMESH_SECURITY_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PICOMESH_SHA256_DIGEST_LEN 32u
#define PICOMESH_SHA256_BLOCK_LEN 64u

struct picomesh_sha256_ctx {
  uint64_t total_len;
  uint32_t state[8];
  uint8_t block[PICOMESH_SHA256_BLOCK_LEN];
  size_t block_len;
};

void picomesh_sha256_init(struct picomesh_sha256_ctx *ctx);
void picomesh_sha256_update(struct picomesh_sha256_ctx *ctx, const void *data,
                            size_t len);
void picomesh_sha256_final(struct picomesh_sha256_ctx *ctx,
                           uint8_t out_digest[PICOMESH_SHA256_DIGEST_LEN]);

/* One-shot convenience. */
void picomesh_sha256(const void *data, size_t len,
                     uint8_t out_digest[PICOMESH_SHA256_DIGEST_LEN]);

/* HMAC-SHA256 over `message` keyed by `key`. */
void picomesh_hmac_sha256(const void *key, size_t key_len, const void *message,
                          size_t message_len,
                          uint8_t out_mac[PICOMESH_SHA256_DIGEST_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_SECURITY_SHA256_H */
