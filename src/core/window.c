#include "../config/config.h"
#include "../platform/platform_compat.h"
#include "../utils/list.h"
#include "../utils/logger.h"
#include "../utils/memory.h"
#include "internal.h"
#include "layout.h"
#include "types.h"
#include "wm.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void
_remove_stale_windows (gf_wm_t *m, gf_win_list_t *windows)
{
    uint32_t removed_windows = 0;
    gf_ws_list_t *workspaces = wm_workspaces (m);

    for (uint32_t i = 0; i < windows->count;)
    {
        gf_win_info_t *win = &windows->items[i];

        bool excluded = wm_is_excluded (m, win->id);
        bool invalid = !wm_is_valid (m, win->id);

        bool hidden = false;
        if (m->platform->window_is_hidden)
        {
            hidden = m->platform->window_is_hidden (m->display, win->id);
        }

        if (excluded || invalid || hidden)
        {
            // If this was a maximized window, clear the workspace's maximized state
            if (win->is_maximized)
            {
                gf_ws_info_t *ws
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
            m->platform->border_remove (m->platform, win->id);
            removed_windows++;
            continue;
        }

        i++;
    }
    if (removed_windows > 0)
        GF_LOG_DEBUG ("Cleaned %u invalid/excluded windows", removed_windows);
}

void
gf_wm_window_class (const gf_wm_t *m, gf_handle_t handle, char *buffer, size_t size)
{
    if (m->platform && m->platform->window_get_class)
    {
        m->platform->window_get_class (m->display, handle, buffer, size);
    }
    else
    {
        snprintf (buffer, size, "N/A");
    }
}

void
_minimize_workspace_windows (gf_wm_t *m, gf_ws_id_t ws_id, gf_handle_t exclude_id)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_win_list_t *windows = wm_windows (m);

    // Iterate backwards to maintain stacking order
    for (int32_t i = (int32_t)windows->count - 1; i >= 0; i--)
    {
        gf_win_info_t *win = &windows->items[i];

        if (win->workspace_id != ws_id || win->id == exclude_id)
            continue;

        if (wm_is_excluded (m, win->id))
            continue;

        platform->window_minimize (display, win->id);
        win->is_minimized = true;
    }
}

void
_unminimize_workspace_windows (gf_wm_t *m, gf_ws_id_t ws_id, gf_handle_t active_window)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_win_list_t *windows = wm_windows (m);

    gf_ws_info_t *ws = gf_workspace_list_find_by_id (wm_workspaces (m), ws_id);
    bool is_maximized_ws = (ws && ws->has_maximized_state);

    // First pass: unminimize all non-active windows in target workspace
    // Iterate backwards to maintain stacking order
    for (int32_t i = (int32_t)windows->count - 1; i >= 0; i--)
    {
        gf_win_info_t *win = &windows->items[i];

        if (win->workspace_id != ws_id)
            continue;

        // For maximized workspaces, we ONLY want one window visible at a time.
        // We skip unminimizing non-active windows in this pass if it's a maximized
        // workspace.
        if (is_maximized_ws)
            continue;

        if (wm_is_excluded (m, win->id))
            continue;

        if (platform->window_is_hidden && platform->window_is_hidden (display, win->id))
            continue;

        // Skip active window for now
        if (active_window != 0 && win->id == active_window)
            continue;

        platform->window_unminimize (display, win->id);
        win->is_minimized = false;

        if (m->config->enable_borders && !win->is_maximized && platform->border_add
            && !wm_is_excluded (m, win->id))
        {
            platform->border_add (platform, win->id, m->config->border_color, 3);
        }
    }

    // Second pass: unminimize active window last (to ensure it's on top)
    if (active_window != 0)
    {
        for (int32_t i = (int32_t)windows->count - 1; i >= 0; i--)
        {
            gf_win_info_t *win = &windows->items[i];

            if (win->workspace_id == ws_id && win->id == active_window)
            {
                platform->window_unminimize (display, win->id);
                win->is_minimized = false;

                if (m->config->enable_borders && !win->is_maximized
                    && platform->border_add && !wm_is_excluded (m, win->id))
                {
                    platform->border_add (platform, win->id, m->config->border_color, 3);
                }
                break;
            }
        }
    }
    else if (is_maximized_ws && windows->count > 0)
    {
        // If it's a maximized workspace but no active window was specified,
        // unminimize at least the first one we find in this workspace
        // to avoid having an empty-looking screen.
        for (uint32_t i = 0; i < windows->count; i++)
        {
            gf_win_info_t *win = &windows->items[i];
            if (win->workspace_id == ws_id && !wm_is_excluded (m, win->id))
            {
                platform->window_unminimize (display, win->id);
                win->is_minimized = false;
                break;
            }
        }
    }
}

void
_detect_minimization_changes (gf_wm_t *m, gf_ws_id_t current_workspace)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_win_list_t *windows = wm_windows (m);

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *win = &windows->items[i];

        if (!win->is_valid || wm_is_excluded (m, win->id))
            continue;

        if (win->workspace_id != current_workspace)
            continue;

        bool currently_minimized = platform->window_is_minimized (display, win->id);

        if (win->is_minimized != currently_minimized)
        {
            win->is_minimized = currently_minimized;
        }
    }
}

gf_err_t
gf_wm_window_sync (gf_wm_t *m, gf_handle_t window, gf_ws_id_t workspace_id)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_win_list_t *windows = wm_windows (m);
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    gf_handle_t id = window;

    if (wm_is_excluded (m, window))
    {

        return GF_SUCCESS;
    }

    gf_rect_t geom;
    if (platform->window_get_geometry (display, window, &geom) != GF_SUCCESS)
        return GF_ERROR_PLATFORM_ERROR;

    gf_win_info_t *existing = gf_window_list_find_by_window_id (windows, id);

    bool is_min_state = existing ? existing->is_minimized
                                 : (platform->window_is_minimized
                                        ? platform->window_is_minimized (display, window)
                                        : false);
    bool is_max_state = existing ? existing->is_maximized : false;

    gf_win_info_t info = {
        .id = window,
        .workspace_id = workspace_id,
        .geometry = geom,
        .is_minimized = is_min_state,
        .is_maximized = is_max_state,
        .is_valid = true,
        .needs_update = true,
        .last_modified = time (NULL),
    };

    if (existing)
    {
        strncpy (info.name, existing->name, sizeof (info.name) - 1);
        info.name[sizeof (info.name) - 1] = '\0';
    }

    return existing ? gf_window_list_update (windows, &info)
                    : gf_window_list_add (windows, &info);
}

void
gf_wm_prune (gf_wm_t *m)
{
    if (!m || !wm_platform (m))
        return;

    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);

    if (!windows || !workspaces)
        return;

    _remove_stale_windows (m, windows);
}

gf_err_t
gf_wm_window_move (gf_wm_t *m, gf_handle_t window_id, gf_ws_id_t target_workspace)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);

    // Find window
    gf_win_info_t *win = NULL;
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

    gf_ws_info_t *target_ws = gf_workspace_list_find_by_id (workspaces, target_workspace);

    if (target_ws->is_locked)
        return GF_ERROR_WORKSPACE_LOCKED;

    if (target_ws->has_maximized_state && !win->is_maximized)
        return GF_ERROR_WORKSPACE_MAXIMIZED;

    if (target_ws->window_count >= m->config->max_windows_per_workspace)
        return GF_ERROR_WORKSPACE_FULL;

    win->workspace_id = target_workspace;

    _rebuild_workspace_stats (m, workspaces, windows,
                              m->config->max_windows_per_workspace);
    _sync_workspaces (m);

    return GF_SUCCESS;
}
