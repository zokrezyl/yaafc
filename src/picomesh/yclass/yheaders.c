/* yheaders — generic request-header bag (see yheaders.h). */

#include <picomesh/yclass/yheaders.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct yheaders_pair {
    char *key;
    char *value;
};

struct yheaders {
    struct yheaders_pair *items;
    size_t len;
    size_t cap;
};

struct yheaders *yheaders_new(void)
{
    return calloc(1, sizeof(struct yheaders));
}

void yheaders_free(struct yheaders *headers)
{
    if (!headers) return;
    for (size_t i = 0; i < headers->len; ++i) {
        free(headers->items[i].key);
        free(headers->items[i].value);
    }
    free(headers->items);
    free(headers);
}

static struct yheaders_pair *find(const struct yheaders *headers, const char *key)
{
    if (!headers || !key) return NULL;
    for (size_t i = 0; i < headers->len; ++i)
        if (strcmp(headers->items[i].key, key) == 0)
            return &headers->items[i];
    return NULL;
}

int yheaders_set(struct yheaders *headers, const char *key, const char *value)
{
    if (!headers || !key) return -1;
    if (!value) value = "";

    struct yheaders_pair *pair = find(headers, key);
    if (pair) {
        char *new_value = strdup(value);
        if (!new_value) return -1;
        free(pair->value);
        pair->value = new_value;
        return 0;
    }

    if (headers->len == headers->cap) {
        size_t new_cap = headers->cap ? headers->cap * 2 : 8;
        struct yheaders_pair *new_items = realloc(headers->items, new_cap * sizeof(*new_items));
        if (!new_items) return -1;
        headers->items = new_items;
        headers->cap = new_cap;
    }
    char *key_copy = strdup(key);
    char *value_copy = strdup(value);
    if (!key_copy || !value_copy) { free(key_copy); free(value_copy); return -1; }
    headers->items[headers->len].key = key_copy;
    headers->items[headers->len].value = value_copy;
    headers->len++;
    return 0;
}

const char *yheaders_get(const struct yheaders *headers, const char *key)
{
    struct yheaders_pair *pair = find(headers, key);
    return pair ? pair->value : NULL;
}

int yheaders_set_u32(struct yheaders *headers, const char *key, uint32_t value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%" PRIu32, value);
    return yheaders_set(headers, key, buf);
}

uint32_t yheaders_get_u32(const struct yheaders *headers, const char *key, uint32_t fallback)
{
    const char *value = yheaders_get(headers, key);
    if (!value || !*value) return fallback;
    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    return (end && *end == 0) ? (uint32_t)parsed : fallback;
}

int yheaders_set_u64(struct yheaders *headers, const char *key, uint64_t value)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%" PRIu64, value);
    return yheaders_set(headers, key, buf);
}

uint64_t yheaders_get_u64(const struct yheaders *headers, const char *key, uint64_t fallback)
{
    const char *value = yheaders_get(headers, key);
    if (!value || !*value) return fallback;
    char *end = NULL;
    unsigned long long parsed = strtoull(value, &end, 10);
    return (end && *end == 0) ? (uint64_t)parsed : fallback;
}

size_t yheaders_count(const struct yheaders *headers)
{
    return headers ? headers->len : 0;
}

void yheaders_for_each(const struct yheaders *headers,
                       void (*cb)(const char *key, const char *value, void *ud),
                       void *userdata)
{
    if (!headers || !cb) return;
    for (size_t i = 0; i < headers->len; ++i)
        cb(headers->items[i].key, headers->items[i].value, userdata);
}

struct yheaders *yheaders_copy(const struct yheaders *headers)
{
    struct yheaders *out = yheaders_new();
    if (!out || !headers) return out;
    for (size_t i = 0; i < headers->len; ++i) {
        if (yheaders_set(out, headers->items[i].key, headers->items[i].value) != 0) {
            yheaders_free(out);
            return NULL;
        }
    }
    return out;
}

/* ---- wire (de)serialization --------------------------------------- */
/* u16 count, then per pair: u16 klen, key, u32 vlen, val. */

size_t yheaders_serialized_size(const struct yheaders *headers)
{
    size_t total = 2; /* count */
    if (!headers) return total;
    for (size_t i = 0; i < headers->len; ++i) {
        total += 2 + strlen(headers->items[i].key);
        total += 4 + strlen(headers->items[i].value);
    }
    return total;
}

size_t yheaders_serialize(const struct yheaders *headers, void *buf, size_t cap)
{
    size_t need = yheaders_serialized_size(headers);
    if (cap < need) return 0;

    uint8_t *bytes = buf;
    size_t offset = 0;
    uint16_t count = headers ? (uint16_t)headers->len : 0;
    memcpy(bytes + offset, &count, 2); offset += 2;
    if (!headers) return offset;

    for (size_t i = 0; i < headers->len; ++i) {
        uint16_t klen = (uint16_t)strlen(headers->items[i].key);
        uint32_t vlen = (uint32_t)strlen(headers->items[i].value);
        memcpy(bytes + offset, &klen, 2); offset += 2;
        memcpy(bytes + offset, headers->items[i].key, klen); offset += klen;
        memcpy(bytes + offset, &vlen, 4); offset += 4;
        memcpy(bytes + offset, headers->items[i].value, vlen); offset += vlen;
    }
    return offset;
}

struct yheaders *yheaders_parse(const void *buf, size_t len, size_t *consumed)
{
    const uint8_t *bytes = buf;
    size_t offset = 0;
    if (len < 2) return NULL;
    uint16_t count;
    memcpy(&count, bytes + offset, 2); offset += 2;

    struct yheaders *headers = yheaders_new();
    if (!headers) return NULL;

    for (uint16_t i = 0; i < count; ++i) {
        if (offset + 2 > len) goto bad;
        uint16_t klen;
        memcpy(&klen, bytes + offset, 2); offset += 2;
        if (offset + klen > len) goto bad;
        char *key = malloc((size_t)klen + 1);
        if (!key) goto bad;
        memcpy(key, bytes + offset, klen); key[klen] = 0; offset += klen;

        if (offset + 4 > len) { free(key); goto bad; }
        uint32_t vlen;
        memcpy(&vlen, bytes + offset, 4); offset += 4;
        if (offset + vlen > len) { free(key); goto bad; }
        char *val = malloc((size_t)vlen + 1);
        if (!val) { free(key); goto bad; }
        memcpy(val, bytes + offset, vlen); val[vlen] = 0; offset += vlen;

        int set_rc = yheaders_set(headers, key, val);
        free(key);
        free(val);
        if (set_rc != 0) goto bad;
    }

    if (consumed) *consumed = offset;
    return headers;
bad:
    yheaders_free(headers);
    return NULL;
}
