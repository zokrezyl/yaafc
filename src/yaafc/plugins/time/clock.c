/* time plugin — current time + coroutine-yielding sleep.
 *
 *   clock_now_ms     wall-clock milliseconds since the Unix epoch.
 *   clock_sleep_ms   yields the caller's coroutine for N ms via
 *                    uv_timer_t; the libuv loop keeps servicing
 *                    other connections meanwhile.
 *
 * `now_ms` goes through <yaafc/yplatform/time.h> so the same source
 * compiles unchanged on Linux/macOS/Windows — the platform backend
 * decides between clock_gettime and GetSystemTimeAsFileTime. The
 * sleep helper still needs the engine's loop, so it's reached via
 * the process-global accessor that the driver installs at startup. */

#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/class.h>
#include <yaafc/yloop/yloop.h>
#include <yaafc/yengine/engine.h>
#include <yaafc/yplatform/time.h>

#include <stdint.h>

struct YAAFC_CLASS_ANNOTATE("class@time:clock") time_clock_data {
    char _placeholder;
};

YAAFC_CLASS_ANNOTATE("override@time:clock:clock_now_ms")
struct yaafc_int64_result time_clock_now_ms_impl(struct ctx *ctx, struct object *obj)
{
    (void)ctx; (void)obj;
    int64_t ms = yaafc_yplatform_time_wall_ms();
    ydebug("clock_now_ms -> %lld", (long long)ms);
    return YAAFC_OK(yaafc_int64, ms);
}

YAAFC_CLASS_ANNOTATE("override@time:clock:clock_sleep_ms")
struct yaafc_int64_result time_clock_sleep_ms_impl(struct ctx *ctx, struct object *obj,
                                                   uint32_t ms)
{
    (void)ctx; (void)obj;
    struct yaafc_engine *e = yaafc_active_engine();
    if (!e) return YAAFC_ERR(yaafc_int64, "clock_sleep_ms: no active engine");
    struct yloop *l = yaafc_engine_loop(e);
    if (!l) return YAAFC_ERR(yaafc_int64, "clock_sleep_ms: engine has no loop");
    ydebug("clock_sleep_ms: sleeping %u ms", ms);
    yloop_sleep_ms(l, ms);
    return YAAFC_OK(yaafc_int64, (int64_t)ms);
}

#include "clock.gen.c"
