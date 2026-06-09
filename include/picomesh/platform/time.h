/* platform/time.h — cross-platform time abstraction.
 *
 * Two clocks plus a sleep, no platform headers in callers. Backends
 * live under src/picomesh/platform/time/<platform>.c — CMake selects
 * one based on the target.
 *
 * Mirrors yetty's platform/time.h convention so a future yetty
 * dependency or shared toolchain doesn't have to translate names. */

#ifndef PICOMESH_PLATFORM_TIME_H
#define PICOMESH_PLATFORM_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Monotonic clock in seconds (steady, epoch unspecified — differences
 * only). Use this for measuring intervals. */
double picomesh_platform_time_monotonic_sec(void);

/* Wall-clock time in milliseconds since the Unix epoch. */
int64_t picomesh_platform_time_wall_ms(void);

/* Block the calling thread for at least `ms` milliseconds. Plugins
 * that want coroutine-friendly sleep should use loop_sleep_ms
 * instead — this is for non-async contexts only. */
void picomesh_platform_time_sleep_ms(unsigned ms);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_PLATFORM_TIME_H */
