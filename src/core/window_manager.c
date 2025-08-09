#include "../../include/core/window_manager.h"
#include "../../include/core/logger.h"
#include "../../include/utils/memory.h"
#include <stdio.h>
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
    gf_window_info_t *existing = gf_window_list_find (&manager->state.windows, window_id);

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

gf_error_code_t
gf_window_manager_arrange_workspace (gf_window_manager_t *manager,
                                     gf_workspace_id_t workspace_id)
{
    if (!manager)
        return GF_ERROR_INVALID_PARAMETER;

    // Get windows for the workspace
    gf_window_info_t *windows = NULL;
    uint32_t window_count = 0;

    gf_error_code_t result = manager->platform->get_windows (
        manager->display, workspace_id, &windows, &window_count);
    if (result != GF_SUCCESS)
        return result;

    if (window_count == 0)
    {
        gf_free (windows);
        return GF_SUCCESS;
    }

    // Unmaximize all windows first
    for (uint32_t i = 0; i < window_count; i++)
    {
        manager->platform->unmaximize_window (manager->display, windows[i].native_handle);
    }

    // Get workspace bounds
    gf_rect_t workspace_bounds;
    result = manager->platform->get_screen_bounds (manager->display, &workspace_bounds);

    if (result != GF_SUCCESS)
    {
        return GF_ERROR_DISPLAY_CONNECTION;
    }

    // Calculate new layout
    gf_rect_t *new_geometries = gf_malloc (window_count * sizeof (gf_rect_t));
    if (!new_geometries)
    {
        gf_free (windows);
        return GF_ERROR_MEMORY_ALLOCATION;
    }
    manager->geometry_calc->calculate_layout (
        manager->geometry_calc, windows, window_count, &workspace_bounds, new_geometries);

    // Apply new geometries
    for (uint32_t i = 0; i < window_count; i++)
    {
        result = manager->platform->set_window_geometry (
            manager->display, windows[i].native_handle, &new_geometries[i],
            GF_GEOMETRY_CHANGE_ALL | GF_GEOMETRY_APPLY_PADDING);

        if (result != GF_SUCCESS)
        {
            GF_LOG_WARN ("Failed to set geometry for window %llu",
                         (unsigned long long)windows[i].id);
        }
    }

    // Update window list and clear update flags
    for (uint32_t i = 0; i < window_count; i++)
    {
        gf_window_manager_update_window_info (manager, windows[i].native_handle,
                                              workspace_id);
    }
    gf_window_list_clear_update_flags (&manager->state.windows, workspace_id);

    gf_free (windows);
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

        // Periodic cleanup of invalid windows
        if (current_time - manager->state.last_cleanup_time >= 1)
        {
            gf_window_manager_cleanup_invalid_windows (manager);
            manager->state.last_cleanup_time = current_time;
        }

        // Handle workspace overflow
        gf_workspace_manager_handle_overflow (manager->state.workspace_manager,
                                              manager->display, &manager->state.windows);

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

        result = gf_window_manager_arrange_workspace (manager, current_workspace);
        if (result != GF_SUCCESS)
        {
            GF_LOG_WARN ("Failed to arrange workspace %d", current_workspace);
        }

        // Periodic statistics
        if (current_time - last_stats_time >= 10)
        {
            gf_window_manager_print_stats (manager);
            last_stats_time = current_time;
        }

        // Adaptive sleep based on activity
        uint32_t sleep_time = manager->state.windows.count > 10 ? 15000 : 20000;
        usleep (sleep_time);
    }

    return GF_SUCCESS;
}

void
gf_window_list_cleanup (gf_window_list_t *list)
{
    if (!list)
        return;

    gf_free (list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static gf_error_code_t
gf_window_list_ensure_capacity (gf_window_list_t *list, uint32_t required_capacity)
{
    if (list->capacity >= required_capacity)
        return GF_SUCCESS;

    uint32_t new_capacity = list->capacity;
    while (new_capacity < required_capacity)
    {
        new_capacity *= 2;
    }

    gf_window_info_t *new_items
        = gf_realloc (list->items, new_capacity * sizeof (gf_window_info_t));
    if (!new_items)
        return GF_ERROR_MEMORY_ALLOCATION;

    list->items = new_items;
    list->capacity = new_capacity;
    return GF_SUCCESS;
}

gf_error_code_t
gf_window_list_add (gf_window_list_t *list, const gf_window_info_t *window)
{
    if (!list || !window)
        return GF_ERROR_INVALID_PARAMETER;

    // Check if window already exists
    if (gf_window_list_find (list, window->id))
    {
        return gf_window_list_update (list, window);
    }

    gf_error_code_t result = gf_window_list_ensure_capacity (list, list->count + 1);
    if (result != GF_SUCCESS)
        return result;

    list->items[list->count] = *window;
    list->count++;

    GF_LOG_DEBUG ("Added window %llu to workspace %d (total: %u)",
                  (unsigned long long)window->id, window->workspace_id, list->count);
    return GF_SUCCESS;
}

gf_error_code_t
gf_window_list_remove (gf_window_list_t *list, gf_window_id_t window_id)
{
    if (!list)
        return GF_ERROR_INVALID_PARAMETER;

    for (uint32_t i = 0; i < list->count; i++)
    {
        if (list->items[i].id == window_id)
        {
            gf_workspace_id_t workspace_id = list->items[i].workspace_id;

            // Move last item to this position
            if (i < list->count - 1)
            {
                list->items[i] = list->items[list->count - 1];
            }
            list->count--;

            GF_LOG_DEBUG ("Removed window %llu from workspace %d (total: %u)",
                          (unsigned long long)window_id, workspace_id, list->count);
            return GF_SUCCESS;
        }
    }

    return GF_ERROR_WINDOW_NOT_FOUND;
}

gf_error_code_t
gf_window_list_update (gf_window_list_t *list, const gf_window_info_t *window)
{
    if (!list || !window)
        return GF_ERROR_INVALID_PARAMETER;

    gf_window_info_t *existing = gf_window_list_find (list, window->id);
    if (!existing)
        return GF_ERROR_WINDOW_NOT_FOUND;

    bool changed = (existing->geometry.x != window->geometry.x
                    || existing->geometry.y != window->geometry.y
                    || existing->geometry.width != window->geometry.width
                    || existing->geometry.height != window->geometry.height
                    || existing->workspace_id != window->workspace_id);

    *existing = *window;

    if (changed)
    {
        existing->last_modified = time (NULL);
        existing->needs_update = true;
    }

    return GF_SUCCESS;
}

gf_window_info_t *
gf_window_list_find (const gf_window_list_t *list, gf_window_id_t window_id)
{
    if (!list)
        return NULL;

    for (uint32_t i = 0; i < list->count; i++)
    {
        if (list->items[i].id == window_id)
        {
            return &list->items[i];
        }
    }

    return NULL;
}

uint32_t
gf_window_list_count_by_workspace (const gf_window_list_t *list,
                                   gf_workspace_id_t workspace_id)
{
    if (!list)
        return 0;

    uint32_t count = 0;
    for (uint32_t i = 0; i < list->count; i++)
    {
        if (list->items[i].workspace_id == workspace_id)
        {
            count++;
        }
    }

    return count;
}

void
gf_window_list_clear_update_flags (gf_window_list_t *list, gf_workspace_id_t workspace_id)
{
    if (!list)
        return;

    for (uint32_t i = 0; i < list->count; i++)
    {
        if (workspace_id < 0 || list->items[i].workspace_id == workspace_id)
        {
            list->items[i].needs_update = false;
        }
    }
}

gf_error_code_t
gf_window_list_get_by_workspace (const gf_window_list_t *list,
                                 gf_workspace_id_t workspace_id,
                                 gf_window_info_t **windows, uint32_t *count)
{
    if (!list || !windows || !count)
        return GF_ERROR_INVALID_PARAMETER;

    *count = gf_window_list_count_by_workspace (list, workspace_id);
    if (*count == 0)
    {
        *windows = NULL;
        return GF_SUCCESS;
    }

    *windows = gf_malloc (*count * sizeof (gf_window_info_t));
    if (!*windows)
        return GF_ERROR_MEMORY_ALLOCATION;

    uint32_t idx = 0;
    for (uint32_t i = list->count; i > idx && idx < *count; i--)
    {
        if (list->items[i].workspace_id == workspace_id)
        {
            (*windows)[idx++] = list->items[i];
        }
    }

    return GF_SUCCESS;
}
