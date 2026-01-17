#include "window_manager.h"
#include "config.h"
#include "internal.h"
#include "layout.h"
#include "list.h"
#include "logger.h"
#include "memory.h"
#include "platform_compat.h"
#include "types.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static gf_workspace_info_t *
_get_workspace (gf_workspace_list_t *workspaces, gf_workspace_id_t id)
{
    if (!workspaces || id < 0 || id >= workspaces->count)
        return NULL;
    return &workspaces->items[id];
}

void
_cleanup_unused_workspace (gf_workspace_list_t *list, uint32_t index)
{
    if (!list || index >= list->count)
        return;

    memmove (&list->items[index], &list->items[index + 1],
             (list->count - index - 1) * sizeof (gf_workspace_info_t));

    list->count--;
}

static bool
_workspace_is_valid (gf_workspace_list_t *workspaces, gf_workspace_id_t id)
{
    return _get_workspace (workspaces, id) != NULL;
}

static bool
_workspace_has_capacity (gf_workspace_info_t *ws, uint32_t max_per_ws)
{
    return !ws->is_locked && ws->window_count < max_per_ws;
}

static void
_rebuild_workspace_stats (gf_workspace_list_t *workspaces, gf_window_list_t *windows,
                          uint32_t max_per_ws)
{
    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        workspaces->items[i].window_count = 0;
        workspaces->items[i].available_space = max_per_ws;
    }

    for (uint32_t i = 0; i < windows->count; i++)
    {
        if (windows->items[i].is_valid)
        {
            gf_workspace_id_t ws_id = windows->items[i].workspace_id;
            if (_workspace_is_valid (workspaces, ws_id))
            {
                workspaces->items[ws_id].window_count++;
                workspaces->items[ws_id].available_space--;
            }
        }
    }
}

static bool
_window_has_valid_workspace (gf_window_info_t *win, gf_workspace_list_t *workspaces)
{
    return win->workspace_id >= 0 && _workspace_is_valid (workspaces, win->workspace_id);
}

void
gf_window_manager_get_window_name (const gf_window_manager_t *m,
                                   gf_native_window_t handle, char *buffer, size_t size)
{
    if (m->platform && m->platform->window_name_info)
    {
        m->platform->window_name_info (m->display, handle, buffer, size);
    }
    else
    {
        snprintf (buffer, size, "N/A");
    }
}

static void
_minimize_workspace_windows (gf_window_manager_t *m, gf_window_info_t *ws_list,
                             uint32_t count)
{
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    for (uint32_t i = 0; i < count; i++)
    {
        if (wm_is_excluded (m, ws_list[i].native_handle))
            continue;

        platform->minimize_window (display, ws_list[i].native_handle);
        ws_list[i].is_minimized = true;
    }
}

static void
_unminimize_workspace_windows (gf_window_manager_t *m, gf_window_info_t *ws_list,
                               uint32_t count)
{
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    for (uint32_t i = 0; i < count; i++)
    {
        gf_window_info_t *win = &ws_list[i];

        if (wm_is_excluded (m, win->native_handle))
        {
            continue;
        }
        if (platform->is_window_hidden
            && platform->is_window_hidden (display, win->native_handle))
        {
            GF_LOG_DEBUG ("Skipping hidden window %lu (closed to tray)", win->id);
            continue;
        }

        platform->unminimize_window (display, win->native_handle);
        win->is_minimized = false;

        gf_window_info_t *actual
            = gf_window_list_find_by_window_id (wm_windows (m), win->id);
        if (actual)
        {
            actual->is_minimized = false;
        }
    }
}

static void
_print_workspace_header (gf_workspace_id_t id, bool is_locked, uint32_t count,
                         uint32_t max_windows, int32_t available)
{
    const char *lock_str = is_locked ? "LOCKED" : "unlocked";
    GF_LOG_INFO ("Workspace %u (%s): %u/%u windows, %d available", id, lock_str, count,
                 max_windows, available);
}

static void
_print_window_info (uint32_t window_id, const char *name)
{
    GF_LOG_INFO ("  - [%u] %s", window_id, name);
}

static void
_window_manager_handle_new_window (gf_window_manager_t *m, gf_window_info_t *new_window,
                                   gf_workspace_id_t workspace_id)
{
    if (!m || !new_window)
        return;

    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_window_list_t *windows = wm_windows (m);
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    GF_LOG_INFO ("Handling new window %lu in workspace %u", new_window->id, workspace_id);

    //  Get all windows in the assigned workspace
    gf_window_info_t *workspace_windows = NULL;
    uint32_t workspace_win_count = 0;

    if (gf_window_list_get_by_workspace (windows, workspace_id, &workspace_windows,
                                         &workspace_win_count)
        != GF_SUCCESS)
    {
        GF_LOG_WARN ("Failed to get windows for workspace %u", workspace_id);
        return;
    }

    // Minimize ALL windows in that workspace EXCEPT the new one
    for (uint32_t i = 0; i < workspace_win_count; i++)
    {
        gf_window_info_t *win = &workspace_windows[i];

        // Skip the new window itself
        if (win->id == new_window->id)
            continue;

        if (wm_is_excluded (m, win->native_handle))
            continue;

        if (!win->is_minimized)
        {
            GF_LOG_DEBUG ("Minimizing window %lu in workspace %u", win->id, workspace_id);
            platform->minimize_window (display, win->native_handle);

            // Update the actual window in the list
            gf_window_info_t *actual
                = gf_window_list_find_by_window_id (windows, win->id);
            if (actual)
            {
                actual->is_minimized = true;
            }
        }
    }

    gf_free (workspace_windows);

    // Minimize windows in OTHER workspaces
    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_workspace_id_t ws_id = workspaces->items[i].id;

        // Skip the workspace where the new window is
        if (ws_id == workspace_id)
            continue;

        gf_window_info_t *list = NULL;
        uint32_t count = 0;

        if (gf_window_list_get_by_workspace (windows, ws_id, &list, &count) != GF_SUCCESS)
            continue;

        _minimize_workspace_windows (m, list, count);
        gf_free (list);
    }

    GF_LOG_INFO ("Focusing new window %lu", new_window->id);
    platform->unminimize_window (display, new_window->native_handle);

    m->state.last_active_window = new_window->id;
    m->state.last_active_workspace = workspace_id;

    workspaces->active_workspace = workspace_id;

    GF_LOG_INFO ("New window %lu is now focused in workspace %u", new_window->id,
                 workspace_id);

    if (m->config->enable_borders && m->platform->add_border
        && !wm_is_excluded (m, new_window->native_handle))
    {
        m->platform->add_border (m->platform, new_window->native_handle,
                                 m->config->border_color, 3);
    }
}

static void
_handle_new_focused_window (gf_window_manager_t *m, gf_window_id_t curr_win_id,
                            gf_workspace_list_t *workspaces)
{
    gf_window_list_t *windows = wm_windows (m);

    gf_window_info_t win = { 0 };
    win.id = curr_win_id;
    win.native_handle = (gf_native_window_t)curr_win_id;
    win.is_valid = true;
    win.workspace_id
        = gf_workspace_list_find_free (workspaces, m->config->max_windows_per_workspace);

    if (!wm_is_excluded (m, win.native_handle))
    {
        gf_window_list_add (windows, &win);
    }
}

static void
_detect_minimization_changes (gf_window_manager_t *m, gf_workspace_id_t current_workspace)
{
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_window_list_t *windows = wm_windows (m);

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_window_info_t *win = &windows->items[i];

        if (!win->is_valid || wm_is_excluded (m, win->native_handle))
            continue;

        if (win->workspace_id != current_workspace)
            continue;

        bool currently_minimized
            = platform->is_window_minimized (display, win->native_handle);

        if (win->is_minimized != currently_minimized)
        {
            win->is_minimized = currently_minimized;
        }
    }
}

static void
_handle_workspace_switch (gf_window_manager_t *m, gf_workspace_id_t current_workspace)
{
    gf_window_list_t *windows = wm_windows (m);
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    GF_LOG_DEBUG ("Workspace changed from %d to %d", m->state.last_active_workspace,
                  current_workspace);

    gf_window_info_t *workspace_windows = NULL;
    uint32_t workspace_win_count = 0;

    if (gf_window_list_get_by_workspace (windows, current_workspace, &workspace_windows,
                                         &workspace_win_count)
        != GF_SUCCESS)
        return;

    // Minimize windows in other workspaces
    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_workspace_id_t ws_id = workspaces->items[i].id;

        if (ws_id == current_workspace)
            continue;

        gf_window_info_t *list = NULL;
        uint32_t count = 0;

        if (gf_window_list_get_by_workspace (windows, ws_id, &list, &count) == GF_SUCCESS)
        {
            _minimize_workspace_windows (m, list, count);
            gf_free (list);
        }
    }

    // Unminimize windows in current workspace
    _unminimize_workspace_windows (m, workspace_windows, workspace_win_count);
    gf_free (workspace_windows);
}

gf_error_code_t
gf_window_manager_create (gf_window_manager_t **manager,
                          gf_platform_interface_t *platform, gf_layout_engine_t *layout)
{
    if (!manager || !platform || !layout)
        return GF_ERROR_INVALID_PARAMETER;

    *manager = gf_calloc (1, sizeof (**manager));
    if (!*manager)
        return GF_ERROR_MEMORY_ALLOCATION;

    (*manager)->platform = platform;
    (*manager)->layout = layout;
    (*manager)->ipc_handle = -1;

    if (gf_window_list_init (wm_windows (*manager), 16) != GF_SUCCESS)
        goto fail;

    if (gf_workspace_list_init (wm_workspaces (*manager), 16) != GF_SUCCESS)
        goto fail;

    return GF_SUCCESS;

fail:
    gf_window_list_cleanup (wm_windows (*manager));
    gf_free (*manager);
    *manager = NULL;
    return GF_ERROR_MEMORY_ALLOCATION;
}

void
gf_window_manager_destroy (gf_window_manager_t *m)
{
    if (!m)
        return;

    gf_window_list_cleanup (wm_windows (m));
    gf_workspace_list_cleanup (wm_workspaces (m));
    gf_free (m);
}

gf_error_code_t
gf_window_manager_init (gf_window_manager_t *m)
{
    if (!m || !m->platform || !m->platform->init || !m->platform->cleanup)
        return GF_ERROR_INVALID_PARAMETER;

    gf_platform_interface_t *platform = wm_platform (m);

    gf_error_code_t result = platform->init (platform, wm_display (m));
    if (result != GF_SUCCESS)
        return result;

    m->state.last_scan_time = time (NULL);
    m->state.last_cleanup_time = time (NULL);
    m->state.initialized = true;

    // Initialize IPC server
    m->ipc_handle = gf_ipc_server_create ();
    if (m->ipc_handle < 0)
    {
        GF_LOG_WARN ("Failed to create IPC server - client commands will not work");
    }

    GF_LOG_INFO ("Window manager initialized successfully");
    gf_window_manager_print_stats (m);
    return GF_SUCCESS;
}

void
gf_window_manager_cleanup (gf_window_manager_t *m)
{
    if (!m || !m->state.initialized)
        return;

    gf_platform_interface_t *platform = wm_platform (m);
    platform->cleanup (*wm_display (m), platform);
    m->state.initialized = false;

    // Cleanup IPC server
    if (m->ipc_handle >= 0)
    {
        gf_ipc_server_destroy (m->ipc_handle);
        m->ipc_handle = -1;
    }

    GF_LOG_INFO ("Window manager cleaned up");
}

gf_error_code_t
gf_window_manager_update_window_info (gf_window_manager_t *m, gf_native_window_t window,
                                      gf_workspace_id_t workspace_id)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_window_list_t *windows = wm_windows (m);
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    gf_window_id_t id = (gf_window_id_t)window;

    if (wm_is_excluded (m, window))
    {
        if (m->platform->remove_border)
            m->platform->remove_border (m->platform, window);
        return GF_SUCCESS;
    }

    gf_rect_t geom;
    if (platform->get_window_geometry (display, window, &geom) != GF_SUCCESS)
        return GF_ERROR_PLATFORM_ERROR;

    gf_window_info_t info = {
        .id = id,
        .native_handle = window,
        .workspace_id = workspace_id,
        .geometry = geom,
        .is_minimized = true,
        .is_valid = true,
        .needs_update = true,
        .last_modified = time (NULL),
    };

    gf_window_info_t *existing = gf_window_list_find_by_window_id (windows, id);

    return existing ? gf_window_list_update (windows, &info)
                    : gf_window_list_add (windows, &info);
}

void
gf_window_manager_cleanup_invalid_data (gf_window_manager_t *m)
{
    if (!m || !wm_platform (m))
        return;

    gf_window_list_t *windows = wm_windows (m);
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    if (!windows || !workspaces)
        return;

    uint32_t removed_windows = 0;

    /* Remove invalid/excluded windows */
    for (uint32_t i = 0; i < windows->count;)
    {
        gf_window_info_t *win = &windows->items[i];

        bool excluded = wm_is_excluded (m, win->native_handle);
        bool invalid = !wm_is_valid (m, win->native_handle);

        bool hidden = false;
        if (m->platform->is_window_hidden)
        {
            hidden = m->platform->is_window_hidden (m->display, win->native_handle);
            if (m->platform->remove_border && hidden)
            {
                m->platform->remove_border (m->platform, win->native_handle);
            }
        }

        if (excluded || invalid || hidden)
        {
            gf_window_list_remove (windows, win->id);
            m->platform->remove_border (m->platform, win->native_handle);
            removed_windows++;
            continue;
        }

        i++;
    }

    if (removed_windows > 0)
        GF_LOG_DEBUG ("Cleaned %u invalid/excluded windows", removed_windows);

    /* Recalculate window counts */
    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        workspaces->items[i].window_count
            = gf_window_list_count_by_workspace (windows, workspaces->items[i].id);
    }

    for (uint32_t i = 0; i < workspaces->count;)
    {
        gf_workspace_info_t *ws = &workspaces->items[i];

        bool is_active = (ws->id == workspaces->active_workspace);
        bool is_empty = (ws->window_count == 0);
        bool can_remove = (workspaces->count > 1);

        if (is_empty && !is_active && can_remove)
        {
            GF_LOG_INFO ("Removing empty workspace %u", ws->id);

            _cleanup_unused_workspace (workspaces, i);
            continue;
        }

        i++;
    }
}

static void
gf_window_manager_sync_workspaces (gf_window_manager_t *m)
{
    gf_workspace_list_t *workspaces = wm_workspaces (m);
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    uint32_t platform_count = platform->get_workspace_count (display);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    for (uint32_t i = 0; i < platform_count; i++)
    {
        gf_workspace_list_ensure (workspaces, i, max_per_ws);
        gf_workspace_info_t *ws = &workspaces->items[i];

        ws->is_locked = gf_config_is_workspace_locked (m->config, i);
        ws->available_space = ws->is_locked ? 0 : (max_per_ws - ws->window_count);
        ws->max_windows = max_per_ws;
    }
}

void
gf_window_manager_print_stats (const gf_window_manager_t *m)
{
    if (!m)
        return;

    const gf_window_list_t *windows = &m->state.windows;
    const gf_workspace_list_t *workspaces = &m->state.workspaces;

    uint32_t *workspace_counts = gf_calloc (m->config->max_workspaces, sizeof (uint32_t));
    if (!workspace_counts)
    {
        GF_LOG_ERROR ("Failed to allocate workspace_counts");
        return;
    }

    gf_workspace_id_t max_workspace = -1;

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_workspace_id_t ws = windows->items[i].workspace_id;
        if (ws >= 0 && ws < m->config->max_workspaces)
        {
            workspace_counts[ws]++;
            if (ws > max_workspace)
                max_workspace = ws;
        }
    }

    GF_LOG_INFO ("=== Stats ===");
    GF_LOG_INFO ("Total Windows: %u, Total Workspaces: %u", windows->count,
                 workspaces->count);

    char win_name[256];

    for (gf_workspace_id_t i = 0; i <= max_workspace; i++)
    {
        uint32_t count = workspace_counts[i];
        uint32_t max_windows = m->config->max_windows_per_workspace;
        int32_t available = -1;
        bool is_locked = gf_config_is_workspace_locked (m->config, i);

        gf_workspace_info_t *ws = _get_workspace ((gf_workspace_list_t *)workspaces, i);
        if (ws)
        {
            max_windows = ws->max_windows;
            available = ws->available_space;
            is_locked = ws->is_locked;
        }
        else
        {
            // Workspace doesn't exist yet, calculate available space
            available = is_locked ? 0 : max_windows;
        }

        _print_workspace_header (i, is_locked, count, max_windows, available);

        for (uint32_t w = 0; w < windows->count; w++)
        {
            const gf_window_info_t *win = &windows->items[w];

            if (win->workspace_id != i)
                continue;

            gf_window_manager_get_window_name (m, win->native_handle, win_name,
                                               sizeof (win_name));
            _print_window_info (win->id, win_name);
        }
    }

    gf_free (workspace_counts);
}

static gf_error_code_t
gf_window_manager_calculate_layout (gf_window_manager_t *m, gf_window_info_t *windows,
                                    uint32_t window_count, gf_rect_t **out_geometries)
{
    if (!m || !windows || !out_geometries || window_count == 0)
        return GF_ERROR_INVALID_PARAMETER;

    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    gf_rect_t workspace_bounds;
    gf_error_code_t result = platform->get_screen_bounds (display, &workspace_bounds);

    if (result != GF_SUCCESS)
        return GF_ERROR_DISPLAY_CONNECTION;

    gf_rect_t *new_geometries = gf_malloc (window_count * sizeof (gf_rect_t));
    if (!new_geometries)
        return GF_ERROR_MEMORY_ALLOCATION;

    wm_geometry (m)->apply_layout (wm_geometry (m), windows, window_count,
                                   &workspace_bounds, new_geometries);

    *out_geometries = new_geometries;
    return GF_SUCCESS;
}

static void
gf_window_manager_assign_workspaces (gf_window_manager_t *m)
{
    gf_workspace_list_t *workspaces = wm_workspaces (m);
    gf_window_list_t *windows = wm_windows (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    // First pass: preserve existing workspace assignments
    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_window_info_t *win = &windows->items[i];

        if (!win->is_valid)
            continue;

        if (_window_has_valid_workspace (win, workspaces))
        {
            gf_workspace_list_ensure (workspaces, win->workspace_id, max_per_ws);
            continue;
        }
    }

    // Second pass: assign windows without valid workspace
    uint32_t ws_id = 0;
    uint32_t slot = 0;

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_window_info_t *win = &windows->items[i];

        if (!win->is_valid || _window_has_valid_workspace (win, workspaces))
            continue;

        // Find next available unlocked workspace with space
        while (ws_id < workspaces->count
               && (workspaces->items[ws_id].is_locked || slot >= max_per_ws))
        {
            if (slot >= max_per_ws)
            {
                ws_id++;
                slot = 0;
            }
            else
            {
                ws_id++;
            }
        }

        // Create workspace if needed
        gf_workspace_list_ensure (workspaces, ws_id, max_per_ws);

        win->workspace_id = ws_id;
        slot++;

        if (slot >= max_per_ws)
        {
            ws_id++;
            slot = 0;
        }
    }

    // Rebuild workspace stats
    _rebuild_workspace_stats (workspaces, windows, max_per_ws);

    if (workspaces->active_workspace >= workspaces->count)
        workspaces->active_workspace = 0;

    gf_window_manager_sync_workspaces (m);
}

gf_error_code_t
gf_window_manager_arrange_workspace (gf_window_manager_t *m)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_list_t *workspaces = wm_workspaces (m);
    gf_window_list_t *windows = wm_windows (m);

    if (windows->count == 0 || !windows->items)
        return GF_SUCCESS;

    gf_window_manager_assign_workspaces (m);
    gf_window_manager_unmaximize_all (m, windows->items, windows->count);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_workspace_info_t *ws = &workspaces->items[i];

        gf_window_info_t *ws_windows = NULL;
        uint32_t ws_window_count = 0;

        if (gf_window_list_get_by_workspace (windows, ws->id, &ws_windows,
                                             &ws_window_count)
                != GF_SUCCESS
            || ws_window_count == 0)
        {
            continue;
        }

        // Filter out minimized windows for layout calculation
        gf_window_info_t *non_minimized_windows = NULL;
        uint32_t non_minimized_count = 0;

        non_minimized_windows = gf_malloc (ws_window_count * sizeof (gf_window_info_t));
        if (!non_minimized_windows)
        {
            gf_free (ws_windows);
            continue;
        }

        for (uint32_t j = 0; j < ws_window_count; j++)
        {
            if (!ws_windows[j].is_minimized)
            {
                non_minimized_windows[non_minimized_count++] = ws_windows[j];
            }
        }

        // Only calculate layout if there are non-minimized windows
        if (non_minimized_count > 0)
        {
            gf_rect_t workspace_bounds;
            if (wm_platform (m)->get_screen_bounds (*wm_display (m), &workspace_bounds)
                == GF_SUCCESS)
            {
                gf_rect_t *new_geometries = NULL;
                if (gf_window_manager_calculate_layout (
                        m, non_minimized_windows, non_minimized_count, &new_geometries)
                    == GF_SUCCESS)
                {
                    gf_window_manager_apply_layout (m, non_minimized_windows,
                                                    new_geometries, non_minimized_count);
                    gf_free (new_geometries);
                }
            }
        }

        gf_free (non_minimized_windows);
        gf_free (ws_windows);
    }

    return GF_SUCCESS;
}
gf_error_code_t
gf_window_manager_run (gf_window_manager_t *m)
{
    if (!m || !m->state.initialized)
        return GF_ERROR_INVALID_PARAMETER;

    gf_window_list_t *windows = wm_windows (m);
    GF_LOG_INFO ("Window manager main loop started");

    time_t last_stats_time = time (NULL);

    while (true)
    {
        m->state.loop_counter++;
        time_t current_time = time (NULL);

        gf_window_manager_load_cfg (m);
        gf_window_manager_watch (m);
        gf_window_manager_arrange_workspace (m);
        gf_window_manager_arrange_overflow (m);
        gf_window_manager_event (m);

        // Process IPC server requests
        if (m->ipc_handle >= 0)
        {
            gf_ipc_server_process (m->ipc_handle, m);
        }

        if (current_time - m->state.last_cleanup_time >= 1)
        {
            gf_window_manager_cleanup_invalid_data (m);
            m->state.last_cleanup_time = current_time;
        }

        uint32_t sleep_time = windows->count > 10 ? 100000 : 50000;
        usleep (sleep_time);
    }

    return GF_SUCCESS;
}

static void
gf_window_manager_unmaximize_all (gf_window_manager_t *m, gf_window_info_t *windows,
                                  uint32_t window_count)
{
    if (!m || !wm_platform (m) || !windows)
        return;

    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    for (uint32_t i = 0; i < window_count; i++)
    {
        if (windows[i].is_maximized || !wm_is_excluded (m, windows[i].native_handle))
            platform->unmaximize_window (display, windows[i].native_handle);
    }
}

static void
gf_window_manager_apply_layout (gf_window_manager_t *m, gf_window_info_t *windows,
                                gf_rect_t *geometry, uint32_t window_count)
{
    if (!m || !windows || !geometry || window_count == 0)
        return;

    gf_window_list_t *window_list = wm_windows (m);
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    for (uint32_t i = 0; i < window_count; i++)
    {
        if (windows[i].is_minimized || !windows[i].needs_update && !windows[i].is_valid)
            continue;

        gf_error_code_t result = platform->set_window_geometry (
            display, windows[i].native_handle, &geometry[i],
            GF_GEOMETRY_CHANGE_ALL | GF_GEOMETRY_APPLY_PADDING, m->config);

        if (result != GF_SUCCESS)
        {
            GF_LOG_WARN ("Failed to set geometry for window %llu",
                         (unsigned long long)windows[i].id);
        }

        gf_window_manager_update_window_info (m, windows[i].native_handle,
                                              windows[i].workspace_id);
        gf_window_list_clear_update_flags (window_list, windows[i].workspace_id);
    }
}

gf_error_code_t
gf_window_manager_swap (gf_window_manager_t *m, const gf_window_info_t *src_copy,
                        const gf_window_info_t *dst_copy)
{
    if (!m || !src_copy || !dst_copy)
        return GF_ERROR_INVALID_PARAMETER;

    gf_window_list_t *list = wm_windows (m);
    int src_idx = -1, dst_idx = -1;

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

    gf_window_info_t temp = list->items[src_idx];
    list->items[src_idx] = list->items[dst_idx];
    list->items[dst_idx] = temp;

    gf_rect_t *new_geometries = NULL;
    gf_error_code_t result = gf_window_manager_calculate_layout (
        m, list->items, list->count, &new_geometries);
    if (result != GF_SUCCESS)
        return result;

    gf_window_manager_unmaximize_all (m, list->items, list->count);
    gf_window_manager_apply_layout (m, list->items, new_geometries, list->count);

    gf_free (new_geometries);
    return GF_SUCCESS;
}

static gf_error_code_t
gf_window_manager_arrange_overflow (gf_window_manager_t *m)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_list_t *workspaces = wm_workspaces (m);
    gf_window_list_t *windows = wm_windows (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_workspace_info_t *src_ws = &workspaces->items[i];

        if (src_ws->window_count <= max_per_ws)
            continue;

        uint32_t overflow = src_ws->window_count - max_per_ws;

        for (uint32_t j = 0; j < overflow; j++)
        {
            gf_workspace_id_t dst_id
                = gf_workspace_list_find_free (workspaces, max_per_ws);

            if (dst_id < 0)
            {
                GF_LOG_ERROR ("Failed to find free workspace for overflow");
                break;
            }

            gf_window_info_t *list = NULL;
            uint32_t count = 0;

            if (gf_window_list_get_by_workspace (windows, src_ws->id, &list, &count)
                    != GF_SUCCESS
                || count == 0)
            {
                gf_free (list);
                break;
            }

            gf_window_info_t *win = &list[0];

            for (uint32_t w = 0; w < windows->count; w++)
            {
                if (wm_is_excluded (m, win->native_handle))
                    continue;

                if (windows->items[w].id == win->id)
                {
                    GF_LOG_INFO ("Move window %u from workspace %u to workspace %u",
                                 windows->items[w].id, windows->items[w].workspace_id,
                                 dst_id);
                    windows->items[w].workspace_id = dst_id;

                    workspaces->items[dst_id].window_count++;
                    workspaces->items[dst_id].available_space--;
                    src_ws->window_count--;
                    src_ws->available_space++;

                    break;
                }
            }

            gf_free (list);
        }
    }

    return GF_SUCCESS;
}

static void
gf_window_manager_watch (gf_window_manager_t *m)
{
    if (!m)
        return;

    gf_workspace_list_t *workspaces = wm_workspaces (m);
    gf_window_list_t *windows = wm_windows (m);
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    gf_window_manager_sync_workspaces (m);

    /* Frequent cleanup of invalid/excluded/hidden windows to ensure borders are removed
     * immediately */
    for (uint32_t i = 0; i < windows->count;)
    {
        gf_window_info_t *win = &windows->items[i];
        bool excluded = wm_is_excluded (m, win->native_handle);
        bool invalid = !wm_is_valid (m, win->native_handle);
        bool hidden = false;
        if (m->platform->is_window_hidden)
            hidden = m->platform->is_window_hidden (m->display, win->native_handle);

        if (excluded || invalid || hidden)
        {
            if (m->platform->remove_border)
                m->platform->remove_border (m->platform, win->native_handle);
            gf_window_list_remove (windows, win->id);
            continue;
        }
        i++;
    }

    /* Game Focus Mode: Minimize other windows if a strictly fullscreen app/game is
     * focused */
    gf_window_id_t active_win_id = m->platform->get_active_window (m->display);
    if (active_win_id != 0
        && m->platform->is_fullscreen (m->display, (gf_native_window_t)active_win_id))
    {
        for (uint32_t i = 0; i < windows->count; i++)
        {
            m->platform->minimize_window (m->display, windows->items[i].native_handle);
            windows->items[i].is_minimized = true;
        }
    }

    for (uint32_t ws_id = 0; ws_id < workspaces->count; ws_id++)
    {
        gf_window_info_t *platform_windows = NULL;
        uint32_t count = 0;

        if (platform->get_windows (display, (gf_workspace_id_t *)&ws_id,
                                   &platform_windows, &count)
            != GF_SUCCESS)
            continue;

        for (uint32_t i = 0; i < count; i++)
        {
            gf_window_info_t *existing
                = gf_window_list_find_by_window_id (windows, platform_windows[i].id);

            if (!existing && platform_windows[i].is_valid
                && !wm_is_excluded (m, platform_windows[i].native_handle))
            {
                // New window detected!
                gf_workspace_id_t assigned_ws = gf_workspace_list_find_free (
                    workspaces, m->config->max_windows_per_workspace);

                platform_windows[i].workspace_id = assigned_ws;
                platform_windows[i].is_minimized = false;

                gf_window_list_add (windows, &platform_windows[i]);

                char win_name[256] = { 0 };
                gf_window_manager_get_window_name (m, platform_windows[i].native_handle,
                                                   win_name, sizeof (win_name));
                GF_LOG_INFO ("New window detected: win=%lu | WS=%u | Name='%s'",
                             platform_windows[i].id, assigned_ws, win_name);

                // Handle the new window: focus it and minimize others in workspace
                _window_manager_handle_new_window (m, &platform_windows[i], assigned_ws);
            }
        }

        if (m->config->enable_borders && m->platform->update_border)
            m->platform->update_border (m->platform);

        gf_free (platform_windows);
    }
}

void
gf_window_manager_load_cfg (gf_window_manager_t *m)
{
    if (!m->config)
    {
        GF_LOG_ERROR ("Config not initialized in main()");
        return;
    }

    const char *config_file = gf_config_get_path ();
    if (!config_file)
    {
        GF_LOG_ERROR ("Failed to determine config file path");
        return;
    }

    struct stat st;
    if (stat (config_file, &st) != 0)
    {
        GF_LOG_ERROR ("Failed to read stat config file: %s", config_file);
        return;
    }

    if (st.st_mtime <= m->config->last_modified)
        return;

    usleep (10000);

    gf_config_t old_config = *m->config;
    gf_config_t new_config = load_or_create_config (config_file);

    if (gf_config_has_changed (&old_config, &new_config))
    {
        GF_LOG_INFO ("Configuration changed! Reloading from: %s", config_file);

        *m->config = new_config;
        m->config->last_modified = st.st_mtime;

        if (m->platform->set_border_color)
            m->platform->set_border_color (m->platform, m->config->border_color);

        if (old_config.enable_borders != new_config.enable_borders)
        {
            if (!new_config.enable_borders)
            {
                GF_LOG_INFO ("Borders disabled, cleaning up...");
                if (m->platform->cleanup_borders)
                    m->platform->cleanup_borders (m->platform);
            }
            else
            {
                GF_LOG_INFO ("Borders enabled, adding to all valid windows...");
                
                // Get all windows from all workspaces to ensure we don't miss any
                for (gf_workspace_id_t workspace = 0; workspace < GF_MAX_WORKSPACES; workspace++)
                {
                    gf_window_info_t *workspace_windows = NULL;
                    uint32_t count = 0;
                    
                    if (m->platform->get_windows (m->display, &workspace, &workspace_windows, &count) != GF_SUCCESS)
                    {
                        GF_LOG_DEBUG ("Failed to get windows for workspace %d", workspace);
                        continue;
                    }
                        
                    GF_LOG_DEBUG ("Processing workspace %d with %d windows", workspace, count);
                    
                    for (uint32_t i = 0; i < count; i++)
                    {
                        gf_window_info_t *win = &workspace_windows[i];
                        
                        // Check if window is valid and not excluded
                        if (win->is_valid && !win->is_minimized
                            && !wm_is_excluded (m, win->native_handle))
                        {
                            GF_LOG_DEBUG ("Adding border to window %lu in workspace %d", 
                                         (unsigned long)win->id, workspace);
                            
                            if (m->platform->add_border)
                                m->platform->add_border (m->platform, win->native_handle,
                                                         m->config->border_color, 3);
                        }
                        else
                        {
                            GF_LOG_DEBUG ("Skipping window %lu (valid=%d, minimized=%d, excluded=%d)", 
                                         (unsigned long)win->id, win->is_valid, win->is_minimized,
                                         wm_is_excluded (m, win->native_handle));
                        }
                    }
                    
                    // Free the window list for this workspace
                    if (workspace_windows)
                        gf_free (workspace_windows);
                }
                
                // Also check the current workspace windows list as a fallback
                gf_window_list_t *current_windows = wm_windows (m);
                GF_LOG_DEBUG ("Current workspace has %d additional windows", current_windows->count);
                for (uint32_t i = 0; i < current_windows->count; i++)
                {
                    gf_window_info_t *win = &current_windows->items[i];
                    
                    if (win->is_valid && !win->is_minimized
                        && !wm_is_excluded (m, win->native_handle))
                    {
                        // Check if border already exists to avoid duplicates
                        bool has_border = false;
                        // This check will be handled by add_border function now
                        
                        if (m->platform->add_border)
                            m->platform->add_border (m->platform, win->native_handle,
                                                     m->config->border_color, 3);
                    }
                }
            }
        }

        gf_window_manager_sync_workspaces (m);
        gf_window_manager_print_stats (m);
    }
    else
    {
        m->config->last_modified = st.st_mtime;
    }
}

void
gf_window_manager_event (gf_window_manager_t *m)
{
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_window_list_t *windows = wm_windows (m);
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    gf_window_id_t curr_win_id = platform->get_active_window (display);
    if (curr_win_id == 0)
        return;

    gf_window_info_t *focused = gf_window_list_find_by_window_id (windows, curr_win_id);

    // Handle unfocused/new window
    if (!focused)
    {
        _handle_new_focused_window (m, curr_win_id, workspaces);
        return;
    }

    gf_workspace_id_t current_workspace = focused->workspace_id;

    _detect_minimization_changes (m, current_workspace);

    // Handle workspace switch if needed
    bool workspace_changed = (m->state.last_active_workspace != current_workspace);
    bool has_previous_window = (m->state.last_active_window != 0);

    if (workspace_changed && has_previous_window)
    {
        _handle_workspace_switch (m, current_workspace);
    }

    // Update state
    m->state.last_active_window = curr_win_id;
    m->state.last_active_workspace = current_workspace;
}

gf_error_code_t
gf_window_manager_move_window (gf_window_manager_t *m, gf_window_id_t window_id,
                               gf_workspace_id_t target_workspace)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_window_list_t *windows = wm_windows (m);
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    // Find window
    gf_window_info_t *win = NULL;
    for (uint32_t i = 0; i < windows->count; i++)
    {
        if (windows->items[i].id == window_id)
        {
            win = &windows->items[i];
            break;
        }
    }

    if (!win)
        return GF_ERROR_INVALID_PARAMETER;

    if (target_workspace < 0 || target_workspace >= m->config->max_workspaces)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_list_ensure (workspaces, target_workspace,
                              m->config->max_windows_per_workspace);

    gf_workspace_info_t *target_ws = &workspaces->items[target_workspace];

    if (target_ws->is_locked)
        return GF_ERROR_WORKSPACE_LOCKED;

    if (target_ws->window_count >= m->config->max_windows_per_workspace)
        return GF_ERROR_WORKSPACE_FULL;

    win->workspace_id = target_workspace;

    _rebuild_workspace_stats (workspaces, windows, m->config->max_windows_per_workspace);
    gf_window_manager_sync_workspaces (m);

    return GF_SUCCESS;
}

gf_error_code_t
gf_window_manager_lock_workspace (gf_window_manager_t *m, gf_workspace_id_t workspace_id)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_list_t *workspaces = wm_workspaces (m);

    if (workspace_id < 0 || workspace_id >= m->config->max_workspaces)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_list_ensure (workspaces, workspace_id,
                              m->config->max_windows_per_workspace);

    gf_workspace_info_t *ws = &workspaces->items[workspace_id];

    if (ws->is_locked)
        return GF_ERROR_ALREADY_LOCKED;

    ws->is_locked = true;

    gf_config_add_locked_workspace (m->config, workspace_id);

    _rebuild_workspace_stats (workspaces, wm_windows (m),
                              m->config->max_windows_per_workspace);
    gf_window_manager_sync_workspaces (m);

    return GF_SUCCESS;
}

gf_error_code_t
gf_window_manager_unlock_workspace (gf_window_manager_t *m,
                                    gf_workspace_id_t workspace_id)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_list_t *workspaces = wm_workspaces (m);

    if (workspace_id < 0 || workspace_id >= m->config->max_workspaces)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_list_ensure (workspaces, workspace_id,
                              m->config->max_windows_per_workspace);

    gf_workspace_info_t *ws = &workspaces->items[workspace_id];

    if (!ws->is_locked)
        return GF_ERROR_ALREADY_UNLOCKED;

    ws->is_locked = false;

    gf_config_remove_locked_workspace (m->config, workspace_id);

    _rebuild_workspace_stats (workspaces, wm_windows (m),
                              m->config->max_windows_per_workspace);
    gf_window_manager_sync_workspaces (m);

    return GF_SUCCESS;
}
