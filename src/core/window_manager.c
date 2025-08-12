#include "../../include/core/window_manager.h"
#include "../../include/core/logger.h"
#include "../../include/utils/memory.h"
#include "core/geometry.h"
#include "core/types.h"
#include "core/workspace.h"
#include "utils/list.h"
#include "utils/workspace.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

gf_error_code_t
gf_window_manager_create (gf_window_manager_t **manager,
                          gf_platform_interface_t *platform,
                          gf_geometry_calculator_t *geometry_calc)
{
    if (!manager || !platform || !geometry_calc)
    {
        return GF_ERROR_INVALID_PARAMETER;
    }

    *manager = gf_calloc (1, sizeof (gf_window_manager_t));
    if (!*manager)
        return GF_ERROR_MEMORY_ALLOCATION;

    (*manager)->platform = platform;
    (*manager)->geometry_calc = geometry_calc;

    // Initialize window list
    gf_error_code_t result = gf_window_list_init (&(*manager)->state.windows, 16);
    if (result != GF_SUCCESS)
    {
        gf_free (*manager);
        *manager = NULL;
        return result;
    }

    // Create workspace manager
    result = gf_workspace_manager_create (&(*manager)->state.workspace_manager, platform);
    if (result != GF_SUCCESS)
    {
        gf_window_list_cleanup (&(*manager)->state.windows);
        gf_free (*manager);
        *manager = NULL;
        return result;
    }

    return GF_SUCCESS;
}

void
gf_window_manager_destroy (gf_window_manager_t *manager)
{
    if (!manager)
        return;

    gf_window_list_cleanup (&manager->state.windows);
    gf_workspace_manager_destroy (manager->state.workspace_manager);
    gf_free (manager);
}

gf_error_code_t
gf_window_manager_init (gf_window_manager_t *manager)
{
    if (!manager || !manager->platform || !manager->platform->init
        || !manager->platform->cleanup)
        return GF_ERROR_INVALID_PARAMETER;

    gf_error_code_t result
        = manager->platform->init (manager->platform, &manager->display);
    if (result != GF_SUCCESS)
        return result;

    // Initialize workspace manager
    result = gf_workspace_manager_update (manager->state.workspace_manager,
                                          manager->display);
    if (result != GF_SUCCESS)
    {
        manager->platform->cleanup (manager->display);
        return result;
    }

    manager->state.last_scan_time = time (NULL);
    manager->state.last_cleanup_time = time (NULL);
    manager->state.initialized = true;

    GF_LOG_INFO ("Window manager initialized successfully");
    return GF_SUCCESS;
}

void
gf_window_manager_cleanup (gf_window_manager_t *manager)
{
    if (!manager || !manager->state.initialized)
        return;

    manager->platform->cleanup (manager->display);
    manager->state.initialized = false;

    GF_LOG_INFO ("Window manager cleaned up");
}

gf_error_code_t
gf_window_manager_update_window_info (gf_window_manager_t *manager,
                                      gf_native_window_t window,
                                      gf_workspace_id_t workspace_id)
{
    if (!manager)
        return GF_ERROR_INVALID_PARAMETER;

    gf_window_id_t window_id = (gf_window_id_t)window;
    gf_window_info_t *existing
        = gf_window_list_find_by_window_id (&manager->state.windows, window_id);

    gf_rect_t geometry;
    gf_error_code_t result
        = manager->platform->get_window_geometry (manager->display, window, &geometry);
    if (result != GF_SUCCESS)
        return result;

    gf_window_info_t window_info = { .id = window_id,
                                     .native_handle = window,
                                     .workspace_id = workspace_id,
                                     .geometry = geometry,
                                     .is_maximized = false, // Will be updated by platform
                                     .needs_update = true,
                                     .is_valid = true,
                                     .last_modified = time (NULL) };

    if (existing)
    {
        result = gf_window_list_update (&manager->state.windows, &window_info);
    }
    else
    {
        result = gf_window_list_add (&manager->state.windows, &window_info);
    }

    return result;
}

gf_error_code_t
gf_window_manager_drag (gf_window_manager_t *manager, gf_workspace_id_t workspace_id)
{
    if (!manager)
        return GF_ERROR_INVALID_PARAMETER;

    gf_window_info_t *windows = manager->state.windows.items;
    uint32_t window_count = manager->state.windows.count;

    if (window_count == 0)
        return GF_SUCCESS;

    gf_rect_t dragged_geom;
    gf_window_info_t *dragged_window = NULL;
    for (uint32_t i = 0; i < window_count; i++)
    {
        gf_rect_t geom;
        gf_error_code_t is_dragging = manager->platform->is_window_drag (
            manager->display, windows[i].native_handle, &geom);

        if (is_dragging == GF_SUCCESS
            && (geom.width > 0 && geom.height > 0)) // Found the active drag window
        {
            dragged_geom = geom;
            dragged_window = &windows[i];
            dragged_window->geometry = geom;
            break;
        }
    }

    if (dragged_window == NULL)
        return GF_ERROR_PLATFORM_ERROR;

    gf_window_info_t *list_window;
    uint32_t list_count;

    gf_window_list_get_by_workspace (&manager->state.windows, workspace_id, &list_window,
                                     &list_count);

    if (!list_window)
    {
        return GF_ERROR_PLATFORM_ERROR;
    }

    int center_x = dragged_geom.x + dragged_geom.width / 2;
    int center_y = dragged_geom.y + dragged_geom.height / 2;

    gf_window_info_t *swap_target = NULL;
    int best_overlap_area = 0;

    for (uint32_t i = 0; i < list_count; i++)
    {
        if (list_window[i].native_handle == dragged_window->native_handle)
            continue;

        gf_rect_t cur_geom = list_window[i].geometry;

        // Directional filtering
        bool horizontally_adjacent
            = (dragged_geom.x < cur_geom.x + cur_geom.width
               && dragged_geom.x + dragged_geom.width > cur_geom.x);

        bool vertically_adjacent = (dragged_geom.y < cur_geom.y + cur_geom.height
                                    && dragged_geom.y + dragged_geom.height > cur_geom.y);

        // Check center point OR overlap area
        if (gf_rect_point_in (center_x, center_y, &cur_geom)
            || (horizontally_adjacent && vertically_adjacent))
        {
            // Calculate overlap area
            int overlap_w
                = fmin (dragged_geom.x + dragged_geom.width, cur_geom.x + cur_geom.width)
                  - fmax (dragged_geom.x, cur_geom.x);
            int overlap_h = fmin (dragged_geom.y + dragged_geom.height,
                                  cur_geom.y + cur_geom.height)
                            - fmax (dragged_geom.y, cur_geom.y);

            if (overlap_w > 0 && overlap_h > 0)
            {
                int area = overlap_w * overlap_h;
                if (area > best_overlap_area)
                {
                    best_overlap_area = area;
                    swap_target = &list_window[i];
                }
            }
        }
    }

    if (swap_target)
    {
        gf_window_manager_swap (manager, dragged_window, swap_target);
    }
    else
    {
        GF_LOG_DEBUG ("target swap not found");
        gf_rect_t *new_geometries = NULL;
        gf_error_code_t result = gf_window_manager_calculate_layout (
            manager, windows, window_count, &new_geometries);
        if (result != GF_SUCCESS)
        {
            gf_free (windows);
            return result;
        }

        gf_window_manager_unmaximize_all (manager, windows, window_count);
        gf_window_manager_apply_layout (manager, windows, new_geometries, window_count);
    }

    return GF_SUCCESS;
}

void
gf_window_manager_cleanup_invalid_windows (gf_window_manager_t *manager)
{
    if (!manager)
        return;

    uint32_t cleaned = 0;
    for (uint32_t i = 0; i < manager->state.windows.count;)
    {
        gf_window_info_t *window = &manager->state.windows.items[i];

        if (!manager->platform->is_window_valid (manager->display, window->native_handle))
        {
            gf_window_list_remove (&manager->state.windows, window->id);
            cleaned++;
        }
        else
        {
            i++;
        }
    }

    if (cleaned > 0)
    {
        GF_LOG_DEBUG ("Cleaned %u invalid windows", cleaned);
    }
}

void
gf_window_manager_print_stats (const gf_window_manager_t *manager)
{
    if (!manager)
        return;

    uint32_t workspace_counts[GF_MAX_WORKSPACES] = { 0 };
    gf_workspace_id_t max_workspace = -1;

    for (uint32_t i = 0; i < manager->state.windows.count; i++)
    {
        gf_workspace_id_t ws = manager->state.windows.items[i].workspace_id;
        if (ws >= 0 && ws < GF_MAX_WORKSPACES)
        {
            workspace_counts[ws]++;
            if (ws > max_workspace)
                max_workspace = ws;
        }
    }

    GF_LOG_DEBUG ("Window distribution (total: %u):", manager->state.windows.count);
    for (gf_workspace_id_t i = 0; i <= max_workspace; i++)
    {
        if (workspace_counts[i] > 0)
        {
            GF_LOG_DEBUG ("  Workspace %d: %u windows", i, workspace_counts[i]);
        }
    }
}

static gf_error_code_t
gf_window_manager_calculate_layout (gf_window_manager_t *manager,
                                    gf_window_info_t *windows, uint32_t window_count,
                                    gf_rect_t **out_geometries)
{
    if (!manager || !windows || !out_geometries || window_count == 0)
        return GF_ERROR_INVALID_PARAMETER;

    gf_rect_t workspace_bounds;
    gf_error_code_t result
        = manager->platform->get_screen_bounds (manager->display, &workspace_bounds);

    if (result != GF_SUCCESS)
    {
        return GF_ERROR_DISPLAY_CONNECTION;
    }

    gf_rect_t *new_geometries = gf_malloc (window_count * sizeof (gf_rect_t));
    if (!new_geometries)
    {
        return GF_ERROR_MEMORY_ALLOCATION;
    }

    manager->geometry_calc->calculate_layout (
        manager->geometry_calc, windows, window_count, &workspace_bounds, new_geometries);

    *out_geometries = new_geometries;
    return GF_SUCCESS;
}

gf_error_code_t
gf_window_manager_arrange_workspace (gf_window_manager_t *manager,
                                     gf_workspace_id_t workspace_id)
{
    if (!manager)
        return GF_ERROR_INVALID_PARAMETER;

    gf_window_info_t *windows = manager->state.windows.items;
    uint32_t window_count = manager->state.windows.count;

    gf_error_code_t result;
    if (window_count == 0 || !windows)
    {
        GF_LOG_WARN ("no window in workspace %d ", workspace_id);
        return GF_SUCCESS;
    }

    gf_window_manager_unmaximize_all (manager, windows, window_count);

    gf_rect_t *new_geometries = NULL;
    result = gf_window_manager_calculate_layout (manager, windows, window_count,
                                                 &new_geometries);
    if (result != GF_SUCCESS)
    {
        return result;
    }

    gf_window_manager_apply_layout (manager, windows, new_geometries, window_count);

    gf_free (new_geometries);
    return GF_SUCCESS;
}

gf_error_code_t
gf_window_manager_run (gf_window_manager_t *manager)
{
    if (!manager || !manager->state.initialized)
    {
        return GF_ERROR_INVALID_PARAMETER;
    }

    GF_LOG_INFO ("Window manager main loop started");

    time_t last_stats_time = time (NULL);

    while (true)
    {
        manager->state.loop_counter++;
        time_t current_time = time (NULL);

        // Update workspace information
        gf_error_code_t result = gf_workspace_manager_update (
            manager->state.workspace_manager, manager->display);
        if (result != GF_SUCCESS)
        {
            GF_LOG_WARN ("Failed to update workspace manager");
        }

        // Get current workspace and arrange if needed
        gf_workspace_id_t current_workspace
            = manager->platform->get_current_workspace (manager->display);

        if (current_workspace != manager->state.workspace_manager->active_workspace)
        {
            GF_LOG_DEBUG ("Workspace changed from %d to %d",
                          manager->state.workspace_manager->active_workspace,
                          current_workspace);
            manager->state.workspace_manager->active_workspace = current_workspace;
        }
        gf_window_manager_watch (manager);

        // Handle workspace overflow
        gf_workspace_manager_handle_overflow (manager->state.workspace_manager,
                                              manager->display, &manager->state.windows);

        result = gf_window_manager_arrange_workspace (manager, current_workspace);
        if (result != GF_SUCCESS)
        {
            GF_LOG_WARN ("Failed to arrange workspace %d", current_workspace);
        }
        gf_window_manager_drag (manager, current_workspace);

        // Periodic statistics
        if (current_time - last_stats_time >= 10)
        {
            gf_window_manager_print_stats (manager);
            last_stats_time = current_time;
        }

        // Periodic cleanup of invalid windows
        if (current_time - manager->state.last_cleanup_time >= 1)
        {
            gf_window_manager_cleanup_invalid_windows (manager);
            manager->state.last_cleanup_time = current_time;
        }

        // Adaptive sleep based on activity
        uint32_t sleep_time = manager->state.windows.count > 10 ? 15000 : 20000;
        usleep (sleep_time);
    }

    return GF_SUCCESS;
}

static void
gf_window_manager_unmaximize_all (gf_window_manager_t *manager, gf_window_info_t *windows,
                                  uint32_t window_count)
{
    if (!manager || !manager->platform || !windows)
        return;

    for (uint32_t i = 0; i < window_count; i++)
    {
        manager->platform->unmaximize_window (manager->display, windows[i].native_handle);
    }
}

static void
gf_window_manager_apply_layout (gf_window_manager_t *manager, gf_window_info_t *windows,
                                gf_rect_t *geometry, uint32_t window_count)
{
    if (!manager || !windows || !geometry || window_count == 0)
        return;

    gf_error_code_t result;

    for (uint32_t i = 0; i < window_count; i++)
    {
        result = manager->platform->set_window_geometry (
            manager->display, windows[i].native_handle, &geometry[i],
            GF_GEOMETRY_CHANGE_ALL | GF_GEOMETRY_APPLY_PADDING);

        if (result != GF_SUCCESS)
        {
            GF_LOG_WARN ("Failed to set geometry for window %llu",
                         (unsigned long long)windows[i].id);
        }
    }

    for (uint32_t i = 0; i < window_count; i++)
    {
        gf_window_manager_update_window_info (
            manager, windows[i].native_handle,
            manager->state.workspace_manager->active_workspace);
    }

    gf_window_list_clear_update_flags (
        &manager->state.windows, manager->state.workspace_manager->active_workspace);
}

gf_error_code_t
gf_window_manager_swap (gf_window_manager_t *manager, gf_window_info_t *src,
                        gf_window_info_t *dst)
{
    if (!manager || !src || !dst)
        return GF_ERROR_INVALID_PARAMETER;

    gf_window_list_t *list = &manager->state.windows;
    int src_idx = -1, dst_idx = -1;

    for (int i = 0; i < list->count; i++)
    {
        if (list->items[i].id == src->id)
            src_idx = i;
        if (list->items[i].id == dst->id)
            dst_idx = i;
    }

    if (src_idx < 0 || dst_idx < 0)
    {
        GF_LOG_WARN ("Swap aborted: one or both windows not found in manager list");
        return GF_ERROR_INVALID_PARAMETER;
    }

    gf_window_info_t *windows = manager->state.windows.items;
    uint32_t window_count = manager->state.windows.count;

    if (window_count == 0)
    {
        return GF_SUCCESS;
    }

    // Swap
    gf_window_info_t temp = windows[src_idx];
    windows[src_idx] = windows[dst_idx];
    windows[dst_idx] = temp;

    gf_rect_t *new_geometries = NULL;
    gf_error_code_t result = gf_window_manager_calculate_layout (
        manager, windows, window_count, &new_geometries);
    if (result != GF_SUCCESS)
    {
        gf_free (windows);
        return result;
    }

    gf_window_manager_unmaximize_all (manager, windows, window_count);
    gf_window_manager_apply_layout (manager, windows, new_geometries, window_count);

    gf_free (new_geometries);
    return GF_SUCCESS;
}

void
gf_window_manager_watch (gf_window_manager_t *manager)
{

    gf_window_info_t *windows = NULL;
    uint32_t window_count = 0;

    gf_workspace_id_t workspace_id = manager->state.workspace_manager->active_workspace;

    gf_error_code_t result = manager->platform->get_windows (
        manager->display, workspace_id, &windows, &window_count);
    if (result != GF_SUCCESS || window_count == 0)
    {
        return;
    }

    for (uint32_t i = 0; i < window_count; i++)
    {
        gf_window_manager_update_window_info (
            manager, windows[i].native_handle,
            manager->state.workspace_manager->active_workspace);
    }
}
