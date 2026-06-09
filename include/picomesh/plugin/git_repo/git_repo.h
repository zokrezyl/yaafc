/* GENERATED — do not edit. */
/* Public interface for plugin `git_repo` — GENERATED.
 * Edit the annotated sources under src/picomesh/plugins/git_repo/. */
#ifndef PICOMESH_PLUGIN_GIT_REPO_H
#define PICOMESH_PLUGIN_GIT_REPO_H

#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/rpc.h>

struct picomesh_int_result;
struct picomesh_json_result;
struct picomesh_size_result;
struct picomesh_string_result;
struct picomesh_uint32_result;
struct yheaders;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result git_repo_git_repo_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result git_repo_git_repo_create(struct ctx *ctx);

/* ---- methods ---- */
struct picomesh_uint32_result
git_repo_git_repo_make(struct ctx *ctx, struct object *obj,
                       struct yheaders *hdrs, uint32_t owner_id,
                       const char *owner_name, const char *repo_name);
struct picomesh_int_result git_repo_git_repo_delete(struct ctx *ctx,
                                                    struct object *obj,
                                                    struct yheaders *hdrs,
                                                    uint32_t repo_id);
struct picomesh_uint32_result git_repo_git_repo_owner_of(struct ctx *ctx,
                                                         struct object *obj,
                                                         struct yheaders *hdrs,
                                                         uint32_t repo_id);
struct picomesh_string_result
git_repo_git_repo_namespace_of(struct ctx *ctx, struct object *obj,
                               struct yheaders *hdrs, uint32_t repo_id);
struct picomesh_size_result
git_repo_git_repo_count_for_owner(struct ctx *ctx, struct object *obj,
                                  struct yheaders *hdrs, uint32_t owner_id);
struct picomesh_size_result
git_repo_git_repo_count_total(struct ctx *ctx, struct object *obj,
                              struct yheaders *hdrs);
struct picomesh_string_result
git_repo_git_repo_list_for_owner(struct ctx *ctx, struct object *obj,
                                 struct yheaders *hdrs, uint32_t owner_id);
struct picomesh_string_result
git_repo_git_repo_list_for_namespace(struct ctx *ctx, struct object *obj,
                                     struct yheaders *hdrs, const char *path);
struct picomesh_size_result
git_repo_git_repo_count_for_namespace(struct ctx *ctx, struct object *obj,
                                      struct yheaders *hdrs, const char *path);
struct picomesh_string_result
git_repo_git_repo_read_tree(struct ctx *ctx, struct object *obj,
                            struct yheaders *hdrs, uint32_t repo_id,
                            const char *ref, const char *path);
struct picomesh_string_result
git_repo_git_repo_read_file(struct ctx *ctx, struct object *obj,
                            struct yheaders *hdrs, uint32_t repo_id,
                            const char *ref, const char *path);
struct picomesh_string_result git_repo_git_repo_put_file(
    struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
    uint32_t repo_id, const char *path, const char *content,
    const char *message, const char *author_name, const char *author_email);
struct picomesh_int_result git_repo_git_repo_is_public(struct ctx *ctx,
                                                       struct object *obj,
                                                       struct yheaders *hdrs,
                                                       uint32_t repo_id);
struct picomesh_int_result git_repo_git_repo_set_public(struct ctx *ctx,
                                                        struct object *obj,
                                                        struct yheaders *hdrs,
                                                        uint32_t repo_id,
                                                        int is_public);
struct picomesh_json_result
git_repo_git_repo_list(struct ctx *ctx, struct object *obj,
                       struct yheaders *hdrs, int64_t offset, int64_t limit);
struct picomesh_json_result git_repo_git_repo_list_all(struct ctx *ctx,
                                                       struct object *obj,
                                                       struct yheaders *hdrs);

/* ---- activation ---- */
struct picomesh_void_result picomesh_plugin_git_repo_register(void);

#endif
