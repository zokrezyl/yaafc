/* calculator — yaapp.plugins.calculator port.
 *
 * One class with add/sub/mul/div methods. divide returns an error
 * result on division-by-zero — exact mirror of the yaapp original.
 *
 * The plugin has no instance state, but a `struct` placeholder is
 * still required so the codegen has something to mark with
 * `class@calculator:calc`. Zero-sized structs aren't portable C;
 * the placeholder byte stays. */

#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/class.h>

#include <stdint.h>

struct YAAFC_CLASS_ANNOTATE("class@calculator:calc") calculator_calc_data {
    char _placeholder;
};

YAAFC_CLASS_ANNOTATE("override@calculator:calc:calc_add")
struct yaafc_int64_result calculator_calc_add_impl(struct ctx *ctx, struct object *obj,
                                                   int64_t x, int64_t y)
{
    (void)ctx; (void)obj;
    ydebug("calc_add: %lld + %lld", (long long)x, (long long)y);
    return YAAFC_OK(yaafc_int64, x + y);
}

YAAFC_CLASS_ANNOTATE("override@calculator:calc:calc_sub")
struct yaafc_int64_result calculator_calc_sub_impl(struct ctx *ctx, struct object *obj,
                                                   int64_t x, int64_t y)
{
    (void)ctx; (void)obj;
    ydebug("calc_sub: %lld - %lld", (long long)x, (long long)y);
    return YAAFC_OK(yaafc_int64, x - y);
}

YAAFC_CLASS_ANNOTATE("override@calculator:calc:calc_mul")
struct yaafc_int64_result calculator_calc_mul_impl(struct ctx *ctx, struct object *obj,
                                                   int64_t x, int64_t y)
{
    (void)ctx; (void)obj;
    ydebug("calc_mul: %lld * %lld", (long long)x, (long long)y);
    return YAAFC_OK(yaafc_int64, x * y);
}

YAAFC_CLASS_ANNOTATE("override@calculator:calc:calc_div")
struct yaafc_int64_result calculator_calc_div_impl(struct ctx *ctx, struct object *obj,
                                                   int64_t x, int64_t y)
{
    (void)ctx; (void)obj;
    if (y == 0) {
        ywarn("calc_div: division by zero");
        return YAAFC_ERR(yaafc_int64, "calc_div: division by zero");
    }
    ydebug("calc_div: %lld / %lld", (long long)x, (long long)y);
    return YAAFC_OK(yaafc_int64, x / y);
}

#include "calc.gen.c"
