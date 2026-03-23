#ifndef GF_CORE_LOGGER_H
#define GF_CORE_LOGGER_H

#include "../core/types.h"

void gf_log_init (gf_log_level_t level);
void gf_log (gf_log_level_t level, const char *format, ...);

#define GF_LOG_ERROR(...) gf_log (GF_LOG_ERROR, __VA_ARGS__)
#define GF_LOG_WARN(...) gf_log (GF_LOG_WARN, __VA_ARGS__)
#define GF_LOG_INFO(...) gf_log (GF_LOG_INFO, __VA_ARGS__)
#define GF_LOG_DEBUG(...) gf_log (GF_LOG_DEBUG, __VA_ARGS__)

#endif // GF_CORE_LOGGER_H
