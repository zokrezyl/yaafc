/* GENERATED — do not edit. */
#include "git_pipeline.internal.h"

__attribute__((unused))
static git_pipeline_git_pipeline_enqueue_fn _git_pipeline_git_pipeline_git_pipeline_git_pipeline_enqueue_check = git_pipeline_git_pipeline_enqueue_impl;
__attribute__((unused))
static git_pipeline_git_pipeline_enqueue_job_fn _git_pipeline_git_pipeline_git_pipeline_git_pipeline_enqueue_job_check = git_pipeline_git_pipeline_enqueue_job_impl;
__attribute__((unused))
static git_pipeline_git_pipeline_lease_fn _git_pipeline_git_pipeline_git_pipeline_git_pipeline_lease_check = git_pipeline_git_pipeline_lease_impl;
__attribute__((unused))
static git_pipeline_git_pipeline_lease_job_fn _git_pipeline_git_pipeline_git_pipeline_git_pipeline_lease_job_check = git_pipeline_git_pipeline_lease_job_impl;
__attribute__((unused))
static git_pipeline_git_pipeline_job_descriptor_fn _git_pipeline_git_pipeline_git_pipeline_git_pipeline_job_descriptor_check = git_pipeline_git_pipeline_job_descriptor_impl;
__attribute__((unused))
static git_pipeline_git_pipeline_append_log_fn _git_pipeline_git_pipeline_git_pipeline_git_pipeline_append_log_check = git_pipeline_git_pipeline_append_log_impl;
__attribute__((unused))
static git_pipeline_git_pipeline_read_log_fn _git_pipeline_git_pipeline_git_pipeline_git_pipeline_read_log_check = git_pipeline_git_pipeline_read_log_impl;
__attribute__((unused))
static git_pipeline_git_pipeline_complete_fn _git_pipeline_git_pipeline_git_pipeline_git_pipeline_complete_check = git_pipeline_git_pipeline_complete_impl;
__attribute__((unused))
static git_pipeline_git_pipeline_complete_job_fn _git_pipeline_git_pipeline_git_pipeline_git_pipeline_complete_job_check = git_pipeline_git_pipeline_complete_job_impl;
__attribute__((unused))
static git_pipeline_git_pipeline_requeue_expired_fn _git_pipeline_git_pipeline_git_pipeline_git_pipeline_requeue_expired_check = git_pipeline_git_pipeline_requeue_expired_impl;
__attribute__((unused))
static git_pipeline_git_pipeline_count_pending_fn _git_pipeline_git_pipeline_git_pipeline_git_pipeline_count_pending_check = git_pipeline_git_pipeline_count_pending_impl;
__attribute__((unused))
static git_pipeline_git_pipeline_count_running_fn _git_pipeline_git_pipeline_git_pipeline_git_pipeline_count_running_check = git_pipeline_git_pipeline_count_running_impl;
__attribute__((unused))
static git_pipeline_git_pipeline_count_done_fn _git_pipeline_git_pipeline_git_pipeline_git_pipeline_count_done_check = git_pipeline_git_pipeline_count_done_impl;
__attribute__((unused))
static git_pipeline_git_pipeline_list_fn _git_pipeline_git_pipeline_git_pipeline_git_pipeline_list_check = git_pipeline_git_pipeline_list_impl;
__attribute__((unused))
static git_pipeline_git_pipeline_list_all_fn _git_pipeline_git_pipeline_git_pipeline_git_pipeline_list_all_check = git_pipeline_git_pipeline_list_all_impl;

struct class_ptr_result git_pipeline_git_pipeline_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return PICOMESH_OK(class_ptr, cls);
    ydebug("registering class=git_pipeline_git_pipeline");

    static const struct class_descriptor desc = {
        .name = "git_pipeline_git_pipeline",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct git_pipeline_git_pipeline_data),
    };
    static const struct op ops[] = {
        {"git_pipeline", "git_pipeline_enqueue", (method_id_t)git_pipeline_git_pipeline_enqueue, (impl_t)git_pipeline_git_pipeline_enqueue_impl},
        {"git_pipeline", "git_pipeline_enqueue_job", (method_id_t)git_pipeline_git_pipeline_enqueue_job, (impl_t)git_pipeline_git_pipeline_enqueue_job_impl},
        {"git_pipeline", "git_pipeline_lease", (method_id_t)git_pipeline_git_pipeline_lease, (impl_t)git_pipeline_git_pipeline_lease_impl},
        {"git_pipeline", "git_pipeline_lease_job", (method_id_t)git_pipeline_git_pipeline_lease_job, (impl_t)git_pipeline_git_pipeline_lease_job_impl},
        {"git_pipeline", "git_pipeline_job_descriptor", (method_id_t)git_pipeline_git_pipeline_job_descriptor, (impl_t)git_pipeline_git_pipeline_job_descriptor_impl},
        {"git_pipeline", "git_pipeline_append_log", (method_id_t)git_pipeline_git_pipeline_append_log, (impl_t)git_pipeline_git_pipeline_append_log_impl},
        {"git_pipeline", "git_pipeline_read_log", (method_id_t)git_pipeline_git_pipeline_read_log, (impl_t)git_pipeline_git_pipeline_read_log_impl},
        {"git_pipeline", "git_pipeline_complete", (method_id_t)git_pipeline_git_pipeline_complete, (impl_t)git_pipeline_git_pipeline_complete_impl},
        {"git_pipeline", "git_pipeline_complete_job", (method_id_t)git_pipeline_git_pipeline_complete_job, (impl_t)git_pipeline_git_pipeline_complete_job_impl},
        {"git_pipeline", "git_pipeline_requeue_expired", (method_id_t)git_pipeline_git_pipeline_requeue_expired, (impl_t)git_pipeline_git_pipeline_requeue_expired_impl},
        {"git_pipeline", "git_pipeline_count_pending", (method_id_t)git_pipeline_git_pipeline_count_pending, (impl_t)git_pipeline_git_pipeline_count_pending_impl},
        {"git_pipeline", "git_pipeline_count_running", (method_id_t)git_pipeline_git_pipeline_count_running, (impl_t)git_pipeline_git_pipeline_count_running_impl},
        {"git_pipeline", "git_pipeline_count_done", (method_id_t)git_pipeline_git_pipeline_count_done, (impl_t)git_pipeline_git_pipeline_count_done_impl},
        {"git_pipeline", "git_pipeline_list", (method_id_t)git_pipeline_git_pipeline_list, (impl_t)git_pipeline_git_pipeline_list_impl},
        {"git_pipeline", "git_pipeline_list_all", (method_id_t)git_pipeline_git_pipeline_list_all, (impl_t)git_pipeline_git_pipeline_list_all_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (PICOMESH_IS_ERR(_r))
        return PICOMESH_ERR(class_ptr, "git_pipeline_git_pipeline_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
