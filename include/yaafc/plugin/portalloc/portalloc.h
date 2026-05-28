/* GENERATED — do not edit. */
/* Public interface for plugin `portalloc` — GENERATED.
 * Edit the annotated sources under src/yaafc/plugins/portalloc/. */
#ifndef YAAFC_PLUGIN_PORTALLOC_H
#define YAAFC_PLUGIN_PORTALLOC_H

#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>

struct yaafc_int_result;
struct yaafc_size_result;
struct yaafc_uint32_result;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result portalloc_store_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result portalloc_store_create(struct ctx *ctx);

/* ---- methods ---- */
struct yaafc_uint32_result portalloc_store_allocate(struct ctx * ctx, struct object * obj, uint32_t service_id);
struct yaafc_int_result portalloc_store_release(struct ctx * ctx, struct object * obj, uint32_t port);
struct yaafc_size_result portalloc_store_count_used(struct ctx * ctx, struct object * obj);

#endif
