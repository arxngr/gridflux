#include "../../include/core/window_manager.h"
#include "../../include/core/logger.h"
#include "../../include/utils/memory.h"
#include "core/config.h"
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
    result = gf_workspace_manager_create (&(*manager)->state.workspace_manager);
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
gf_window_manager_drag (gf_window_manager_t *manager)
{
    if (!manager)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_id_t workspace_id = manager->state.workspace_manager->active_workspace;
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
            dragged_window->needs_update = true;
            dragged_window->geometry = geom;
            break;
        }
    }

    if (dragged_window == NULL)
        return GF_SUCCESS;

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
                    swap_target->needs_update = true;
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
    if (!manager || !manager->platform)
        return;

    gf_window_list_t *window_list = &manager->state.windows;
    if (window_list->count == 0 || !window_list->items)
        return;

    uint32_t removed = 0;

    for (uint32_t i = 0; i < window_list->count;)
    {
        gf_window_info_t *win_info = &window_list->items[i];

        bool excluded
            = manager->platform->is_window_excluded
              && manager->platform->is_window_excluded (
                  manager->display, (gf_native_window_t)win_info->native_handle);

        bool invalid = manager->platform->is_window_valid
                       && !manager->platform->is_window_valid (manager->display,
                                                               win_info->native_handle);

        if (excluded || invalid)
        {
            gf_window_list_remove (window_list, win_info->id);
            removed++;
            continue;
        }

        i++;
    }

    if (removed > 0)
    {
        GF_LOG_DEBUG ("Cleaned %u windows (excluded or invalid)", removed);
    }

    return;
}

void
gf_window_manager_print_stats (const gf_window_manager_t *manager)
{
    if (!manager)
        return;
    
    uint32_t *workspace_counts = gf_calloc(manager->config->max_workspaces, sizeof(uint32_t));
    if (!workspace_counts) {
        GF_LOG_ERROR("Failed to allocate workspace_counts");
        return;
    }
    
    gf_workspace_id_t max_workspace = -1;
    for (uint32_t i = 0; i < manager->state.windows.count; i++)
    {
        gf_workspace_id_t ws = manager->state.windows.items[i].workspace_id;
        if (ws >= 0 && ws < manager->config->max_workspaces)
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
    
    gf_free(workspace_counts);  
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
gf_window_manager_arrange_workspace (gf_window_manager_t *manager)
{
    if (!manager)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_manager_t *wmgr = manager->state.workspace_manager;
    gf_window_list_t *window_list = &manager->state.windows;

    if (window_list->count == 0 || !window_list->items)
        return GF_SUCCESS;

    // Unmaximize all windows before rearranging
    gf_window_manager_unmaximize_all (manager, window_list->items, window_list->count);

    for (uint32_t i = 0; i < wmgr->workspaces.count; i++)
    {
        gf_workspace_info_t *ws = &wmgr->workspaces.items[i];

        gf_window_info_t *ws_windows = NULL;
        uint32_t ws_window_count = 0;

        // Get only windows in this workspace
        if (gf_window_list_get_by_workspace (window_list, ws->id, &ws_windows,
                                             &ws_window_count)
                != GF_SUCCESS
            || ws_window_count == 0)
        {
            continue;
        }

        // Get screen bounds
        gf_rect_t workspace_bounds;
        if (manager->platform->get_screen_bounds (manager->display, &workspace_bounds)
            != GF_SUCCESS)
        {
            gf_free (ws_windows);
            continue;
        }

        gf_rect_t *new_geometries = NULL;
        if (gf_window_manager_calculate_layout (manager, ws_windows, ws_window_count,
                                                &new_geometries)
            == GF_SUCCESS)
        {
            gf_window_manager_apply_layout (manager, ws_windows, new_geometries,
                                            ws_window_count);
            gf_free (new_geometries);
        }

        gf_free (ws_windows);
    }

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

        gf_window_manager_load_cfg(manager);
        gf_window_manager_watch (manager);
        gf_window_manager_arrange_overflow (manager);
        gf_window_manager_arrange_workspace (manager);
        gf_window_manager_drag (manager);

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
        if (windows[i].is_maximized)
            manager->platform->unmaximize_window (manager->display,
                                                  windows[i].native_handle);
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
        if (!windows[i].needs_update && !windows[i].is_valid)
            continue;

        result = manager->platform->set_window_geometry (
            manager->display, windows[i].native_handle, &geometry[i],
            GF_GEOMETRY_CHANGE_ALL | GF_GEOMETRY_APPLY_PADDING,manager->config);

        if (result != GF_SUCCESS)
        {
            GF_LOG_WARN ("Failed to set geometry for window %llu",
                         (unsigned long long)windows[i].id);
        }
        gf_window_manager_update_window_info (manager, windows[i].native_handle,
                                              windows[i].workspace_id);

        gf_window_list_clear_update_flags (&manager->state.windows,
                                           windows[i].workspace_id);
    }
}

gf_error_code_t
gf_window_manager_swap (gf_window_manager_t *manager, const gf_window_info_t *src_copy,
                        const gf_window_info_t *dst_copy)
{
    if (!manager || !src_copy || !dst_copy)
        return GF_ERROR_INVALID_PARAMETER;

    gf_window_list_t *list = &manager->state.windows;
    int src_idx = -1, dst_idx = -1;

    // Match based on unique ID (not pointer)
    for (int i = 0; i < list->count; i++)
    {
        if (list->items[i].id == src_copy->id)
            src_idx = i;
        if (list->items[i].id == dst_copy->id)
            dst_idx = i;
    }

    if (src_idx < 0 || dst_idx < 0)
    {
        GF_LOG_WARN ("Swap aborted: one or both windows not found in manager list "
                     "(src_id=%d, dst_id=%d)",
                     src_copy->id, dst_copy->id);
        return GF_ERROR_INVALID_PARAMETER;
    }

    gf_window_info_t *windows = manager->state.windows.items;
    uint32_t window_count = manager->state.windows.count;

    // Swap in the actual list
    gf_window_info_t temp = windows[src_idx];
    windows[src_idx] = windows[dst_idx];
    windows[dst_idx] = temp;

    // Recalculate layout
    gf_rect_t *new_geometries = NULL;
    gf_error_code_t result = gf_window_manager_calculate_layout (
        manager, windows, window_count, &new_geometries);
    if (result != GF_SUCCESS)
        return result;

    gf_window_manager_unmaximize_all (manager, windows, window_count);
    gf_window_manager_apply_layout (manager, windows, new_geometries, window_count);

    gf_free (new_geometries);
    return GF_SUCCESS;
}

static void
gf_window_manager_sync_workspaces (gf_window_manager_t *manager)
{
    gf_workspace_manager_t *wmgr = manager->state.workspace_manager;
    uint32_t platform_count = manager->platform->get_workspace_count (manager->display);
    
    if (wmgr->workspaces.count > platform_count)
        wmgr->workspaces.count = platform_count;
    
    // Get current config value
    uint32_t current_max_windows = manager->config->max_windows_per_workspace;
    
    for (uint32_t i = 0; i < platform_count; i++)
    {
        gf_workspace_info_t *existing = gf_workspace_list_find (&wmgr->workspaces, i);
        
        if (!existing)
        {
            gf_workspace_info_t ws = { 
                .id = i,
                .window_count = 0,
                .max_windows = current_max_windows,
                .available_space = current_max_windows 
            };
            gf_workspace_list_add (&wmgr->workspaces, &ws);
            GF_LOG_DEBUG("Created workspace %u with max_windows=%u", i, current_max_windows);
        }
        else
        {
            if (existing->max_windows != current_max_windows)
            {
                GF_LOG_INFO("Updating workspace %u: max_windows %u -> %u", 
                           i, existing->max_windows, current_max_windows);
                
                existing->max_windows = current_max_windows;
                
                int32_t avail = (int32_t)current_max_windows - (int32_t)existing->window_count;
                existing->available_space = (avail < 0) ? 0 : avail;
            }
        }
    }
}

static gf_error_code_t
gf_window_manager_arrange_overflow (gf_window_manager_t *manager)
{
    gf_workspace_manager_t *wmgr = manager->state.workspace_manager;
    gf_window_list_t *windows = &manager->state.windows;

    for (uint32_t i = 0; i < wmgr->workspaces.count; i++)
    {
        wmgr->workspaces.items[i].window_count
            = gf_window_list_count_by_workspace (windows, wmgr->workspaces.items[i].id);

        int32_t avail = (int32_t)wmgr->workspaces.items[i].max_windows
                        - (int32_t)wmgr->workspaces.items[i].window_count;
        if (avail < 0)
            avail = 0; // clamp
        wmgr->workspaces.items[i].available_space = avail;
    }

    uint32_t *moved_ids = gf_calloc(manager->config->max_windows_per_workspace, sizeof(uint32_t));
    uint32_t moved_count = 0;

    for (uint32_t i = 0; i < wmgr->workspaces.count; i++)
    {
        gf_workspace_info_t *src_ws = &wmgr->workspaces.items[i];
        int32_t overflow = (int32_t)src_ws->window_count - (int32_t)src_ws->max_windows;

        if (overflow <= 0)
            continue;

        gf_window_info_t *overflow_windows = NULL;
        uint32_t overflow_window_count = 0;
        if (gf_window_list_get_by_workspace (windows, src_ws->id, &overflow_windows,
                                             &overflow_window_count)
            != GF_SUCCESS)
            continue;

        uint32_t moved_from_this_ws = 0;
        for (uint32_t j = 0;
             j < wmgr->workspaces.count && moved_from_this_ws < (uint32_t)overflow; j++)
        {
            gf_workspace_info_t *dst_ws = &wmgr->workspaces.items[j];
            if (dst_ws->id == src_ws->id || dst_ws->available_space <= 0)
                continue;

            uint32_t can_move = dst_ws->available_space;
            if (can_move > (overflow - moved_from_this_ws))
                can_move = overflow - moved_from_this_ws;

            for (uint32_t x = 0; x < can_move; x++)
            {
                uint32_t idx = moved_from_this_ws + x;

                // skip if already moved this pass
                bool already_moved = false;
                for (uint32_t m = 0; m < moved_count; m++)
                {
                    if (moved_ids[m] == overflow_windows[idx].id)
                    {
                        already_moved = true;
                        break;
                    }
                }
                if (already_moved)
                    continue;

                manager->platform->move_window_to_workspace (
                    manager->display, overflow_windows[idx].native_handle, dst_ws->id);

                GF_LOG_DEBUG ("Moving window %u from workspace %u to %u",
                              overflow_windows[idx].native_handle, src_ws->id,
                              dst_ws->id);

                // update tracking
                moved_ids[moved_count++] = overflow_windows[idx].id;
                wmgr->active_workspace = dst_ws->id;

                // update counts immediately
                src_ws->window_count--;
                dst_ws->window_count++;
                src_ws->available_space++;
                dst_ws->available_space--;
                for (uint32_t w = 0; w < windows->count; w++)
                {
                    if (windows->items[w].id == overflow_windows[idx].id)
                    {
                        windows->items[w].workspace_id = dst_ws->id;
                        break;
                    }
                }
            }
            moved_from_this_ws += can_move;
        }
        gf_free (overflow_windows);
    }

    return GF_SUCCESS;
}

void
gf_window_manager_watch (gf_window_manager_t *manager)
{
    if (!manager || !manager->state.workspace_manager)
        return;

    gf_workspace_manager_t *wmgr = manager->state.workspace_manager;

    wmgr->active_workspace = manager->platform->get_current_workspace (manager->display);

    gf_window_manager_sync_workspaces (manager);

    for (uint32_t ws_id = 0; ws_id < wmgr->workspaces.count; ws_id++)
    {
        gf_window_info_t *windows = NULL;
        uint32_t count = 0;
        if (manager->platform->get_windows (manager->display, ws_id, &windows, &count)
            == GF_SUCCESS)
        {
            for (uint32_t i = 0; i < count; i++)
            {
                if (windows[i].is_valid)
                    gf_window_list_add (&manager->state.windows, &windows[i]);
            }
        }
    }

    for (uint32_t i = 0; i < wmgr->workspaces.count; i++)
    {
        gf_workspace_info_t *ws = &wmgr->workspaces.items[i];
    }
}


void gf_window_manager_load_cfg(gf_window_manager_t *manager) {
    if (!manager->config) {
        GF_LOG_ERROR("Config not initialized in main()");
        return;
    }
    
    struct stat st;
    if (stat(GLOB_CFG, &st) != 0) {
        return;
    }
    
    // Check if file has been modified
    if (st.st_mtime <= manager->config->last_modified) {
        return;
    }
    
    usleep(10000); // Small delay
    
    // Reload and check for changes
    gf_config_t old_config = *manager->config;
    gf_config_t new_config = load_or_create_config(GLOB_CFG);
    
    if (gf_config_has_changed(&old_config, &new_config)) {
        GF_LOG_INFO("Configuration changed! Reloading from: %s", GLOB_CFG);
        
        *manager->config = new_config;
        manager->config->last_modified = st.st_mtime;
        
        gf_window_manager_sync_workspaces(manager);
    } else {
        manager->config->last_modified = st.st_mtime;
    }
}

