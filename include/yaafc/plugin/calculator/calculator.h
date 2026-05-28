/* GENERATED — do not edit. */
/* Public interface for plugin `calculator` — GENERATED.
 * Edit the annotated sources under src/yaafc/plugins/calculator/. */
#ifndef YAAFC_PLUGIN_CALCULATOR_H
#define YAAFC_PLUGIN_CALCULATOR_H

#include <yaafc/yclass/class.h>
#include <yaafc/yclass/rpc.h>

struct yaafc_int64_result;
struct object_ptr_result;
struct class_ptr_result;

/* ---- class accessors ---- */
struct class_ptr_result calculator_calc_class_get(void);

/* ---- constructors ---- */
struct object_ptr_result calculator_calc_create(struct ctx *ctx);

/* ---- methods ---- */
struct yaafc_int64_result calculator_calc_add(struct ctx * ctx, struct object * obj, int64_t x, int64_t y);
struct yaafc_int64_result calculator_calc_sub(struct ctx * ctx, struct object * obj, int64_t x, int64_t y);
struct yaafc_int64_result calculator_calc_mul(struct ctx * ctx, struct object * obj, int64_t x, int64_t y);
struct yaafc_int64_result calculator_calc_div(struct ctx * ctx, struct object * obj, int64_t x, int64_t y);

#endif
