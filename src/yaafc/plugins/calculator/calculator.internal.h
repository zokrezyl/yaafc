/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `calculator`.
 * NEVER include this from outside src/yaafc/plugins/calculator/. */
#ifndef YAAFC_CALCULATOR_INTERNAL_H
#define YAAFC_CALCULATOR_INTERNAL_H

#include <yaafc/plugin/calculator/calculator.h>

typedef struct yaafc_int64_result (*calculator_calc_add_fn)(struct ctx *, struct object *, int64_t, int64_t);
typedef struct yaafc_int64_result (*calculator_calc_sub_fn)(struct ctx *, struct object *, int64_t, int64_t);
typedef struct yaafc_int64_result (*calculator_calc_mul_fn)(struct ctx *, struct object *, int64_t, int64_t);
typedef struct yaafc_int64_result (*calculator_calc_div_fn)(struct ctx *, struct object *, int64_t, int64_t);

#endif
