/* GENERATED — do not edit. */
/* Public interface for plugin `storage` — GENERATED.
 * Edit the annotated sources under src/yaafc/plugins/storage/. */
#ifndef YAAFC_PLUGIN_STORAGE_H
#define YAAFC_PLUGIN_STORAGE_H

#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>

struct yaafc_int_result;
struct yaafc_size_result;
struct yaafc_string_result;
struct yheaders;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result storage_kv_class_get(void);
struct class_ptr_result storage_db_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result storage_kv_create(struct ctx *ctx);
struct object_ptr_result storage_db_create(struct ctx *ctx);

/* ---- methods ---- */
struct yaafc_int_result storage_kv_set(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * key, const char * value);
struct yaafc_string_result storage_kv_get(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * key);
struct yaafc_size_result storage_kv_count(struct ctx * ctx, struct object * obj, struct yheaders * hdrs);
struct yaafc_int_result storage_set(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key, const char * value);
struct yaafc_string_result storage_get(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key);
struct yaafc_int_result storage_exists(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key);
struct yaafc_int_result storage_del(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key);
struct yaafc_size_result storage_count(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context);

#endif
