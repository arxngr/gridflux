#ifndef PLATFORM_COMPAT_H
#define PLATFORM_COMPAT_H
#define GF_SWIPE_THRESHOLD_PX 200.0

#ifdef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <windows.h>

// POSIX usleep -> Windows Sleep
#define gf_usleep(x) Sleep ((x) / 1000)

// Map POSIX stat to Windows _stat
#define stat _stat
#define lstat _stat

// ssize_t is not defined on Windows
#ifdef _WIN64
typedef __int64 ssize_t;
#else
typedef long ssize_t;
#endif

#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifndef _WIN32
#define gf_usleep(x) usleep (x)
#endif

#endif // PLATFORM_COMPAT_H
