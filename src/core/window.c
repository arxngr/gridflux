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

static void
remove_stale_windows (gf_wm_t *m, gf_win_list_t *windows)
{
    uint32_t removed = 0;
    gf_ws_list_t *workspaces = wm_workspaces (m);

    for (uint32_t i = 0; i < windows->count;)
    {
        gf_win_info_t *win = &windows->items[i];
        bool excluded = wm_is_excluded (m, win->id);
        bool invalid = !wm_is_valid (m, win->id);
        bool hidden = m->platform->window_is_hidden
                      && m->platform->window_is_hidden (m->display, win->id);

        if (excluded || invalid || hidden)
        {
            if (win->is_maximized)
            {
                gf_ws_info_t *ws
                    = gf_workspace_list_find_by_id (workspaces, win->workspace_id);
                if (ws && ws->has_maximized_state)
                    cleanup_empty_maximized_ws (m, ws->id);
            }
            gf_handle_t stale_id = win->id;
            m->platform->border_remove (m->platform, stale_id);
            gf_window_list_remove (windows, stale_id);
            removed++;
            continue;
        }
        i++;
    }

    if (removed > 0)
    {
        m->state.resize_active = false;
        GF_LOG_DEBUG ("Cleaned %u invalid/excluded windows", removed);
    }
}

void
gf_wm_window_class (const gf_wm_t *m, gf_handle_t handle, char *buffer, size_t size)
{
    if (m->platform && m->platform->window_get_class)
        m->platform->window_get_class (m->display, handle, buffer, size);
    else
        snprintf (buffer, size, "N/A");
}

/* Use cached_name if it was already resolved; otherwise re-query the
 * platform. Callers pass NULL/"" for a never-seen window (always queries),
 * or a previous name for a tracked window whose class may have arrived
 * late (e.g. GTK apps that set WM_CLASS a moment after mapping). */
void
gf_wm_resolve_window_name (const gf_wm_t *m, gf_handle_t handle, const char *cached_name,
                           char *out_name, size_t out_size)
{
    if (cached_name && cached_name[0] != '\0')
    {
        strncpy (out_name, cached_name, out_size - 1);
        out_name[out_size - 1] = '\0';
        return;
    }

    gf_wm_window_class (m, handle, out_name, out_size);
}

gf_monitor_id_t
find_active_monitor (gf_wm_t *m)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_win_list_t *windows = wm_windows (m);

    gf_handle_t curr = platform->window_get_focused (display);
    if (curr != 0)
    {
        gf_win_info_t *focused = gf_window_list_find_by_window_id (windows, curr);
        if (focused)
            return focused->monitor_id;
        if (platform->monitor_from_window)
            return platform->monitor_from_window (platform, curr);
    }

    for (uint32_t i = 0; i < windows->count; i++)
    {
        if (windows->items[i].is_valid)
            return windows->items[i].monitor_id;
    }

    return 0;
}

void
minimize_workspace_windows (gf_wm_t *m, gf_ws_id_t ws_id, gf_handle_t exclude_id,
                            gf_monitor_id_t active_monitor)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_win_list_t *windows = wm_windows (m);

    for (int32_t i = (int32_t)windows->count - 1; i >= 0; i--)
    {
        gf_win_info_t *win = &windows->items[i];

        if (win->workspace_id != ws_id || win->id == exclude_id)
            continue;
        if (wm_is_excluded (m, win->id))
            continue;
        if (active_monitor != (gf_monitor_id_t)-1 && win->monitor_id != active_monitor)
            continue;

        platform->window_minimize (display, win->id);
        win->is_minimized = true;
    }
}

static void
restore_non_active_windows (gf_wm_t *m, gf_ws_id_t ws_id, gf_handle_t active_window,
                            gf_monitor_id_t active_monitor, bool is_maximized_ws)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_win_list_t *windows = wm_windows (m);

    for (int32_t i = (int32_t)windows->count - 1; i >= 0; i--)
    {
        gf_win_info_t *win = &windows->items[i];

        if (win->workspace_id != ws_id || is_maximized_ws)
            continue;
        if (wm_is_excluded (m, win->id))
            continue;
        if (active_monitor != (gf_monitor_id_t)-1 && win->monitor_id != active_monitor)
            continue;
        if (platform->window_is_hidden && platform->window_is_hidden (display, win->id))
            continue;
        if (active_window != 0 && win->id == active_window)
            continue;

        platform->window_unminimize (display, win->id);
        win->is_minimized = false;

        if (m->config->enable_borders && !win->is_maximized && platform->border_add
            && !wm_is_excluded (m, win->id))
            platform->border_add (platform, win->id, m->config->border_color,
                                  GF_BORDER_WIDTH);
    }
}

static void
restore_active_window (gf_wm_t *m, gf_ws_id_t ws_id, gf_handle_t active_window,
                       gf_monitor_id_t active_monitor)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_win_list_t *windows = wm_windows (m);

    for (int32_t i = (int32_t)windows->count - 1; i >= 0; i--)
    {
        gf_win_info_t *win = &windows->items[i];

        if (win->workspace_id != ws_id || win->id != active_window)
            continue;
        if (active_monitor != (gf_monitor_id_t)-1 && win->monitor_id != active_monitor)
            continue;

        platform->window_unminimize (display, win->id);
        win->is_minimized = false;

        if (m->config->enable_borders && !win->is_maximized && platform->border_add
            && !wm_is_excluded (m, win->id))
            platform->border_add (platform, win->id, m->config->border_color,
                                  GF_BORDER_WIDTH);
        break;
    }
}

static void
restore_fallback_window (gf_wm_t *m, gf_ws_id_t ws_id, gf_monitor_id_t active_monitor)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_win_list_t *windows = wm_windows (m);

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *win = &windows->items[i];
        if (win->workspace_id != ws_id || wm_is_excluded (m, win->id))
            continue;
        if (active_monitor != (gf_monitor_id_t)-1 && win->monitor_id != active_monitor)
            continue;

        platform->window_unminimize (display, win->id);
        win->is_minimized = false;
        break;
    }
}

void
restore_workspace_windows (gf_wm_t *m, gf_ws_id_t ws_id, gf_handle_t active_window,
                           gf_monitor_id_t active_monitor)
{
    gf_ws_info_t *ws = gf_workspace_list_find_by_id (wm_workspaces (m), ws_id);
    bool is_maximized_ws = (ws && ws->has_maximized_state);

    restore_non_active_windows (m, ws_id, active_window, active_monitor, is_maximized_ws);

    if (active_window != 0)
        restore_active_window (m, ws_id, active_window, active_monitor);
    else if (is_maximized_ws)
        restore_fallback_window (m, ws_id, active_monitor);
}

void
detect_minimize_changes (gf_wm_t *m, gf_ws_id_t current_workspace)
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

        if (win->is_minimized == currently_minimized)
            continue;

        win->is_minimized = currently_minimized;
        m->state.resize_active = false;
        gf_window_list_mark_all_needs_update (windows, &current_workspace);

        gf_ws_info_t *ws
            = gf_workspace_list_find_by_id (wm_workspaces (m), current_workspace);
        if (ws)
            ws->is_custom_layout = false;

        GF_LOG_INFO ("[EVENT] Window %p minimize state changed to %d, triggering "
                     "layout update",
                     (void *)win->id, currently_minimized);
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

    if (wm_is_excluded (m, window))
        return GF_SUCCESS;

    gf_rect_t geom;
    if (platform->window_get_geometry (display, window, &geom) != GF_SUCCESS)
        return GF_ERROR_PLATFORM_ERROR;

    gf_win_info_t *existing = gf_window_list_find_by_window_id (windows, window);

    bool is_min_state = existing ? existing->is_minimized
                                 : (platform->window_is_minimized
                                        ? platform->window_is_minimized (display, window)
                                        : false);
    bool is_max_state = existing ? existing->is_maximized : false;

    gf_monitor_id_t mon_id = 0;
    if (existing)
        mon_id = existing->monitor_id;
    else if (platform->monitor_from_window)
        mon_id = platform->monitor_from_window (platform, window);

    gf_win_info_t info = {
        .id = window,
        .workspace_id = workspace_id,
        .monitor_id = mon_id,
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
    else
    {
        gf_wm_window_class (m, window, info.name, sizeof (info.name));
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

    remove_stale_windows (m, windows);
}

gf_err_t
gf_wm_window_move (gf_wm_t *m, gf_handle_t window_id, gf_ws_id_t target_workspace)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);

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

    recount_workspace_windows (m, workspaces, windows,
                               m->config->max_windows_per_workspace);
    sync_workspaces (m);

    return GF_SUCCESS;
}

uint32_t
find_maximized_windows (gf_wm_t *m, gf_win_info_t **out_windows)
{
    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);
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

    gf_win_info_t *result = gf_malloc (total * sizeof (gf_win_info_t));
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

        gf_win_info_t *ws_wins = NULL;
        uint32_t ws_count = 0;
        if (gf_window_list_get_by_workspace (windows, workspaces->items[i].id, &ws_wins,
                                             &ws_count)
            == GF_SUCCESS)
        {
            for (uint32_t j = 0; j < ws_count && idx < total; j++)
                result[idx++] = ws_wins[j];
            gf_free (ws_wins);
        }
    }

    *out_windows = result;
    return idx;
}

int
find_maximized_ws_index (gf_win_info_t *windows, uint32_t count, gf_handle_t handle)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (windows[i].id == handle)
            return (int)i;
    }
    return -1;
}
