/* GENERATED — do not edit. */
/* Public interface for plugin `time` — GENERATED.
 * Edit the annotated sources under src/picomesh/plugins/time/. */
#ifndef PICOMESH_PLUGIN_TIME_H
#define PICOMESH_PLUGIN_TIME_H

#include <picomesh/picoclass/class.h>
#include <picomesh/picoclass/rpc.h>

struct picomesh_int64_result;
struct yheaders;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result time_clock_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result time_clock_create(struct ctx *ctx);

/* ---- methods ---- */
struct picomesh_int64_result time_clock_now_ms(struct ctx * ctx, struct object * obj, struct yheaders * hdrs);
struct picomesh_int64_result time_clock_sleep_ms(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t ms);

/* ---- activation ---- */
struct picomesh_void_result picomesh_plugin_time_register(void);

#endif
