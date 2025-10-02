// Syphax-Web - Ougi Washi

#ifndef SW_TIME_H
#define SW_TIME_H

#include "sw_types.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h> /* for time_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Provide CLOCK_REALTIME and CLOCK_MONOTONIC if not defined */
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

/* Provide timespec if missing */
//#ifndef HAVE_STRUCT_TIMESPEC
//struct timespec {
//    time_t tv_sec;   /* seconds */
//    long   tv_nsec;  /* nanoseconds */
//};
//#endif

static int clock_gettime(int clk_id, struct timespec *ts)
{
    if (!ts) {
        errno = EINVAL;
        return -1;
    }
    if (clk_id == CLOCK_REALTIME) {
        /* Use GetSystemTimeAsFileTime to get wall-clock time with 100-ns resolution */
        FILETIME ft;
        unsigned long long ull;
        GetSystemTimeAsFileTime(&ft);
        ull = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        /* FILETIME is in 100-ns intervals since Jan 1, 1601 (UTC).
           Convert to Unix epoch (seconds since Jan 1, 1970). */
        const unsigned long long EPOCH_DIFF_100NS = 116444736000000000ULL;
        if (ull < EPOCH_DIFF_100NS) {
            ts->tv_sec = 0;
            ts->tv_nsec = 0;
            return 0;
        }
        ull -= EPOCH_DIFF_100NS;
        ts->tv_sec = (time_t)(ull / 10000000ULL);
        ts->tv_nsec = (long)((ull % 10000000ULL) * 100); /* 100ns -> ns */
        return 0;
    }
    else if (clk_id == CLOCK_MONOTONIC) {
        /* Use QueryPerformanceCounter for high-resolution monotonic time */
        static LARGE_INTEGER freq = {0};
        static int freq_init = 0;
        LARGE_INTEGER counter;
        if (!freq_init) {
            if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) {
                errno = EINVAL;
                return -1;
            }
            freq_init = 1;
        }
        if (!QueryPerformanceCounter(&counter)) {
            errno = EINVAL;
            return -1;
        }
        /* seconds = counter / freq */
        LONGLONG sec = counter.QuadPart / freq.QuadPart;
        LONGLONG rem = counter.QuadPart % freq.QuadPart;
        ts->tv_sec = (time_t)sec;
        /* rem * 1e9 / freq -> nanoseconds */
        ts->tv_nsec = (long)((rem * 1000000000LL) / freq.QuadPart);
        return 0;
    }
    errno = EINVAL;
    return -1;
}

struct utimbuf {
    time_t actime;
    time_t modtime;
};

static int utime(const char *path, const struct utimbuf *times)
{
    HANDLE hFile;
    FILETIME atime, mtime;
    ULARGE_INTEGER ull;
    const ULARGE_INTEGER EPOCH_DIFF_100NS = {116444736000000000ULL};
    
    if (!path) {
        errno = EINVAL;
        return -1;
    }

    hFile = CreateFileA(path, 
                        FILE_WRITE_ATTRIBUTES, 
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                        NULL, 
                        OPEN_EXISTING, 
                        FILE_ATTRIBUTE_NORMAL, 
                        NULL);
    
    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD dwError = GetLastError();
        switch (dwError) {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                errno = ENOENT;
                break;
            case ERROR_ACCESS_DENIED:
                errno = EACCES;
                break;
            default:
                errno = EINVAL;
                break;
        }
        return -1;
    }

    if (times) {
        /* Convert access time */
        ull.QuadPart = ((ULONGLONG)times->actime * 10000000ULL) + EPOCH_DIFF_100NS.QuadPart;
        atime.dwLowDateTime = ull.LowPart;
        atime.dwHighDateTime = ull.HighPart;
        
        /* Convert modification time */
        ull.QuadPart = ((ULONGLONG)times->modtime * 10000000ULL) + EPOCH_DIFF_100NS.QuadPart;
        mtime.dwLowDateTime = ull.LowPart;
        mtime.dwHighDateTime = ull.HighPart;
    } else {
        /* Use current time */
        SYSTEMTIME st;
        GetSystemTime(&st);
        SystemTimeToFileTime(&st, &atime);
        mtime = atime;
    }

    if (!SetFileTime(hFile, NULL, &atime, &mtime)) {
        CloseHandle(hFile);
        errno = EACCES;
        return -1;
    }

    CloseHandle(hFile);
    return 0;
}

#ifdef __cplusplus
}
#endif

#else /* not _WIN32 (POSIX) */

#if defined(__unix__) || defined(__APPLE__) || defined(__MACH__)
/* On POSIX systems include the usual headers */
#include <time.h>
#include <utime.h>
#else
#include <time.h>
#include <utime.h>
#endif

/* Leave native clock_gettime / timespec / utime in place. */

#endif /* _WIN32 */

#endif /* SW_TIME_H */

