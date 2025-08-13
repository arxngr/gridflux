
#ifndef GF_CORE_WORKSPACE_H
#define GF_CORE_WORKSPACE_H

#include "../utils/list.h"
#include "interfaces.h"
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
gf_error_code_t gf_workspace_manager_handle_overflow (gf_workspace_manager_t *manager,
                                                      gf_display_t display,
                                                      gf_window_list_t *windows);
void gf_workspace_manager_update_free_workspace (
    gf_workspace_manager_t *manager, gf_window_list_t *windows, gf_window_info_t *window,
    gf_workspace_id_t target_workspace, gf_workspace_id_t *free_workspaces,
    uint32_t *free_count, gf_error_code_t result, uint32_t *moved);
void gf_workspace_manager_find_free_workspace (gf_workspace_manager_t *manager,
                                               gf_window_list_t *windows,
                                               gf_workspace_id_t *overflow_workspaces,
                                               uint32_t *overflow_count,
                                               gf_workspace_id_t *free_workspaces,
                                               uint32_t *free_count);

#endif // GF_CORE_WORKSPACE_H
