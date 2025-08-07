
#ifndef GF_CORE_WORKSPACE_H
#define GF_CORE_WORKSPACE_H

#include "../utils/collections.h"
#include "interfaces.h"

// Workspace manager
typedef struct {
  gf_workspace_list_t workspaces;
  gf_workspace_id_t active_workspace;
  gf_platform_interface_t *platform;
} gf_workspace_manager_t;

gf_error_code_t gf_workspace_manager_create(gf_workspace_manager_t **manager,
                                            gf_platform_interface_t *platform);
void gf_workspace_manager_destroy(gf_workspace_manager_t *manager);

gf_error_code_t gf_workspace_manager_update(gf_workspace_manager_t *manager,
                                            gf_display_t display);
gf_workspace_info_t *
gf_workspace_manager_get_current(gf_workspace_manager_t *manager);
gf_error_code_t
gf_workspace_manager_handle_overflow(gf_workspace_manager_t *manager,
                                     gf_display_t display,
                                     gf_window_list_t *windows);

#endif // GF_CORE_WORKSPACE_H
