#include "../../include/core/logger.h"
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static gf_log_level_t g_log_level = GF_LOG_INFO;

static const char *level_strings[] = { "ERROR", "WARN", "INFO", "DEBUG" };

void
gf_log_init (gf_log_level_t level)
{
    g_log_level = level;
}

void
gf_log (gf_log_level_t level, const char *format, ...)
{
    if (level > g_log_level)
        return;

    time_t now = time (NULL);
    struct tm *tm_info = localtime (&now);

    printf ("[%02d:%02d:%02d] [%s] ", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
            level_strings[level]);

    va_list args;
    va_start (args, format);
    vprintf (format, args);
    va_end (args);

    printf ("\n");
    fflush (stdout);
}
