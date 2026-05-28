/* GENERATED — do not edit. */
/* Public interface for plugin `time` — GENERATED.
 * Edit the annotated sources under src/yaafc/plugins/time/. */
#ifndef YAAFC_PLUGIN_TIME_H
#define YAAFC_PLUGIN_TIME_H

#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>

struct yaafc_int64_result;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result time_clock_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result time_clock_create(struct ctx *ctx);

/* ---- methods ---- */
struct yaafc_int64_result time_clock_now_ms(struct ctx * ctx, struct object * obj);
struct yaafc_int64_result time_clock_sleep_ms(struct ctx * ctx, struct object * obj, uint32_t ms);

#endif
