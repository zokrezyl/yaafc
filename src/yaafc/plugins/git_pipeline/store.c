/* git_pipeline — CI job queue.
 *
 *   enqueue(repo_id)        → job_id
 *   lease(runner_id)        → job_id (0 if queue empty)
 *   complete(job_id, status)→ 1 ok / 0 unknown
 *   count_pending           → still in queued state
 *   count_running           → leased but not yet completed
 *   count_done              → completed
 *
 * Status: 0=queued, 1=running, 2=succeeded, 3=failed. */

#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/class.h>

#include <stdint.h>

#define PIPE_MAX 256

struct job_entry {
    uint32_t job_id;
    uint32_t repo_id;
    uint32_t runner_id; /* 0 = unassigned */
    int status;         /* 0 q, 1 r, 2 s, 3 f */
    int used;
};

struct YAAFC_CLASS_ANNOTATE("class@git_pipeline:store") git_pipeline_store_data {
    struct job_entry entries[PIPE_MAX];
    size_t count;
    uint32_t next_id;
};

static struct git_pipeline_store_data *gp(struct object *obj)
{
    return (struct git_pipeline_store_data *)((char *)obj + sizeof(struct object));
}

YAAFC_CLASS_ANNOTATE("override@git_pipeline:store:store_enqueue")
struct yaafc_uint32_result git_pipeline_store_enqueue_impl(struct ctx *ctx, struct object *obj,
                                                           uint32_t repo_id)
{
    (void)ctx;
    struct git_pipeline_store_data *d = gp(obj);
    if (d->next_id == 0) d->next_id = 1;
    for (size_t i = 0; i < PIPE_MAX; ++i) {
        if (!d->entries[i].used) {
            d->entries[i].job_id = d->next_id++;
            d->entries[i].repo_id = repo_id;
            d->entries[i].runner_id = 0;
            d->entries[i].status = 0;
            d->entries[i].used = 1;
            d->count++;
            return YAAFC_OK(yaafc_uint32, d->entries[i].job_id);
        }
    }
    return YAAFC_ERR(yaafc_uint32, "git_pipeline_enqueue: queue full");
}

YAAFC_CLASS_ANNOTATE("override@git_pipeline:store:store_lease")
struct yaafc_uint32_result git_pipeline_store_lease_impl(struct ctx *ctx, struct object *obj,
                                                         uint32_t runner_id)
{
    (void)ctx;
    struct git_pipeline_store_data *d = gp(obj);
    /* FIFO scan: first queued slot wins */
    for (size_t i = 0; i < PIPE_MAX; ++i) {
        if (d->entries[i].used && d->entries[i].status == 0) {
            d->entries[i].status = 1;
            d->entries[i].runner_id = runner_id;
            yinfo("git_pipeline: lease job=%u to runner=%u",
                  d->entries[i].job_id, runner_id);
            return YAAFC_OK(yaafc_uint32, d->entries[i].job_id);
        }
    }
    return YAAFC_OK(yaafc_uint32, 0);
}

YAAFC_CLASS_ANNOTATE("override@git_pipeline:store:store_complete")
struct yaafc_int_result git_pipeline_store_complete_impl(struct ctx *ctx, struct object *obj,
                                                         uint32_t job_id, int32_t status)
{
    (void)ctx;
    struct git_pipeline_store_data *d = gp(obj);
    int final_status = status == 0 ? 2 : 3;
    for (size_t i = 0; i < PIPE_MAX; ++i) {
        if (d->entries[i].used && d->entries[i].job_id == job_id) {
            d->entries[i].status = final_status;
            return YAAFC_OK(yaafc_int, 1);
        }
    }
    return YAAFC_OK(yaafc_int, 0);
}

static size_t count_status(const struct git_pipeline_store_data *d, int status)
{
    size_t n = 0;
    for (size_t i = 0; i < PIPE_MAX; ++i) {
        if (d->entries[i].used && d->entries[i].status == status) n++;
    }
    return n;
}

YAAFC_CLASS_ANNOTATE("override@git_pipeline:store:store_count_pending")
struct yaafc_size_result git_pipeline_store_count_pending_impl(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    return YAAFC_OK(yaafc_size, count_status(gp(obj), 0));
}

YAAFC_CLASS_ANNOTATE("override@git_pipeline:store:store_count_running")
struct yaafc_size_result git_pipeline_store_count_running_impl(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    return YAAFC_OK(yaafc_size, count_status(gp(obj), 1));
}

YAAFC_CLASS_ANNOTATE("override@git_pipeline:store:store_count_done")
struct yaafc_size_result git_pipeline_store_count_done_impl(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    const struct git_pipeline_store_data *d = gp(obj);
    return YAAFC_OK(yaafc_size, count_status(d, 2) + count_status(d, 3));
}

#include "store.gen.c"
