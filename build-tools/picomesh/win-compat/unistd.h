/* Windows compat shim for <unistd.h> (POSIX) + the few <time.h> POSIX clock
 * bits picomesh uses directly. Only on the WIN32 include path.
 *
 * The handful of Win32 calls used here (Sleep, QueryPerformance*) are
 * forward-declared rather than pulled from <windows.h>, so this header never
 * forces <windows.h> ahead of a translation unit that needs <winsock2.h>
 * first. */

#ifndef PICOMESH_WIN_COMPAT_UNISTD_H
#define PICOMESH_WIN_COMPAT_UNISTD_H

#include <BaseTsd.h>  /* SSIZE_T */
#include <direct.h>   /* _getcwd, _chdir, _rmdir, _mkdir */
#include <io.h>       /* _access, _unlink, _chsize, _commit, _isatty, _dup */
#include <process.h>  /* _getpid */
#include <time.h>     /* struct timespec (C11) */

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#endif

/* access(2) mode bits — MSVC's _access has no execute mode, so X_OK folds to
 * an existence check. */
#ifndef F_OK
#define F_OK 0
#define X_OK 0
#define W_OK 2
#define R_OK 4
#endif

#define access(path, mode) _access((path), (mode))
#define unlink(path)       _unlink((path))
#define getpid()           _getpid()
#define ftruncate(fd, len) _chsize((fd), (len))
#define fsync(fd)          _commit((fd))
#define isatty(fd)         _isatty((fd))

__declspec(dllimport) void __stdcall Sleep(unsigned long milliseconds);
static __inline int usleep(unsigned int usec) {
  Sleep(usec / 1000u);
  return 0;
}

/* ---- POSIX monotonic/realtime clock ---- */
typedef int clockid_t;
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#endif

__declspec(dllimport) int __stdcall QueryPerformanceCounter(long long *count);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(long long *freq);
__declspec(dllimport) void __stdcall GetSystemTimePreciseAsFileTime(
    unsigned long long *filetime);

static __inline int clock_gettime(clockid_t clock_id, struct timespec *ts) {
  if (clock_id == CLOCK_MONOTONIC) {
    long long freq = 0, count = 0;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    if (freq == 0)
      return -1;
    ts->tv_sec = (time_t)(count / freq);
    ts->tv_nsec = (long)(((count % freq) * 1000000000LL) / freq);
  } else {
    /* FILETIME: 100 ns ticks since 1601-01-01; offset to the Unix epoch. */
    unsigned long long filetime = 0;
    GetSystemTimePreciseAsFileTime(&filetime);
    unsigned long long unix100ns = filetime - 116444736000000000ULL;
    ts->tv_sec = (time_t)(unix100ns / 10000000ULL);
    ts->tv_nsec = (long)((unix100ns % 10000000ULL) * 100ULL);
  }
  return 0;
}

#endif /* PICOMESH_WIN_COMPAT_UNISTD_H */
