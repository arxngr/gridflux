#include "window_manager.h"
#include "config.h"
#include "internal.h"
#include "layout.h"
#include "list.h"
#include "logger.h"
#include "memory.h"
#include "types.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static gf_workspace_info_t *
_get_workspace (gf_workspace_list_t *workspaces, gf_workspace_id_t id)
{
    if (!workspaces || id < GF_FIRST_WORKSPACE_ID)
        return NULL;
    return gf_workspace_list_find_by_id (workspaces, id);
}

void
_cleanup_unused_workspace (gf_workspace_list_t *list, uint32_t index)
{
    if (!list || index >= list->count)
        return;

    memmove (&list->items[index], &list->items[index + 1],
             (list->count - index - 1) * sizeof (gf_workspace_info_t));

    list->count--;
    memset (&list->items[list->count], 0, sizeof (gf_workspace_info_t));
}

void
_remove_stale_windows (gf_window_manager_t *m, gf_window_list_t *windows)
{
    uint32_t removed_windows = 0;
    gf_workspace_list_t *workspaces = wm_workspaces (m);

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
            // If this was a maximized window, clear the workspace's maximized state
            if (win->is_maximized)
            {
                gf_workspace_info_t *ws
                    = gf_workspace_list_find_by_id (workspaces, win->workspace_id);
                if (ws && ws->has_maximized_state)
                {
                    ws->has_maximized_state = false;
                    ws->max_windows = m->config->max_windows_per_workspace;
                    ws->available_space = m->config->max_windows_per_workspace;
                    GF_LOG_DEBUG ("Cleared maximized state from workspace %d", ws->id);
                }
            }

            gf_window_list_remove (windows, win->id);
            m->platform->remove_border (m->platform, win->native_handle);
            removed_windows++;
            continue;
        }

        i++;
    }
    if (removed_windows > 0)
        GF_LOG_DEBUG ("Cleaned %u invalid/excluded windows", removed_windows);
}

void
_remove_stale_workspaces (gf_window_manager_t *m, gf_workspace_list_t *workspaces,
                          gf_window_list_t *windows)
{
    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        workspaces->items[i].window_count
            = gf_window_list_count_by_workspace (windows, workspaces->items[i].id);
    }

    int *ws_id_to_clean = NULL;
    size_t count = 0;

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_workspace_info_t *ws = &workspaces->items[i];

        bool is_active = (ws->id == workspaces->active_workspace);
        bool is_empty = (ws->window_count == 0);

        if (is_empty && !is_active)
        {
            int *tmp = realloc (ws_id_to_clean, (count + 1) * sizeof (int));
            if (!tmp)
                break;

            ws_id_to_clean = tmp;
            ws_id_to_clean[count++] = i;
        }
    }

    if (count - workspaces->count == 1)
        return;

    for (uint32_t i = 0; i < count; i++)
        _cleanup_unused_workspace (workspaces, i);

    free (ws_id_to_clean);
}

static bool
_workspace_is_valid (gf_workspace_list_t *workspaces, gf_workspace_id_t id)
{
    return _get_workspace (workspaces, id) != NULL;
}

static void
_move_window_between_workspaces (gf_window_manager_t *m, gf_window_info_t *win,
                                 gf_workspace_id_t new_ws_id)
{
    gf_workspace_list_t *workspaces = wm_workspaces (m);
    gf_window_list_t *windows = wm_windows (m);

    gf_workspace_info_t *old
        = gf_workspace_list_find_by_id (workspaces, win->workspace_id);
    gf_workspace_info_t *new = gf_workspace_list_find_by_id (workspaces, new_ws_id);

    if (!old || !new || old == new)
        return;

    gf_workspace_list_remove_window (old, windows, win->id);
    gf_workspace_list_add_window (new, windows, win->id);

    win->workspace_id = new_ws_id;
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
            gf_workspace_info_t *ws = gf_workspace_list_find_by_id (workspaces, ws_id);
            if (ws)
            {
                ws->window_count++;
                ws->available_space--;
            }
        }
    }
}

static bool
_window_has_valid_workspace (gf_window_info_t *win, gf_workspace_list_t *workspaces)
{
    return win->workspace_id >= GF_FIRST_WORKSPACE_ID
           && _workspace_is_valid (workspaces, win->workspace_id);
}

void
gf_window_manager_get_window_name (const gf_window_manager_t *m,
                                   gf_native_window_t handle, char *buffer, size_t size)
{
    if (m->platform && m->platform->get_window_name_info)
    {
        m->platform->get_window_name_info (m->display, handle, buffer, size);
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

        // Remove border before minimizing
        if (m->config->enable_borders && platform->remove_border)
            platform->remove_border (platform, ws_list[i].native_handle);

        platform->set_minimize_window (display, ws_list[i].native_handle);
        ws_list[i].is_minimized = true;
    }
}

static void
_unminimize_workspace_windows (gf_window_manager_t *m, gf_window_info_t *ws_list,
                               uint32_t count, gf_native_window_t active_window)
{
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    // First pass: unminimize all non-active windows
    for (uint32_t i = 0; i < count; i++)
    {
        gf_window_info_t *win = &ws_list[i];

        if (wm_is_excluded (m, win->native_handle))
            continue;

        if (platform->is_window_hidden
            && platform->is_window_hidden (display, win->native_handle))
            continue;

        // Skip active window for now
        if (active_window != 0 && win->native_handle == active_window)
            continue;

        platform->set_unminimize_window (display, win->native_handle);
        win->is_minimized = false;

        gf_window_info_t *actual
            = gf_window_list_find_by_window_id (wm_windows (m), win->id);
        if (actual)
            actual->is_minimized = false;

        if (m->config->enable_borders && !win->is_maximized && platform->create_border)
        {
            platform->create_border (platform, win->native_handle,
                                     m->config->border_color, 3);
        }
    }

    // Second pass: unminimize active window last (to ensure it's on top)
    if (active_window != 0)
    {
        for (uint32_t i = 0; i < count; i++)
        {
            gf_window_info_t *win = &ws_list[i];

            if (win->native_handle == active_window)
            {
                platform->set_unminimize_window (display, win->native_handle);
                win->is_minimized = false;

                gf_window_info_t *actual
                    = gf_window_list_find_by_window_id (wm_windows (m), win->id);
                if (actual)
                    actual->is_minimized = false;

                if (m->config->enable_borders && !win->is_maximized && platform->create_border)
                {
                    platform->create_border (platform, win->native_handle,
                                             m->config->border_color, 3);
                }
                break;
            }
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
    {
        GF_LOG_DEBUG ("Fail to get window list by workspace num: %u", current_workspace);
        return;
    }

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

    gf_platform_interface_t *platform = wm_platform (m);

    // Get current active window to preserve focus
    gf_native_window_t active_window = 0;
    if (platform->get_active_window)
        active_window = platform->get_active_window (*wm_display(m));

    // Unminimize windows in current workspace
    _unminimize_workspace_windows (m, workspace_windows, workspace_win_count, active_window);
    gf_free (workspace_windows);

    // Toggle dock based on target workspace type
    gf_workspace_info_t *target_ws
        = gf_workspace_list_find_by_id (workspaces, current_workspace);

    if (target_ws && target_ws->has_maximized_state)
    {
        if (!m->state.dock_hidden && platform->set_dock_autohide)
        {
            platform->set_dock_autohide (platform);
            m->state.dock_hidden = true;
        }
    }
    else
    {
        if (m->state.dock_hidden && platform->restore_dock)
        {
            platform->restore_dock (platform);
            m->state.dock_hidden = false;
        }
    }
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

    if (platform->gesture_init)
    {
        if (platform->gesture_init (platform, *wm_display (m)) == GF_SUCCESS)
        {
            m->state.gesture_initialized = true;
            GF_LOG_INFO ("Gesture support enabled");
        }
        else
        {
            GF_LOG_WARN ("Gesture support not available");
        }
    }

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

    // Clear all windows and workspaces from memory
    gf_window_list_t *windows = wm_windows (m);
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    GF_LOG_INFO ("Clearing %u windows and %u workspaces from memory", windows->count,
                 workspaces->count);

    // Reset lists (items still allocated, will be freed in destroy)
    windows->count = 0;
    workspaces->count = 0;
    workspaces->active_workspace = 0;

    // Reset state
    m->state.last_active_window = 0;
    m->state.last_active_workspace = 0;

    // Cleanup gestures
    if (m->state.gesture_initialized && platform->gesture_cleanup)
    {
        platform->gesture_cleanup (platform);
        m->state.gesture_initialized = false;
    }

    // Restore dock if it was hidden (critical for program termination)
    if (m->state.dock_hidden && platform->restore_dock)
    {
        platform->restore_dock (platform);
        m->state.dock_hidden = false;
        GF_LOG_INFO ("Dock restored during cleanup");
    }

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

    _remove_stale_windows (m, windows);
    _remove_stale_workspaces (m, workspaces, windows);
}

static void
_sync_workspaces (gf_window_manager_t *m)
{
    gf_workspace_list_t *workspaces = wm_workspaces (m);
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    uint32_t platform_count = platform->get_workspace_count (display);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    for (uint32_t i = GF_FIRST_WORKSPACE_ID; i <= platform_count; i++)
    {
        gf_workspace_list_ensure (workspaces, i, max_per_ws);
        gf_workspace_info_t *ws = gf_workspace_list_find_by_id (workspaces, i);
        if (!ws)
            continue;

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
        if (ws >= GF_FIRST_WORKSPACE_ID
            && ws < m->config->max_workspaces + GF_FIRST_WORKSPACE_ID)
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

    for (gf_workspace_id_t i = GF_FIRST_WORKSPACE_ID; i <= max_workspace; i++)
    {
        uint32_t count = workspace_counts[i];
        uint32_t max_windows = m->config->max_windows_per_workspace;
        int32_t available = -1;
        bool is_locked = gf_config_is_workspace_locked (m->config, i);
        bool has_maximized = false;

        gf_workspace_info_t *ws = _get_workspace ((gf_workspace_list_t *)workspaces, i);
        if (ws)
        {
            max_windows = ws->max_windows;
            available = ws->available_space;
            is_locked = ws->is_locked;
            has_maximized = ws->has_maximized_state;
        }
        else
        {
            // Workspace does not exist yet
            available = is_locked ? 0 : max_windows;
        }

        /* ---- Workspace header ---- */
        GF_LOG_INFO ("WS %d %s %s  %u/%u  free=%d", i, is_locked ? "[LOCKED]" : "",
                     has_maximized ? "[MAX]" : "", count, max_windows, available);

        /* ---- Windows in this workspace ---- */
        for (uint32_t w = 0; w < windows->count; w++)
        {
            const gf_window_info_t *win = &windows->items[w];

            if (win->workspace_id != i)
                continue;

            gf_window_manager_get_window_name (m, win->native_handle, win_name,
                                               sizeof (win_name));

            GF_LOG_INFO ("   0x%lx  %s", (unsigned long)win->id, win_name);
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
_build_workspace_candidate (gf_window_manager_t *m)
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
    uint32_t ws_id = GF_FIRST_WORKSPACE_ID;
    uint32_t slot = 0;

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_window_info_t *win = &windows->items[i];

        if (!win->is_valid || _window_has_valid_workspace (win, workspaces))
            continue;

        // Find next available unlocked workspace with space
        while (ws_id < workspaces->count)
        {
            gf_workspace_info_t *check_ws
                = gf_workspace_list_find_by_id (workspaces, ws_id);
            if (!check_ws || check_ws->is_locked || slot >= max_per_ws)
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
            else
            {
                break;
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
        workspaces->active_workspace = GF_FIRST_WORKSPACE_ID;

    _sync_workspaces (m);
}

gf_error_code_t
gf_window_manager_arrange_workspace (gf_window_manager_t *m)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_list_t *workspaces = wm_workspaces (m);
    gf_window_list_t *windows = wm_windows (m);
    gf_platform_interface_t *platform = wm_platform (m);

    if (windows->count == 0 || !windows->items)
        return GF_SUCCESS;

    _build_workspace_candidate (m);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_workspace_info_t *ws = &workspaces->items[i];

        if (ws->has_maximized_state)
            continue;

        gf_window_info_t *ws_windows = NULL;
        uint32_t ws_window_count = 0;

        if (gf_window_list_get_by_workspace (windows, ws->id, &ws_windows,
                                             &ws_window_count)
                != GF_SUCCESS
            || ws_window_count == 0)
        {
            continue;
        }

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
                    if (m->config->enable_borders && platform->update_border)
                        platform->update_border (platform);

                    gf_free (new_geometries);
                }
            }
        }

        gf_free (non_minimized_windows);
        gf_free (ws_windows);
    }

    return GF_SUCCESS;
}

static uint32_t
_get_maximized_windows (gf_window_manager_t *m, gf_window_info_t **out_windows)
{
    gf_window_list_t *windows = wm_windows (m);
    gf_workspace_list_t *workspaces = wm_workspaces (m);
    uint32_t total = 0;

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        if (!workspaces->items[i].has_maximized_state)
            continue;

        total += gf_window_list_count_by_workspace (windows, workspaces->items[i].id);
    }

    if (total == 0 || !out_windows)
    {
        if (out_windows)
            *out_windows = NULL;
        return 0;
    }

    gf_window_info_t *result = gf_malloc (total * sizeof (gf_window_info_t));
    if (!result)
    {
        *out_windows = NULL;
        return 0;
    }

    uint32_t idx = 0;
    for (uint32_t i = 0; i < workspaces->count && idx < total; i++)
    {
        if (!workspaces->items[i].has_maximized_state)
            continue;

        gf_window_info_t *ws_wins = NULL;
        uint32_t ws_count = 0;
        if (gf_window_list_get_by_workspace (windows, workspaces->items[i].id, &ws_wins,
                                             &ws_count)
            == GF_SUCCESS)
        {
            for (uint32_t j = 0; j < ws_count && idx < total; j++)
            {
                result[idx++] = ws_wins[j];
            }
            gf_free (ws_wins);
        }
    }

    *out_windows = result;
    return idx;
}

static int
_find_maximized_index (gf_window_info_t *windows, uint32_t count,
                       gf_native_window_t handle)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (windows[i].native_handle == handle)
            return (int)i;
    }
    return -1;
}

#define GF_SWIPE_THRESHOLD_PX 200.0

static void
gf_window_manager_gesture_event (gf_window_manager_t *m)
{
    gf_platform_interface_t *platform = wm_platform (m);

    if (!m->state.gesture_initialized || !platform->gesture_poll)
        return;

    gf_gesture_event_t gev;

    while (platform->gesture_poll (platform, *wm_display (m), &gev))
    {
        if (gev.fingers != 3)
            continue;

        gf_display_t display = *wm_display (m);

        if (gev.type == GF_GESTURE_SWIPE_END)
        {
            if (gev.total_dx > GF_SWIPE_THRESHOLD_PX
                || gev.total_dx < -GF_SWIPE_THRESHOLD_PX)
            {
                bool swipe_left = (gev.total_dx < 0);

                gf_window_id_t active = 0;
                if (platform->get_active_window)
                    active = platform->get_active_window (display);

                gf_window_info_t *max_wins = NULL;
                uint32_t max_count = _get_maximized_windows (m, &max_wins);

                if (max_count >= 2 && max_wins)
                {
                    int current_idx = _find_maximized_index (max_wins, max_count, active);

                    if (current_idx >= 0)
                    {
                        int next_idx = swipe_left ? (current_idx + 1) % (int)max_count
                                                  : (current_idx - 1 + (int)max_count)
                                                        % (int)max_count;

                        gf_native_window_t next_win = max_wins[next_idx].native_handle;

                        if (platform->set_minimize_window)
                            platform->set_minimize_window (display, active);
                        if (platform->set_unminimize_window)
                            platform->set_unminimize_window (display, next_win);

                        GF_LOG_INFO ("Gesture swipe %s: switched to window %lu",
                                     swipe_left ? "left" : "right",
                                     (unsigned long)next_win);
                    }
                }
                gf_free (max_wins);
            }
            else
            {
                GF_LOG_DEBUG ("Gesture swipe too short (%.1f px), ignoring",
                              gev.total_dx);
            }
        }
    }
}

gf_error_code_t
gf_window_manager_run (gf_window_manager_t *m)
{
    if (!m || !m->state.initialized)
        return GF_ERROR_INVALID_PARAMETER;

    time_t last_stats_time = time (NULL);

    while (true)
    {
        m->state.loop_counter++;
        time_t current_time = time (NULL);

        gf_window_manager_load_cfg (m);
        gf_window_manager_watch (m);

        gf_window_manager_gesture_event (m);

        gf_window_manager_arrange_workspace (m);
        gf_window_manager_arrange_overflow (m);
        gf_window_manager_event (m);

        if (m->ipc_handle >= 0)
        {
            gf_ipc_server_process (m->ipc_handle, m);
        }

        if (current_time - m->state.last_cleanup_time >= 1)
        {
            gf_window_manager_cleanup_invalid_data (m);
            m->state.last_cleanup_time = current_time;
        }

        usleep (33000);
    }

    return GF_SUCCESS;
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
            gf_workspace_id_t dst_id = -1;
            gf_workspace_info_t *active_ws_info
                = gf_workspace_list_find_by_id (workspaces, workspaces->active_workspace);

            if (active_ws_info->available_space == 0)
            {
                dst_id = gf_workspace_create (workspaces, max_per_ws, false);
            }
            else
            {
                dst_id = active_ws_info->id;
            }
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

            if (src_ws->id == dst_id) continue;
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
_handle_fullscreen_windows (gf_window_manager_t *m)
{
    gf_workspace_list_t *workspaces = wm_workspaces (m);
    gf_window_list_t *windows = wm_windows (m);
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_window_id_t active_win_id = m->platform->get_active_window (m->display);

    if (active_win_id != 0
        && m->platform->is_window_fullscreen (m->display,
                                              (gf_native_window_t)active_win_id))
    {
        for (uint32_t i = 0; i < windows->count; i++)
        {
            m->platform->set_minimize_window (m->display,
                                              windows->items[i].native_handle);
            windows->items[i].is_minimized = true;
        }
    }
}

static gf_workspace_id_t
_find_or_create_maximized_ws (gf_window_manager_t *m)
{
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        if (workspaces->items[i].has_maximized_state)
        {
            return workspaces->items[i].id;
        }
    }

    return gf_workspace_create (workspaces, m->config->max_windows_per_workspace, true);
}

static gf_workspace_id_t
_find_or_create_ws (gf_window_manager_t *m)
{
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        if (workspaces->items[i].available_space > 0)
        {
            return workspaces->items[i].id;
        }
    }

    return gf_workspace_create (workspaces, m->config->max_windows_per_workspace, false);
}

static gf_workspace_id_t
_assign_workspace_for_window (gf_window_manager_t *m, gf_window_info_t *win,
                              gf_workspace_info_t *current_ws)
{
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    if (current_ws && current_ws->available_space > 0)
        return current_ws->id;

    if (platform->is_window_maximized
        && platform->is_window_maximized (display, win->native_handle))
    {
        win->is_maximized = true;
        return _find_or_create_maximized_ws (m);
    }

    return _find_or_create_ws (m);
}

static void
_handle_new_window (gf_window_manager_t *m, gf_window_info_t *win,
                    gf_workspace_info_t *current_ws)
{
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_window_list_t *windows = wm_windows (m);
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    win->workspace_id = _assign_workspace_for_window (m, win, current_ws);
    win->is_minimized = false;

    gf_window_list_add (windows, win);

    platform->set_unminimize_window (display, win->native_handle);

    m->state.last_active_window = win->id;
    m->state.last_active_workspace = win->workspace_id;
    workspaces->active_workspace = win->workspace_id;

    if (m->config->enable_borders && platform->create_border)
        platform->create_border (platform, win->native_handle, m->config->border_color,
                                 3);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_workspace_id_t ws_id = workspaces->items[i].id;
        if (ws_id == win->workspace_id)
            continue;

        gf_window_info_t *list = NULL;
        uint32_t count = 0;

        if (gf_window_list_get_by_workspace (windows, ws_id, &list, &count) == GF_SUCCESS)
        {
            _minimize_workspace_windows (m, list, count);
            gf_free (list);
        }
    }

    char name[256];
    gf_window_manager_get_window_name (m, win->native_handle, name, sizeof (name));
    GF_LOG_INFO ("New window %lu → workspace %u (%s)", win->id, win->workspace_id, name);
}

void
gf_window_manager_watch (gf_window_manager_t *m)
{
    if (!m)
        return;

    gf_platform_interface_t *platform = wm_platform (m);
    gf_workspace_list_t *workspaces = wm_workspaces (m);
    gf_window_list_t *windows = wm_windows (m);
    gf_display_t display = *wm_display (m);

    _sync_workspaces (m);
    _handle_fullscreen_windows (m);

    gf_workspace_info_t *current_ws
        = gf_workspace_list_find_by_id (workspaces, workspaces->active_workspace);

    for (uint32_t wsi = 0; wsi < workspaces->count; wsi++)
    {
        gf_workspace_id_t ws_id = workspaces->items[wsi].id - GF_FIRST_WORKSPACE_ID;

        gf_window_info_t *ws_windows = NULL;
        uint32_t ws_count = 0;

        if (!platform->get_windows
            || platform->get_windows (display, &ws_id, &ws_windows, &ws_count)
                   != GF_SUCCESS)
            continue;

        for (uint32_t i = 0; i < ws_count; i++)
        {
            gf_window_info_t *win = &ws_windows[i];

            if (!win->is_valid || wm_is_excluded (m, win->native_handle))
                continue;

            gf_window_info_t *existing
                = gf_window_list_find_by_window_id (windows, win->id);

            if (!existing)
                _handle_new_window (m, win, current_ws);
            else
            {
                // Preserve internally managed fields that the window manager
                // has set (e.g. via _move_window_between_workspaces for
                // maximized windows). The platform scan returns the raw
                // desktop number which would overwrite our virtual workspace
                // assignment, causing maximized workspace to report 0 windows.
                win->workspace_id = existing->workspace_id;
                win->is_maximized = existing->is_maximized;
                win->is_minimized = existing->is_minimized;
                gf_window_list_update (windows, win);
            }

            if (win->is_maximized && platform->remove_border)
                platform->remove_border (platform, win->native_handle);
        }

        gf_free (ws_windows);
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
                if (m->platform->cleanup_borders)
                    m->platform->cleanup_borders (m->platform);
                // Get all windows from all workspaces to ensure we don't miss any
                for (gf_workspace_id_t workspace = 0; workspace < GF_MAX_WORKSPACES;
                     workspace++)
                {
                    gf_window_info_t *workspace_windows = NULL;
                    uint32_t count = 0;

                    if (m->platform->get_windows (m->display, &workspace,
                                                  &workspace_windows, &count)
                        != GF_SUCCESS)
                    {
                        GF_LOG_DEBUG ("Failed to get windows for workspace %d",
                                      workspace);
                        continue;
                    }

                    GF_LOG_DEBUG ("Processing workspace %d with %d windows", workspace,
                                  count);

                    for (uint32_t i = 0; i < count; i++)
                    {
                        gf_window_info_t *win = &workspace_windows[i];

                        // Check if window is valid and not excluded
                        if (win->is_valid && !win->is_minimized
                            && !wm_is_excluded (m, win->native_handle))
                        {
                            GF_LOG_DEBUG ("Adding border to window %lu in workspace %d",
                                          (unsigned long)win->id, workspace);

                            if (m->platform->create_border)
                                m->platform->create_border (m->platform,
                                                            win->native_handle,
                                                            m->config->border_color, 5);
                        }
                        else
                        {
                            GF_LOG_DEBUG ("Skipping window %lu (valid=%d, minimized=%d, "
                                          "excluded=%d)",
                                          (unsigned long)win->id, win->is_valid,
                                          win->is_minimized,
                                          wm_is_excluded (m, win->native_handle));
                        }
                    }

                    // Free the window list for this workspace
                    if (workspace_windows)
                        gf_free (workspace_windows);
                }

                // Also check the current workspace windows list as a fallback
                gf_window_list_t *current_windows = wm_windows (m);
                GF_LOG_DEBUG ("Current workspace has %d additional windows",
                              current_windows->count);
                for (uint32_t i = 0; i < current_windows->count; i++)
                {
                    gf_window_info_t *win = &current_windows->items[i];

                    if (win->is_valid && !win->is_minimized
                        && !wm_is_excluded (m, win->native_handle))
                    {
                        // Check if border already exists to avoid duplicates
                        bool has_border = false;
                        // This check will be handled by add_border function now

                        if (m->platform->create_border)
                            m->platform->create_border (m->platform, win->native_handle,
                                                        m->config->border_color, 3);
                    }
                }
            }
        }

        _sync_workspaces (m);
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
    {
        GF_LOG_WARN ("[EVENT] No active window");
        return;
    }

    gf_window_info_t *focused = gf_window_list_find_by_window_id (windows, curr_win_id);

    if (!focused)
    {
        GF_LOG_WARN ("[EVENT] Active window %lu not tracked yet", curr_win_id);
        return;
    }

    bool now_maximized = false;
    if (platform->is_window_maximized)
        now_maximized = platform->is_window_maximized (display, curr_win_id);

    bool was_maximized = focused->is_maximized;
    if (now_maximized)
    {
        focused->is_maximized = true;

        gf_workspace_id_t max_ws = _find_or_create_maximized_ws (m);

        _move_window_between_workspaces (m, focused, max_ws);

        // Hide dock when maximizing
        if (!m->state.dock_hidden && platform->set_dock_autohide)
        {
            platform->set_dock_autohide (platform);
            m->state.dock_hidden = true;
        }
    }
    else if (!now_maximized && was_maximized)
    {
        // Clear the maximized state from the old workspace
        gf_workspace_info_t *old_ws
            = gf_workspace_list_find_by_id (workspaces, focused->workspace_id);
        if (old_ws && old_ws->has_maximized_state)
        {
            old_ws->has_maximized_state = false;
            old_ws->max_windows = m->config->max_windows_per_workspace;
            old_ws->available_space = m->config->max_windows_per_workspace;
            GF_LOG_DEBUG (
                "Cleared maximized state from workspace %d (window unmaximized)",
                old_ws->id);
        }

        focused->is_maximized = false;

        gf_workspace_id_t normal_ws = _find_or_create_ws (m);

        _move_window_between_workspaces (m, focused, normal_ws);

        // Restore dock when unmaximizing
        if (m->state.dock_hidden && platform->restore_dock)
        {
            // Only restore if no other maximized windows remain
            gf_workspace_info_t *max_ws_check
                = gf_workspace_list_find_by_id (workspaces, old_ws ? old_ws->id : 0);
            bool has_remaining_max = false;
            if (max_ws_check)
            {
                uint32_t remaining
                    = gf_window_list_count_by_workspace (windows, max_ws_check->id);
                has_remaining_max = (remaining > 0);
            }
            if (!has_remaining_max)
            {
                platform->restore_dock (platform);
                m->state.dock_hidden = false;
            }
        }
    }

    gf_workspace_id_t current_workspace = focused->workspace_id;

    _detect_minimization_changes (m, current_workspace);

    bool workspace_changed = (m->state.last_active_workspace != current_workspace);
    bool has_previous_window = (m->state.last_active_window != 0);

    if (workspace_changed && has_previous_window)
    {
        _handle_workspace_switch (m, current_workspace);
    }

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

    if (target_workspace < GF_FIRST_WORKSPACE_ID
        || target_workspace >= m->config->max_workspaces + GF_FIRST_WORKSPACE_ID)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_list_ensure (workspaces, target_workspace,
                              m->config->max_windows_per_workspace);

    gf_workspace_info_t *target_ws
        = gf_workspace_list_find_by_id (workspaces, target_workspace);

    if (target_ws->is_locked)
        return GF_ERROR_WORKSPACE_LOCKED;

    if (target_ws->window_count >= m->config->max_windows_per_workspace)
        return GF_ERROR_WORKSPACE_FULL;

    win->workspace_id = target_workspace;

    _rebuild_workspace_stats (workspaces, windows, m->config->max_windows_per_workspace);
    _sync_workspaces (m);

    return GF_SUCCESS;
}

gf_error_code_t
gf_window_manager_lock_workspace (gf_window_manager_t *m, gf_workspace_id_t workspace_id)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_list_t *workspaces = wm_workspaces (m);

    if (workspace_id < GF_FIRST_WORKSPACE_ID
        || workspace_id >= m->config->max_workspaces + GF_FIRST_WORKSPACE_ID)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_list_ensure (workspaces, workspace_id,
                              m->config->max_windows_per_workspace);

    gf_workspace_info_t *ws = gf_workspace_list_find_by_id (workspaces, workspace_id);

    if (ws->is_locked)
        return GF_ERROR_ALREADY_LOCKED;

    ws->is_locked = true;

    gf_config_add_locked_workspace (m->config, workspace_id);

    _rebuild_workspace_stats (workspaces, wm_windows (m),
                              m->config->max_windows_per_workspace);
    _sync_workspaces (m);

    return GF_SUCCESS;
}

gf_error_code_t
gf_window_manager_unlock_workspace (gf_window_manager_t *m,
                                    gf_workspace_id_t workspace_id)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_list_t *workspaces = wm_workspaces (m);

    if (workspace_id < GF_FIRST_WORKSPACE_ID
        || workspace_id >= m->config->max_workspaces + GF_FIRST_WORKSPACE_ID)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_list_ensure (workspaces, workspace_id,
                              m->config->max_windows_per_workspace);

    gf_workspace_info_t *ws = gf_workspace_list_find_by_id (workspaces, workspace_id);

    if (!ws->is_locked)
        return GF_ERROR_ALREADY_UNLOCKED;

    ws->is_locked = false;

    gf_config_remove_locked_workspace (m->config, workspace_id);

    _rebuild_workspace_stats (workspaces, wm_windows (m),
                              m->config->max_windows_per_workspace);
    _sync_workspaces (m);

    return GF_SUCCESS;
}
