/* GENERATED — do not edit. */
#include "git_pipeline.internal.h"

__attribute__((unused))
static git_pipeline_store_enqueue_fn _git_pipeline_store_git_pipeline_store_enqueue_check = git_pipeline_store_enqueue_impl;
__attribute__((unused))
static git_pipeline_store_lease_fn _git_pipeline_store_git_pipeline_store_lease_check = git_pipeline_store_lease_impl;
__attribute__((unused))
static git_pipeline_store_complete_fn _git_pipeline_store_git_pipeline_store_complete_check = git_pipeline_store_complete_impl;
__attribute__((unused))
static git_pipeline_store_count_pending_fn _git_pipeline_store_git_pipeline_store_count_pending_check = git_pipeline_store_count_pending_impl;
__attribute__((unused))
static git_pipeline_store_count_running_fn _git_pipeline_store_git_pipeline_store_count_running_check = git_pipeline_store_count_running_impl;
__attribute__((unused))
static git_pipeline_store_count_done_fn _git_pipeline_store_git_pipeline_store_count_done_check = git_pipeline_store_count_done_impl;

struct class_ptr_result git_pipeline_store_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YAAFC_OK(class_ptr, cls);
    ydebug("registering class=git_pipeline_store");

    static const struct class_descriptor desc = {
        .name = "git_pipeline_store",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct git_pipeline_store_data),
    };
    static const struct op ops[] = {
        {"git_pipeline", "store_enqueue", (method_id_t)git_pipeline_store_enqueue, (impl_t)git_pipeline_store_enqueue_impl},
        {"git_pipeline", "store_lease", (method_id_t)git_pipeline_store_lease, (impl_t)git_pipeline_store_lease_impl},
        {"git_pipeline", "store_complete", (method_id_t)git_pipeline_store_complete, (impl_t)git_pipeline_store_complete_impl},
        {"git_pipeline", "store_count_pending", (method_id_t)git_pipeline_store_count_pending, (impl_t)git_pipeline_store_count_pending_impl},
        {"git_pipeline", "store_count_running", (method_id_t)git_pipeline_store_count_running, (impl_t)git_pipeline_store_count_running_impl},
        {"git_pipeline", "store_count_done", (method_id_t)git_pipeline_store_count_done, (impl_t)git_pipeline_store_count_done_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YAAFC_IS_ERR(_r))
        return YAAFC_ERR(class_ptr, "git_pipeline_store_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
