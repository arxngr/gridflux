#ifndef PLATFORM_COMPAT_H
#define PLATFORM_COMPAT_H

#ifdef _WIN32
    #include <windows.h>
    #include <sys/types.h>
    #include <sys/stat.h>

    // POSIX usleep -> Windows Sleep
    #define usleep(x) Sleep((x) / 1000)

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
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/stat.h>
#endif

#endif // PLATFORM_COMPAT_H
