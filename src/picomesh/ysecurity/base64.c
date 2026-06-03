/* base64url encode/decode — URL-safe alphabet, no padding. */

#include <picomesh/ysecurity/base64.h>

size_t picomesh_base64url_encode(const void *data, size_t len, char *out, size_t out_cap)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const uint8_t *bytes = data;
    size_t out_len = (len + 2) / 3 * 4;
    /* Trim the padding characters base64url omits. */
    if (len % 3 == 1) out_len -= 2;
    else if (len % 3 == 2) out_len -= 1;
    if (out_cap < out_len + 1) return 0;

    size_t written = 0;
    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t triple = (uint32_t)bytes[i] << 16 | (uint32_t)bytes[i + 1] << 8 | bytes[i + 2];
        out[written++] = alphabet[(triple >> 18) & 0x3f];
        out[written++] = alphabet[(triple >> 12) & 0x3f];
        out[written++] = alphabet[(triple >> 6) & 0x3f];
        out[written++] = alphabet[triple & 0x3f];
        i += 3;
    }
    size_t remaining = len - i;
    if (remaining == 1) {
        uint32_t triple = (uint32_t)bytes[i] << 16;
        out[written++] = alphabet[(triple >> 18) & 0x3f];
        out[written++] = alphabet[(triple >> 12) & 0x3f];
    } else if (remaining == 2) {
        uint32_t triple = (uint32_t)bytes[i] << 16 | (uint32_t)bytes[i + 1] << 8;
        out[written++] = alphabet[(triple >> 18) & 0x3f];
        out[written++] = alphabet[(triple >> 12) & 0x3f];
        out[written++] = alphabet[(triple >> 6) & 0x3f];
    }
    out[written] = 0;
    return written;
}

static int decode_symbol(char symbol)
{
    if (symbol >= 'A' && symbol <= 'Z') return symbol - 'A';
    if (symbol >= 'a' && symbol <= 'z') return symbol - 'a' + 26;
    if (symbol >= '0' && symbol <= '9') return symbol - '0' + 52;
    if (symbol == '-') return 62;
    if (symbol == '_') return 63;
    return -1;
}

size_t picomesh_base64url_decode(const char *text, uint8_t *out, size_t out_cap)
{
    size_t text_len = 0;
    while (text[text_len]) ++text_len;
    /* Tolerate stray '=' padding even though base64url omits it. */
    while (text_len > 0 && text[text_len - 1] == '=') --text_len;

    size_t written = 0;
    uint32_t accumulator = 0;
    unsigned bits = 0;
    for (size_t i = 0; i < text_len; ++i) {
        int value = decode_symbol(text[i]);
        if (value < 0) return (size_t)-1;
        accumulator = (accumulator << 6) | (uint32_t)value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (written >= out_cap) return (size_t)-1;
            out[written++] = (uint8_t)((accumulator >> bits) & 0xff);
        }
    }
    return written;
}
