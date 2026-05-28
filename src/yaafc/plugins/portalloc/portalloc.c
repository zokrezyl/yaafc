/* portalloc — port allocator.
 *
 * The mesh assigns this plugin a fixed port (8200 in the scenario);
 * every other service asks portalloc for a free port at spawn time.
 *
 * Methods:
 *   allocate(service_id) → uint32 port (0 on full)
 *   release(port)        → 1 ok, 0 unknown
 *   count_used()         → size of in-use set
 *
 * `service_id` is the requester's stable id. Calling allocate twice
 * with the same id returns the same port — idempotent across crashes
 * once the persistence file is wired up (TODO). */

#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/class.h>

#include <stdint.h>

#define PORTALLOC_RANGE_LO 8201
#define PORTALLOC_RANGE_HI 8299
#define PORTALLOC_MAX_ENTRIES ((PORTALLOC_RANGE_HI - PORTALLOC_RANGE_LO) + 1)

struct port_entry {
    uint32_t service_id;
    uint32_t port;
    int used;
};

struct YAAFC_CLASS_ANNOTATE("class@portalloc:store") portalloc_store_data {
    struct port_entry entries[PORTALLOC_MAX_ENTRIES];
    size_t count;
};

static struct portalloc_store_data *pa(struct object *obj)
{
    return (struct portalloc_store_data *)((char *)obj + sizeof(struct object));
}

YAAFC_CLASS_ANNOTATE("override@portalloc:store:store_allocate")
struct yaafc_uint32_result portalloc_store_allocate_impl(struct ctx *ctx, struct object *obj,
                                                         uint32_t service_id)
{
    (void)ctx;
    struct portalloc_store_data *d = pa(obj);
    /* idempotent: already-allocated → same port */
    for (size_t i = 0; i < PORTALLOC_MAX_ENTRIES; ++i) {
        if (d->entries[i].used && d->entries[i].service_id == service_id) {
            return YAAFC_OK(yaafc_uint32, d->entries[i].port);
        }
    }
    /* first-fit on the range */
    for (uint32_t p = PORTALLOC_RANGE_LO; p <= PORTALLOC_RANGE_HI; ++p) {
        int taken = 0;
        for (size_t i = 0; i < PORTALLOC_MAX_ENTRIES; ++i) {
            if (d->entries[i].used && d->entries[i].port == p) { taken = 1; break; }
        }
        if (taken) continue;
        for (size_t i = 0; i < PORTALLOC_MAX_ENTRIES; ++i) {
            if (!d->entries[i].used) {
                d->entries[i].service_id = service_id;
                d->entries[i].port = p;
                d->entries[i].used = 1;
                d->count++;
                yinfo("portalloc: service %u → port %u", service_id, p);
                return YAAFC_OK(yaafc_uint32, p);
            }
        }
    }
    return YAAFC_ERR(yaafc_uint32, "portalloc_allocate: no ports left in range");
}

YAAFC_CLASS_ANNOTATE("override@portalloc:store:store_release")
struct yaafc_int_result portalloc_store_release_impl(struct ctx *ctx, struct object *obj,
                                                     uint32_t port)
{
    (void)ctx;
    struct portalloc_store_data *d = pa(obj);
    for (size_t i = 0; i < PORTALLOC_MAX_ENTRIES; ++i) {
        if (d->entries[i].used && d->entries[i].port == port) {
            d->entries[i].used = 0;
            d->count--;
            yinfo("portalloc: released port %u", port);
            return YAAFC_OK(yaafc_int, 1);
        }
    }
    return YAAFC_OK(yaafc_int, 0);
}

YAAFC_CLASS_ANNOTATE("override@portalloc:store:store_count_used")
struct yaafc_size_result portalloc_store_count_used_impl(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    return YAAFC_OK(yaafc_size, pa(obj)->count);
}

#include "portalloc.gen.c"
