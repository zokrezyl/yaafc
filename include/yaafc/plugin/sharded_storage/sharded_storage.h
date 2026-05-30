/* GENERATED — do not edit. */
/* Public interface for plugin `sharded_storage` — GENERATED.
 * Edit the annotated sources under src/yaafc/plugins/sharded_storage/. */
#ifndef YAAFC_PLUGIN_SHARDED_STORAGE_H
#define YAAFC_PLUGIN_SHARDED_STORAGE_H

#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>

struct yaafc_int_result;
struct yaafc_size_result;
struct yaafc_string_result;
struct yheaders;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result sharded_storage_db_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result sharded_storage_db_create(struct ctx *ctx);

/* ---- methods ---- */
struct yaafc_int_result sharded_storage_db_set(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key, const char * value);
struct yaafc_string_result sharded_storage_db_get(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key);
struct yaafc_int_result sharded_storage_db_exists(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key);
struct yaafc_int_result sharded_storage_db_del(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key);
struct yaafc_size_result sharded_storage_db_count(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context);

#endif
