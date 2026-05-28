/* GENERATED — do not edit. */
/* Public interface for plugin `accounts` — GENERATED.
 * Edit the annotated sources under src/yaafc/plugins/accounts/. */
#ifndef YAAFC_PLUGIN_ACCOUNTS_H
#define YAAFC_PLUGIN_ACCOUNTS_H

#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>

struct yaafc_int64_result;
struct yaafc_int_result;
struct yaafc_size_result;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result accounts_store_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result accounts_store_create(struct ctx *ctx);

/* ---- methods ---- */
struct yaafc_int_result accounts_store_register(struct ctx * ctx, struct object * obj, uint32_t uid);
struct yaafc_int_result accounts_store_exists(struct ctx * ctx, struct object * obj, uint32_t uid);
struct yaafc_int_result accounts_store_set_balance(struct ctx * ctx, struct object * obj, uint32_t uid, int64_t n);
struct yaafc_int64_result accounts_store_balance(struct ctx * ctx, struct object * obj, uint32_t uid);
struct yaafc_size_result accounts_store_count(struct ctx * ctx, struct object * obj);

#endif
