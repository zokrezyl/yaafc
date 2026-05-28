/* GENERATED — do not edit. */
#include "calculator.internal.h"

__attribute__((unused))
static calculator_calc_add_fn _calculator_calc_calculator_calc_add_check = calculator_calc_add_impl;
__attribute__((unused))
static calculator_calc_sub_fn _calculator_calc_calculator_calc_sub_check = calculator_calc_sub_impl;
__attribute__((unused))
static calculator_calc_mul_fn _calculator_calc_calculator_calc_mul_check = calculator_calc_mul_impl;
__attribute__((unused))
static calculator_calc_div_fn _calculator_calc_calculator_calc_div_check = calculator_calc_div_impl;

struct class_ptr_result calculator_calc_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YAAFC_OK(class_ptr, cls);
    ydebug("registering class=calculator_calc");

    static const struct class_descriptor desc = {
        .name = "calculator_calc",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct calculator_calc_data),
    };
    static const struct op ops[] = {
        {"calculator", "calc_add", (method_id_t)calculator_calc_add, (impl_t)calculator_calc_add_impl},
        {"calculator", "calc_sub", (method_id_t)calculator_calc_sub, (impl_t)calculator_calc_sub_impl},
        {"calculator", "calc_mul", (method_id_t)calculator_calc_mul, (impl_t)calculator_calc_mul_impl},
        {"calculator", "calc_div", (method_id_t)calculator_calc_div, (impl_t)calculator_calc_div_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YAAFC_IS_ERR(_r))
        return YAAFC_ERR(class_ptr, "calculator_calc_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
