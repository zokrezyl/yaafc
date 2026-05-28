/* GENERATED — do not edit. */
/* Public interface for plugin `storage` — GENERATED.
 * Edit the annotated sources under src/yaafc/plugins/storage/. */
#ifndef YAAFC_PLUGIN_STORAGE_H
#define YAAFC_PLUGIN_STORAGE_H

#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>

struct yaafc_int64_result;
struct yaafc_int_result;
struct yaafc_size_result;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result storage_kv_class_get(void);
struct class_ptr_result storage_sql_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result storage_kv_create(struct ctx *ctx);
struct object_ptr_result storage_sql_create(struct ctx *ctx);

/* ---- methods ---- */
struct yaafc_int_result storage_kv_set(struct ctx * ctx, struct object * obj, uint32_t key_id, int32_t value);
struct yaafc_int_result storage_kv_get(struct ctx * ctx, struct object * obj, uint32_t key_id);
struct yaafc_size_result storage_kv_count(struct ctx * ctx, struct object * obj);
struct yaafc_int_result storage_sql_set(struct ctx * ctx, struct object * obj, const char * key, int64_t value);
struct yaafc_int64_result storage_sql_get(struct ctx * ctx, struct object * obj, const char * key);
struct yaafc_int_result storage_sql_exists(struct ctx * ctx, struct object * obj, const char * key);
struct yaafc_int_result storage_sql_del(struct ctx * ctx, struct object * obj, const char * key);
struct yaafc_size_result storage_sql_count(struct ctx * ctx, struct object * obj);

#endif
