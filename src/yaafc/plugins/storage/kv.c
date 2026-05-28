/* Storage example — a single class with set/get/count methods.
 *
 * Mirrors yaapp's `examples/plugins/storage` (key-value store, exposed
 * via a few methods). All public-stub plumbing + the RPC skels are
 * generated from these annotations; the file you're reading is the
 * only thing a user writes. */

#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/class.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KV_MAX_ENTRIES 64
#define KV_KEY_MAX     32

struct entry {
    char key[KV_KEY_MAX];
    int32_t value;
    int used;
};

struct YAAFC_CLASS_ANNOTATE("class@storage:kv") storage_kv_data {
    struct entry entries[KV_MAX_ENTRIES];
    size_t count;
};

static struct storage_kv_data *kv_data(struct object *obj)
{
    return (struct storage_kv_data *)((char *)obj + sizeof(struct object));
}

YAAFC_CLASS_ANNOTATE("override@storage:kv:kv_set")
struct yaafc_int_result storage_kv_set_impl(struct ctx *ctx, struct object *obj,
                                             uint32_t key_id, int32_t value)
{
    (void)ctx;
    struct storage_kv_data *d = kv_data(obj);
    char key[KV_KEY_MAX];
    snprintf(key, sizeof(key), "k%u", (unsigned)key_id);

    for (size_t i = 0; i < KV_MAX_ENTRIES; ++i) {
        if (d->entries[i].used && strcmp(d->entries[i].key, key) == 0) {
            d->entries[i].value = value;
            yinfo("kv_set: updated %s=%d", key, value);
            return YAAFC_OK(yaafc_int, 1);
        }
    }
    for (size_t i = 0; i < KV_MAX_ENTRIES; ++i) {
        if (!d->entries[i].used) {
            strncpy(d->entries[i].key, key, KV_KEY_MAX - 1);
            d->entries[i].value = value;
            d->entries[i].used = 1;
            d->count++;
            yinfo("kv_set: inserted %s=%d", key, value);
            return YAAFC_OK(yaafc_int, 1);
        }
    }
    return YAAFC_ERR(yaafc_int, "kv_set: store full");
}

YAAFC_CLASS_ANNOTATE("override@storage:kv:kv_get")
struct yaafc_int_result storage_kv_get_impl(struct ctx *ctx, struct object *obj,
                                             uint32_t key_id)
{
    (void)ctx;
    struct storage_kv_data *d = kv_data(obj);
    char key[KV_KEY_MAX];
    snprintf(key, sizeof(key), "k%u", (unsigned)key_id);

    for (size_t i = 0; i < KV_MAX_ENTRIES; ++i) {
        if (d->entries[i].used && strcmp(d->entries[i].key, key) == 0) {
            yinfo("kv_get: %s -> %d", key, d->entries[i].value);
            return YAAFC_OK(yaafc_int, d->entries[i].value);
        }
    }
    return YAAFC_ERR(yaafc_int, "kv_get: key not found");
}

YAAFC_CLASS_ANNOTATE("override@storage:kv:kv_count")
struct yaafc_size_result storage_kv_count_impl(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    struct storage_kv_data *d = kv_data(obj);
    yinfo("kv_count: %zu entries", d->count);
    return YAAFC_OK(yaafc_size, d->count);
}

/* Pull in the generated accessor — emitted by src/yaafc/yclass/gen/codegen.py and
 * compiled in-line so the static class descriptor table lives in this
 * translation unit. */
#include "kv.gen.c"
