#ifndef GF_CORE_CONFIG_H
#define GF_CORE_CONFIG_H

#include "types.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "platform_compat.h"  // Centralized platform-specific includes

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
    uint32_t locked_workspaces_count;
};
bool gf_config_has_changed (const gf_config_t *old_cfg, const gf_config_t *new_cfg);
gf_config_t load_or_create_config (const char *filename);
const char *gf_config_get_path (void);
bool config_has_changed (const gf_config_t *old_cfg, const gf_config_t *new_cfg);
bool gf_config_is_workspace_locked (const gf_config_t *cfg, gf_workspace_id_t ws);
gf_error_code_t gf_config_add_locked_workspace (gf_config_t *config,
                                                gf_workspace_id_t ws_id);
gf_error_code_t gf_config_remove_locked_workspace (gf_config_t *config,
                                                   gf_workspace_id_t ws_id);

#endif
