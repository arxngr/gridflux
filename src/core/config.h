#ifndef GF_CORE_CONFIG_H
#define GF_CORE_CONFIG_H

#include "core/types.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>
#define GF_MAX_LOCKED_WORKSPACES 32

typedef struct gf_config gf_config_t;

struct gf_config
{
    uint32_t max_windows_per_workspace;
    uint32_t max_workspaces;
    uint32_t default_padding;
    uint32_t min_window_size;
    time_t last_modified;
    uint32_t locked_workspaces[GF_MAX_LOCKED_WORKSPACES];
    uint32_t locked_workspace_count;
};
bool gf_config_has_changed (const gf_config_t *old_cfg, const gf_config_t *new_cfg);
gf_config_t load_or_create_config (const char *filename);
const char *gf_config_get_path (void);
bool config_has_changed (const gf_config_t *old_cfg, const gf_config_t *new_cfg);
bool gf_config_is_workspace_locked (const gf_config_t *cfg, gf_workspace_id_t ws);

#endif
