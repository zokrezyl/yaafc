/* GENERATED — do not edit. */
#include "time.internal.h"

__attribute__((unused))
static time_clock_now_ms_fn _time_clock_time_clock_now_ms_check = time_clock_now_ms_impl;
__attribute__((unused))
static time_clock_sleep_ms_fn _time_clock_time_clock_sleep_ms_check = time_clock_sleep_ms_impl;

struct class_ptr_result time_clock_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YAAFC_OK(class_ptr, cls);
    ydebug("registering class=time_clock");

    static const struct class_descriptor desc = {
        .name = "time_clock",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct time_clock_data),
    };
    static const struct op ops[] = {
        {"time", "clock_now_ms", (method_id_t)time_clock_now_ms, (impl_t)time_clock_now_ms_impl},
        {"time", "clock_sleep_ms", (method_id_t)time_clock_sleep_ms, (impl_t)time_clock_sleep_ms_impl},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YAAFC_IS_ERR(_r))
        return YAAFC_ERR(class_ptr, "time_clock_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
