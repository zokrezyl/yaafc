/* GENERATED — do not edit. */
/* Public interface for plugin `relational_storage` — GENERATED.
 * Edit the annotated sources under src/picomesh/plugins/relational_storage/. */
#ifndef PICOMESH_PLUGIN_RELATIONAL_STORAGE_H
#define PICOMESH_PLUGIN_RELATIONAL_STORAGE_H

#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/rpc.h>

struct picomesh_int_result;
struct picomesh_json_result;
struct yheaders;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result relational_storage_db_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result relational_storage_db_create(struct ctx *ctx);

/* ---- methods ---- */
struct picomesh_json_result relational_storage_db_exec(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * db_name, uint32_t shard_key, const char * sql, const char * args_json);
struct picomesh_json_result relational_storage_db_query(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * db_name, uint32_t shard_key, const char * sql, const char * args_json);
struct picomesh_int_result relational_storage_db_shard_count(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * db_name);

/* ---- activation ---- */
struct picomesh_void_result picomesh_plugin_relational_storage_register(void);

#endif
