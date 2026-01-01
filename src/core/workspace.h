
#ifndef GF_CORE_WORKSPACE_H
#define GF_CORE_WORKSPACE_H

#include "../platform/platform.h"
#include "../utils/list.h"
#include <stdint.h>

// Workspace manager
typedef struct
{
    gf_workspace_list_t workspaces;
    gf_workspace_id_t active_workspace;
    gf_platform_interface_t *platform;
} gf_workspace_manager_t;

gf_error_code_t gf_workspace_manager_create (gf_workspace_manager_t **manager);
void gf_workspace_manager_destroy (gf_workspace_manager_t *manager);

gf_workspace_info_t *gf_workspace_manager_get_current (gf_workspace_manager_t *manager);
uint32_t gf_workspace_manager_calc_required_workspaces (uint32_t total_windows,
                                                        uint32_t current_workspaces,
                                                        uint32_t max_per_workspace);
gf_workspace_id_t gf_workspace_manager_find_free (gf_workspace_manager_t *manager,
                                                  uint32_t max_win_per_ws);
void gf_workspace_manager_ensure (gf_workspace_manager_t *manager,
                                  gf_workspace_id_t ws_id, uint32_t max_per_ws);
void gf_workspace_manager_rebuild_stats (gf_workspace_manager_t *wmgr,
                                         const gf_window_list_t *windows);

#endif // GF_CORE_WORKSPACE_H
