#ifndef GF_CORE_CONFIG_H
#define GF_CORE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>

typedef struct gf_config gf_config_t;

struct gf_config
{
    uint32_t max_windows_per_workspace;
    uint32_t max_workspaces;
    uint32_t default_padding;
    uint32_t min_window_size;
    time_t last_modified;
};
bool gf_config_has_changed (const gf_config_t *old_cfg, const gf_config_t *new_cfg);
gf_config_t load_or_create_config (const char *filename);
const char *gf_config_get_path (void);
bool config_has_changed (const gf_config_t *old_cfg, const gf_config_t *new_cfg);

#endif
