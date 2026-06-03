/* SHA-256 (FIPS 180-4) + HMAC-SHA256. Standard construction, no deps. */

#include <picomesh/ysecurity/sha256.h>

#include <string.h>

static uint32_t rotate_right(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32u - bits));
}

static void sha256_transform(uint32_t state[8], const uint8_t block[PICOMESH_SHA256_BLOCK_LEN])
{
    /* Round constants — held as a function-local static const so no
     * file-scope data symbol is emitted. */
    static const uint32_t round_constants[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
        0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
        0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
        0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };

    uint32_t words[64];
    for (unsigned i = 0; i < 16; ++i) {
        words[i] = (uint32_t)block[i * 4 + 0] << 24 |
                   (uint32_t)block[i * 4 + 1] << 16 |
                   (uint32_t)block[i * 4 + 2] << 8 |
                   (uint32_t)block[i * 4 + 3];
    }
    for (unsigned i = 16; i < 64; ++i) {
        uint32_t s0 = rotate_right(words[i - 15], 7) ^ rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
        uint32_t s1 = rotate_right(words[i - 2], 17) ^ rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (unsigned i = 0; i < 64; ++i) {
        uint32_t big_s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t temp1 = h + big_s1 + choose + round_constants[i] + words[i];
        uint32_t big_s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = big_s0 + majority;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void picomesh_sha256_init(struct picomesh_sha256_ctx *ctx)
{
    ctx->total_len = 0;
    ctx->block_len = 0;
    ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
}

void picomesh_sha256_update(struct picomesh_sha256_ctx *ctx, const void *data, size_t len)
{
    const uint8_t *bytes = data;
    ctx->total_len += len;
    while (len > 0) {
        size_t room = PICOMESH_SHA256_BLOCK_LEN - ctx->block_len;
        size_t take = len < room ? len : room;
        memcpy(ctx->block + ctx->block_len, bytes, take);
        ctx->block_len += take;
        bytes += take;
        len -= take;
        if (ctx->block_len == PICOMESH_SHA256_BLOCK_LEN) {
            sha256_transform(ctx->state, ctx->block);
            ctx->block_len = 0;
        }
    }
}

void picomesh_sha256_final(struct picomesh_sha256_ctx *ctx, uint8_t out_digest[PICOMESH_SHA256_DIGEST_LEN])
{
    uint64_t bit_len = ctx->total_len * 8u;
    /* Append 0x80, then pad with zeros until 56 mod 64, then the 64-bit length. */
    uint8_t one = 0x80;
    picomesh_sha256_update(ctx, &one, 1);
    uint8_t zero = 0;
    while (ctx->block_len != 56)
        picomesh_sha256_update(ctx, &zero, 1);
    uint8_t length_bytes[8];
    for (unsigned i = 0; i < 8; ++i)
        length_bytes[i] = (uint8_t)(bit_len >> (56u - i * 8u));
    picomesh_sha256_update(ctx, length_bytes, 8);

    for (unsigned i = 0; i < 8; ++i) {
        out_digest[i * 4 + 0] = (uint8_t)(ctx->state[i] >> 24);
        out_digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        out_digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        out_digest[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

void picomesh_sha256(const void *data, size_t len, uint8_t out_digest[PICOMESH_SHA256_DIGEST_LEN])
{
    struct picomesh_sha256_ctx ctx;
    picomesh_sha256_init(&ctx);
    picomesh_sha256_update(&ctx, data, len);
    picomesh_sha256_final(&ctx, out_digest);
}

void picomesh_hmac_sha256(const void *key, size_t key_len,
                          const void *message, size_t message_len,
                          uint8_t out_mac[PICOMESH_SHA256_DIGEST_LEN])
{
    uint8_t key_block[PICOMESH_SHA256_BLOCK_LEN] = {0};
    if (key_len > PICOMESH_SHA256_BLOCK_LEN) {
        picomesh_sha256(key, key_len, key_block);
    } else {
        memcpy(key_block, key, key_len);
    }

    uint8_t inner_pad[PICOMESH_SHA256_BLOCK_LEN];
    uint8_t outer_pad[PICOMESH_SHA256_BLOCK_LEN];
    for (unsigned i = 0; i < PICOMESH_SHA256_BLOCK_LEN; ++i) {
        inner_pad[i] = (uint8_t)(key_block[i] ^ 0x36);
        outer_pad[i] = (uint8_t)(key_block[i] ^ 0x5c);
    }

    uint8_t inner_digest[PICOMESH_SHA256_DIGEST_LEN];
    struct picomesh_sha256_ctx ctx;
    picomesh_sha256_init(&ctx);
    picomesh_sha256_update(&ctx, inner_pad, sizeof(inner_pad));
    picomesh_sha256_update(&ctx, message, message_len);
    picomesh_sha256_final(&ctx, inner_digest);

    picomesh_sha256_init(&ctx);
    picomesh_sha256_update(&ctx, outer_pad, sizeof(outer_pad));
    picomesh_sha256_update(&ctx, inner_digest, sizeof(inner_digest));
    picomesh_sha256_final(&ctx, out_mac);
}
