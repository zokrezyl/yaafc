/* GENERATED — do not edit. */
#include "git_repo.internal.h"

__attribute__((unused))
static git_repo_store_make_fn _git_repo_store_git_repo_store_make_check = git_repo_store_make_impl;
__attribute__((unused))
static git_repo_store_delete_fn _git_repo_store_git_repo_store_delete_check = git_repo_store_delete_impl;
__attribute__((unused))
static git_repo_store_owner_of_fn _git_repo_store_git_repo_store_owner_of_check = git_repo_store_owner_of_impl;
__attribute__((unused))
static git_repo_store_count_for_owner_fn _git_repo_store_git_repo_store_count_for_owner_check = git_repo_store_count_for_owner_impl;
__attribute__((unused))
static git_repo_store_count_total_fn _git_repo_store_git_repo_store_count_total_check = git_repo_store_count_total_impl;

struct class_ptr_result git_repo_store_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YAAFC_OK(class_ptr, cls);
    ydebug("registering class=git_repo_store");

    static const struct class_descriptor desc = {
        .name = "git_repo_store",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct git_repo_store_data),
    };
    static const struct op ops[] = {
        {"git_repo", "store_make", (method_id_t)git_repo_store_make, (impl_t)git_repo_store_make_impl},
        {"git_repo", "store_delete", (method_id_t)git_repo_store_delete, (impl_t)git_repo_store_delete_impl},
        {"git_repo", "store_owner_of", (method_id_t)git_repo_store_owner_of, (impl_t)git_repo_store_owner_of_impl},
        {"git_repo", "store_count_for_owner", (method_id_t)git_repo_store_count_for_owner, (impl_t)git_repo_store_count_for_owner_impl},
        {"git_repo", "store_count_total", (method_id_t)git_repo_store_count_total, (impl_t)git_repo_store_count_total_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YAAFC_IS_ERR(_r))
        return YAAFC_ERR(class_ptr, "git_repo_store_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
