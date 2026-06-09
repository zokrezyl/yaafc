/* time/posix.c — POSIX backend, used on Linux, macOS, and BSDs. */

#define _POSIX_C_SOURCE 200809L

#include <picomesh/platform/time.h>

#include <time.h>
#include <unistd.h>

double picomesh_platform_time_monotonic_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int64_t picomesh_platform_time_wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void picomesh_platform_time_sleep_ms(unsigned ms)
{
    struct timespec req = {
        .tv_sec = (time_t)(ms / 1000U),
        .tv_nsec = (long)((ms % 1000U) * 1000000UL),
    };
    /* nanosleep is more portable than usleep (which glibc deprecated)
     * and survives EINTR via the second-arg remaining-time pattern.
     * Single shot is fine here — callers asking for "ms ms" don't
     * usually care about a few microseconds of slippage on signals. */
    nanosleep(&req, NULL);
}
