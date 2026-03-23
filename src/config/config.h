#ifndef GF_CORE_CONFIG_H
#define GF_CORE_CONFIG_H

#include "../core/types.h"
#include "rules.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "../platform/platform_compat.h" // Centralized platform-specific includes

#define GF_MAX_LOCKED_WORKSPACES 32

typedef struct gf_config gf_config_t;

struct gf_config
{
    uint32_t max_windows_per_workspace;
    uint32_t max_workspaces;
    uint32_t default_padding;
    uint32_t min_window_size;
    uint32_t border_color;
    bool enable_borders;
    time_t last_modified;
    uint32_t locked_workspaces[GF_MAX_LOCKED_WORKSPACES];
    uint32_t locked_workspaces_count;
    gf_window_rule_t window_rules[GF_MAX_RULES];
    uint32_t window_rules_count;
};
// --- Configuration Lifecycle ---
const char *gf_config_get_path (void);
void gf_config_save (const char *filename, const gf_config_t *cfg);
gf_config_t load_or_create_config (const char *filename);

// --- Comparison & State ---
bool config_has_changed (const gf_config_t *old_cfg, const gf_config_t *new_cfg);
bool gf_config_changed (const gf_config_t *old_cfg, const gf_config_t *new_cfg);

// --- Workspace Locking ---
bool gf_config_workspace_is_locked (const gf_config_t *cfg, gf_ws_id_t ws);
gf_err_t gf_config_workspace_lock (gf_config_t *config, gf_ws_id_t ws_id);
gf_err_t gf_config_workspace_unlock (gf_config_t *config, gf_ws_id_t ws_id);

#endif
