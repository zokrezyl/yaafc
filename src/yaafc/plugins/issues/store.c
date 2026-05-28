/* issues — bug tracker.
 *
 *   open(repo_id, author_id)              → issue_id
 *   close(issue_id)                       → 1 closed / 0 unknown
 *   status(issue_id)                      → 0 unknown / 1 open / 2 closed
 *   count_open_in_repo(repo_id)           → number of open issues
 *   count_total                           → total tracked
 *
 * State: per-repo monotonically increasing issue ids in a flat
 * array. Real plugin persists via the storage backend; here it's
 * in-memory like every other stub in this scenario port. */

#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/class.h>

#include <stdint.h>

#define ISSUES_MAX 1024

struct issue_entry {
    uint32_t issue_id;
    uint32_t repo_id;
    uint32_t author_id;
    int closed;
    int used;
};

struct YAAFC_CLASS_ANNOTATE("class@issues:store") issues_store_data {
    struct issue_entry entries[ISSUES_MAX];
    size_t count;
    uint32_t next_id;
};

static struct issues_store_data *is_(struct object *obj)
{
    return (struct issues_store_data *)((char *)obj + sizeof(struct object));
}

YAAFC_CLASS_ANNOTATE("override@issues:store:store_open")
struct yaafc_uint32_result issues_store_open_impl(struct ctx *ctx, struct object *obj,
                                                  uint32_t repo_id, uint32_t author_id)
{
    (void)ctx;
    struct issues_store_data *d = is_(obj);
    if (d->next_id == 0) d->next_id = 1;
    for (size_t i = 0; i < ISSUES_MAX; ++i) {
        if (!d->entries[i].used) {
            d->entries[i].issue_id = d->next_id++;
            d->entries[i].repo_id = repo_id;
            d->entries[i].author_id = author_id;
            d->entries[i].closed = 0;
            d->entries[i].used = 1;
            d->count++;
            yinfo("issues: open id=%u repo=%u by=%u", d->entries[i].issue_id, repo_id, author_id);
            return YAAFC_OK(yaafc_uint32, d->entries[i].issue_id);
        }
    }
    return YAAFC_ERR(yaafc_uint32, "issues_open: table full");
}

YAAFC_CLASS_ANNOTATE("override@issues:store:store_close")
struct yaafc_int_result issues_store_close_impl(struct ctx *ctx, struct object *obj,
                                                uint32_t issue_id)
{
    (void)ctx;
    struct issues_store_data *d = is_(obj);
    for (size_t i = 0; i < ISSUES_MAX; ++i) {
        if (d->entries[i].used && d->entries[i].issue_id == issue_id &&
            !d->entries[i].closed) {
            d->entries[i].closed = 1;
            return YAAFC_OK(yaafc_int, 1);
        }
    }
    return YAAFC_OK(yaafc_int, 0);
}

YAAFC_CLASS_ANNOTATE("override@issues:store:store_status")
struct yaafc_int_result issues_store_status_impl(struct ctx *ctx, struct object *obj,
                                                 uint32_t issue_id)
{
    (void)ctx;
    struct issues_store_data *d = is_(obj);
    for (size_t i = 0; i < ISSUES_MAX; ++i) {
        if (d->entries[i].used && d->entries[i].issue_id == issue_id) {
            return YAAFC_OK(yaafc_int, d->entries[i].closed ? 2 : 1);
        }
    }
    return YAAFC_OK(yaafc_int, 0);
}

YAAFC_CLASS_ANNOTATE("override@issues:store:store_count_open_in_repo")
struct yaafc_size_result issues_store_count_open_in_repo_impl(struct ctx *ctx, struct object *obj,
                                                              uint32_t repo_id)
{
    (void)ctx;
    struct issues_store_data *d = is_(obj);
    size_t n = 0;
    for (size_t i = 0; i < ISSUES_MAX; ++i) {
        if (d->entries[i].used && d->entries[i].repo_id == repo_id && !d->entries[i].closed) {
            n++;
        }
    }
    return YAAFC_OK(yaafc_size, n);
}

YAAFC_CLASS_ANNOTATE("override@issues:store:store_count_total")
struct yaafc_size_result issues_store_count_total_impl(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    return YAAFC_OK(yaafc_size, is_(obj)->count);
}

#include "store.gen.c"
