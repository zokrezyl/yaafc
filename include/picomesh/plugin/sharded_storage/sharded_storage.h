/* GENERATED — do not edit. */
/* Public interface for plugin `sharded_storage` — GENERATED.
 * Edit the annotated sources under src/picomesh/plugins/sharded_storage/. */
#ifndef PICOMESH_PLUGIN_SHARDED_STORAGE_H
#define PICOMESH_PLUGIN_SHARDED_STORAGE_H

#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/rpc.h>

struct picomesh_int64_result;
struct picomesh_int_result;
struct picomesh_json_result;
struct picomesh_size_result;
struct picomesh_string_result;
struct yheaders;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result sharded_storage_db_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result sharded_storage_db_create(struct ctx *ctx);

/* ---- methods ---- */
struct picomesh_int_result
sharded_storage_db_set(struct ctx *ctx, struct object *obj,
                       struct yheaders *hdrs, const char *context,
                       const char *key, const char *value);
struct picomesh_string_result sharded_storage_db_get(struct ctx *ctx,
                                                     struct object *obj,
                                                     struct yheaders *hdrs,
                                                     const char *context,
                                                     const char *key);
struct picomesh_int_result sharded_storage_db_exists(struct ctx *ctx,
                                                     struct object *obj,
                                                     struct yheaders *hdrs,
                                                     const char *context,
                                                     const char *key);
struct picomesh_int_result sharded_storage_db_del(struct ctx *ctx,
                                                  struct object *obj,
                                                  struct yheaders *hdrs,
                                                  const char *context,
                                                  const char *key);
struct picomesh_size_result sharded_storage_db_count(struct ctx *ctx,
                                                     struct object *obj,
                                                     struct yheaders *hdrs,
                                                     const char *context);
struct picomesh_json_result
sharded_storage_db_list(struct ctx *ctx, struct object *obj,
                        struct yheaders *hdrs, const char *context,
                        const char *prefix, int64_t offset, int64_t limit);
struct picomesh_json_result sharded_storage_db_list_all(struct ctx *ctx,
                                                        struct object *obj,
                                                        struct yheaders *hdrs,
                                                        const char *context,
                                                        const char *prefix);
struct picomesh_int64_result
sharded_storage_db_incr(struct ctx *ctx, struct object *obj,
                        struct yheaders *hdrs, const char *context,
                        const char *key, int64_t delta);
struct picomesh_int_result
sharded_storage_db_put_if_absent(struct ctx *ctx, struct object *obj,
                                 struct yheaders *hdrs, const char *context,
                                 const char *key, const char *value);
struct picomesh_int_result
sharded_storage_db_compare_and_set(struct ctx *ctx, struct object *obj,
                                   struct yheaders *hdrs, const char *context,
                                   const char *key, const char *expected,
                                   const char *replacement);

/* ---- activation ---- */
struct picomesh_void_result picomesh_plugin_sharded_storage_register(void);

#endif
