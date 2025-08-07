#include "../../include/core/workspace.h"
#include "../../include/core/logger.h"
#include "../../include/utils/memory.h"

gf_error_code_t gf_workspace_manager_create(gf_workspace_manager_t **manager,
                                            gf_platform_interface_t *platform) {
  if (!manager || !platform)
    return GF_ERROR_INVALID_PARAMETER;

  *manager = gf_calloc(1, sizeof(gf_workspace_manager_t));
  if (!*manager)
    return GF_ERROR_MEMORY_ALLOCATION;

  gf_error_code_t result = gf_workspace_list_init(&(*manager)->workspaces, 8);
  if (result != GF_SUCCESS) {
    gf_free(*manager);
    *manager = NULL;
    return result;
  }

  (*manager)->platform = platform;
  (*manager)->active_workspace = -1;

  return GF_SUCCESS;
}

void gf_workspace_manager_destroy(gf_workspace_manager_t *manager) {
  if (!manager)
    return;

  gf_workspace_list_cleanup(&manager->workspaces);
  gf_free(manager);
}

gf_error_code_t gf_workspace_manager_update(gf_workspace_manager_t *manager,
                                            gf_display_t display) {
  if (!manager)
    return GF_ERROR_INVALID_PARAMETER;

  manager->active_workspace = manager->platform->get_current_workspace(display);
  uint32_t workspace_count = manager->platform->get_workspace_count(display);

  // Update workspace list
  for (uint32_t i = 0; i < workspace_count; i++) {
    if (!gf_workspace_list_find(&manager->workspaces, i)) {
      gf_workspace_info_t workspace = {
          .id = i,
          .window_count = 0,
          .max_windows = GF_MAX_WINDOWS_PER_WORKSPACE,
          .available_space = GF_MAX_WINDOWS_PER_WORKSPACE,
          .bounds = {0, 0, 1920, 1080} // TODO: Get actual bounds
      };

      gf_error_code_t result =
          manager->platform->get_screen_bounds(display, &workspace.bounds);
      if (result != GF_SUCCESS) {
        GF_LOG_WARN("Failed to get screen bounds for workspace %u", i);
      }

      gf_workspace_list_add(&manager->workspaces, &workspace);
    }
  }

  return GF_SUCCESS;
}

gf_workspace_info_t *
gf_workspace_manager_get_current(gf_workspace_manager_t *manager) {
  if (!manager)
    return NULL;
  return gf_workspace_list_find(&manager->workspaces,
                                manager->active_workspace);
}

gf_error_code_t
gf_workspace_manager_handle_overflow(gf_workspace_manager_t *manager,
                                     gf_display_t display,
                                     gf_window_list_t *windows) {
  if (!manager || !windows)
    return GF_ERROR_INVALID_PARAMETER;

  // Find workspaces with overflow and free space
  gf_workspace_id_t overflow_workspaces[GF_MAX_WORKSPACES];
  gf_workspace_id_t free_workspaces[GF_MAX_WORKSPACES];
  uint32_t overflow_count = 0, free_count = 0;

  for (uint32_t i = 0; i < manager->workspaces.count; i++) {
    gf_workspace_info_t *workspace = &manager->workspaces.items[i];
    uint32_t window_count =
        gf_window_list_count_by_workspace(windows, workspace->id);

    if (window_count > GF_MAX_WINDOWS_PER_WORKSPACE) {
      overflow_workspaces[overflow_count++] = workspace->id;
    } else if (window_count < GF_MAX_WINDOWS_PER_WORKSPACE) {
      free_workspaces[free_count++] = workspace->id;
    }
  }

  // Move windows from overflow to free workspaces
  for (uint32_t i = 0; i < overflow_count && free_count > 0; i++) {
    gf_window_info_t *workspace_windows;
    uint32_t window_count;

    gf_error_code_t result = gf_window_list_get_by_workspace(
        windows, overflow_workspaces[i], &workspace_windows, &window_count);
    if (result != GF_SUCCESS)
      continue;

    // Move excess windows
    uint32_t excess = window_count - GF_MAX_WINDOWS_PER_WORKSPACE;
    uint32_t moved = 0;

    for (uint32_t j = 0; j < window_count && moved < excess && free_count > 0;
         j++) {
      gf_workspace_id_t target_workspace = free_workspaces[0];

      result = manager->platform->move_window_to_workspace(
          display, workspace_windows[j].native_handle, target_workspace);
      if (result == GF_SUCCESS) {
        workspace_windows[j].workspace_id = target_workspace;
        moved++;

        // Update free workspace count
        uint32_t target_count =
            gf_window_list_count_by_workspace(windows, target_workspace);
        if (target_count >= GF_MAX_WINDOWS_PER_WORKSPACE - 1) {
          // Remove from free list
          for (uint32_t k = 0; k < free_count - 1; k++) {
            free_workspaces[k] = free_workspaces[k + 1];
          }
          free_count--;
        }
      }
    }

    gf_free(workspace_windows);
  }

  return GF_SUCCESS;
}
