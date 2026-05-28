/* GENERATED — do not edit. */
#include "issues.internal.h"

__attribute__((unused))
static issues_store_open_fn _issues_store_issues_store_open_check = issues_store_open_impl;
__attribute__((unused))
static issues_store_close_fn _issues_store_issues_store_close_check = issues_store_close_impl;
__attribute__((unused))
static issues_store_status_fn _issues_store_issues_store_status_check = issues_store_status_impl;
__attribute__((unused))
static issues_store_count_open_in_repo_fn _issues_store_issues_store_count_open_in_repo_check = issues_store_count_open_in_repo_impl;
__attribute__((unused))
static issues_store_count_total_fn _issues_store_issues_store_count_total_check = issues_store_count_total_impl;

struct class_ptr_result issues_store_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YAAFC_OK(class_ptr, cls);
    ydebug("registering class=issues_store");

    static const struct class_descriptor desc = {
        .name = "issues_store",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct issues_store_data),
    };
    static const struct op ops[] = {
        {"issues", "store_open", (method_id_t)issues_store_open, (impl_t)issues_store_open_impl},
        {"issues", "store_close", (method_id_t)issues_store_close, (impl_t)issues_store_close_impl},
        {"issues", "store_status", (method_id_t)issues_store_status, (impl_t)issues_store_status_impl},
        {"issues", "store_count_open_in_repo", (method_id_t)issues_store_count_open_in_repo, (impl_t)issues_store_count_open_in_repo_impl},
        {"issues", "store_count_total", (method_id_t)issues_store_count_total, (impl_t)issues_store_count_total_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YAAFC_IS_ERR(_r))
        return YAAFC_ERR(class_ptr, "issues_store_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
