/* time/windows.c — Win32 backend, selected by CMake on WIN32. */

#include <picomesh/platform/time.h>

#include <windows.h>

double picomesh_platform_time_monotonic_sec(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
}

int64_t picomesh_platform_time_wall_ms(void)
{
    /* GetSystemTimeAsFileTime returns 100-nanosecond intervals since
     * 1601-01-01 UTC. Convert to Unix epoch (1970-01-01) ms. */
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    /* 11644473600 = seconds between 1601-01-01 and 1970-01-01. */
    return (int64_t)((u.QuadPart - 116444736000000000ULL) / 10000ULL);
}

void picomesh_platform_time_sleep_ms(unsigned ms)
{
    Sleep((DWORD)ms);
}
