/* time plugin — current time + coroutine-yielding sleep.
 *
 *   clock_now_ms     wall-clock milliseconds since the Unix epoch.
 *   clock_sleep_ms   yields the caller's coroutine for N ms via
 *                    uv_timer_t; the libuv loop keeps servicing
 *                    other connections meanwhile.
 *
 * `now_ms` goes through <picomesh/platform/time.h> so the same source
 * compiles unchanged on Linux/macOS/Windows — the platform backend
 * decides between clock_gettime and GetSystemTimeAsFileTime. The
 * sleep helper still needs the engine's loop, so it's reached via
 * the process-global accessor that the driver installs at startup. */

#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/picoclass/class.h>
#include <picomesh/loop/loop.h>
#include <picomesh/engine/engine.h>
#include <picomesh/platform/time.h>

#include <stdint.h>

struct PICOMESH_CLASS_ANNOTATE("class@time:clock") time_clock_data {
    char _placeholder;
};

PICOMESH_CLASS_ANNOTATE("override@time:clock:clock_now_ms")
struct picomesh_int64_result time_clock_now_ms_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs)
{
    (void)ctx; (void)obj;
    int64_t ms = picomesh_platform_time_wall_ms();
    ydebug("clock_now_ms -> %lld", (long long)ms);
    return PICOMESH_OK(picomesh_int64, ms);
}

PICOMESH_CLASS_ANNOTATE("override@time:clock:clock_sleep_ms")
struct picomesh_int64_result time_clock_sleep_ms_impl(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                                                   uint32_t ms)
{
    (void)ctx; (void)obj;
    struct picomesh_engine *engine = picomesh_active_engine();
    if (!engine) return PICOMESH_ERR(picomesh_int64, "clock_sleep_ms: no active engine");
    struct loop *loop = picomesh_engine_loop(engine);
    if (!loop) return PICOMESH_ERR(picomesh_int64, "clock_sleep_ms: engine has no loop");
    ydebug("clock_sleep_ms: sleeping %u ms", ms);
    loop_sleep_ms(loop, ms);
    return PICOMESH_OK(picomesh_int64, (int64_t)ms);
}

#include "clock.gen.c"
