/* calculator — yaapp.plugins.calculator port.
 *
 * One class with add/sub/mul/div methods. divide returns an error
 * result on division-by-zero — exact mirror of the yaapp original.
 *
 * The plugin has no instance state, but a `struct` placeholder is
 * still required so the codegen has something to mark with
 * `class@calculator:calc`. Zero-sized structs aren't portable C;
 * the placeholder byte stays. */

#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/picoclass/class.h>

#include <stdint.h>

struct PICOMESH_CLASS_ANNOTATE("class@calculator:calc") calculator_calc_data {
    char _placeholder;
};

PICOMESH_CLASS_ANNOTATE("override@calculator:calc:calc_add")
struct picomesh_int64_result calculator_calc_add_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                   int64_t x, int64_t y)
{
    (void)ctx; (void)obj;
    ydebug("calc_add: %lld + %lld", (long long)x, (long long)y);
    return PICOMESH_OK(picomesh_int64, x + y);
}

PICOMESH_CLASS_ANNOTATE("override@calculator:calc:calc_sub")
struct picomesh_int64_result calculator_calc_sub_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                   int64_t x, int64_t y)
{
    (void)ctx; (void)obj;
    ydebug("calc_sub: %lld - %lld", (long long)x, (long long)y);
    return PICOMESH_OK(picomesh_int64, x - y);
}

PICOMESH_CLASS_ANNOTATE("override@calculator:calc:calc_mul")
struct picomesh_int64_result calculator_calc_mul_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                   int64_t x, int64_t y)
{
    (void)ctx; (void)obj;
    ydebug("calc_mul: %lld * %lld", (long long)x, (long long)y);
    return PICOMESH_OK(picomesh_int64, x * y);
}

PICOMESH_CLASS_ANNOTATE("override@calculator:calc:calc_div")
struct picomesh_int64_result calculator_calc_div_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                   int64_t x, int64_t y)
{
    (void)ctx; (void)obj;
    if (y == 0) {
        ywarn("calc_div: division by zero");
        return PICOMESH_ERR(picomesh_int64, "calc_div: division by zero");
    }
    ydebug("calc_div: %lld / %lld", (long long)x, (long long)y);
    return PICOMESH_OK(picomesh_int64, x / y);
}

#include "calc.gen.c"
