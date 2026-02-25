#include "../config/config.h"
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
gf_wm_keymap_event (gf_wm_t *m)
{
    gf_platform_t *platform = wm_platform (m);

    if (!m->state.keymap_initialized || !platform->keymap_poll)
        return;

    gf_display_t display = *wm_display (m);
    gf_key_action_t action = platform->keymap_poll (platform, display);

    if (action == GF_KEY_NONE)
        return;

    gf_ws_list_t *workspaces = wm_workspaces (m);

    if (workspaces->count < 2)
    {
        GF_LOG_DEBUG ("Keymap: only %u workspace(s), nothing to switch", workspaces->count);
        return;
    }

    /* Find the index of the currently active workspace. */
    int current_idx = -1;
    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        if (workspaces->items[i].id == workspaces->active_workspace)
        {
            current_idx = (int)i;
            break;
        }
    }

    if (current_idx < 0)
        return;

    /* Scan in the requested direction, skipping empty workspaces. */
    int step = (action == GF_KEY_WORKSPACE_PREV) ? -1 : 1;
    int n = (int)workspaces->count;
    int target_idx = -1;

    for (int offset = 1; offset < n; offset++)
    {
        int idx = (current_idx + step * offset + n) % n;
        if (workspaces->items[idx].window_count > 0)
        {
            target_idx = idx;
            break;
        }
    }

    if (target_idx < 0)
        return; /* no non-empty workspace to switch to */

    gf_ws_id_t target_ws = workspaces->items[target_idx].id;

    if (target_ws == workspaces->active_workspace)
        return;

    /* Switch workspace — this minimizes other workspaces & unminimizes target. */
    _handle_workspace_switch (m, target_ws);

    workspaces->active_workspace = target_ws;
    m->state.last_active_workspace = target_ws;

    /* Raise and focus the first valid window in the target workspace. */
    gf_win_list_t *windows = wm_windows (m);
    for (uint32_t i = 0; i < windows->count; i++)
    {
        if (windows->items[i].workspace_id == target_ws && windows->items[i].is_valid)
        {
            if (platform->window_unminimize)
                platform->window_unminimize (display, windows->items[i].id);
            m->state.last_active_window = windows->items[i].id;
            break;
        }
    }


    GF_LOG_INFO ("Keymap: switched to workspace %d", target_ws);
}

void
gf_wm_watch (gf_wm_t *m)
{
    if (!m)
        return;

    gf_platform_t *platform = wm_platform (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_win_list_t *windows = wm_windows (m);
    gf_display_t display = *wm_display (m);

    _sync_workspaces (m);
    _handle_fullscreen_windows (m);

    gf_ws_info_t *current_ws
        = gf_workspace_list_find_by_id (workspaces, workspaces->active_workspace);

    for (uint32_t wsi = 0; wsi < workspaces->count; wsi++)
    {
        gf_ws_id_t ws_id = workspaces->items[wsi].id - GF_FIRST_WORKSPACE_ID;

        gf_win_info_t *ws_windows = NULL;
        uint32_t ws_count = 0;

        if (!platform->window_enumerate
            || platform->window_enumerate (display, &ws_id, &ws_windows, &ws_count)
                   != GF_SUCCESS)
            continue;

        for (uint32_t i = 0; i < ws_count; i++)
        {
            gf_win_info_t *win = &ws_windows[i];

            if (!win->is_valid || wm_is_excluded (m, win->id))
            {
                continue;
            }

            gf_win_info_t *existing = gf_window_list_find_by_window_id (windows, win->id);

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
        }

        gf_free (ws_windows);
    }
}

void
gf_wm_event (gf_wm_t *m)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);

    gf_handle_t curr_win_id = platform->window_get_focused (display);

    if (curr_win_id == 0)
    {
        GF_LOG_WARN ("[EVENT] No active window");
        return;
    }

    gf_win_info_t *focused = gf_window_list_find_by_window_id (windows, curr_win_id);

    if (!focused)
    {
        if (!wm_is_excluded (m, curr_win_id))
            GF_LOG_WARN ("[EVENT] Active window %lu not tracked yet", curr_win_id);
        return;
    }

    bool now_maximized = false;
    if (platform->window_is_maximized)
        now_maximized = platform->window_is_maximized (display, curr_win_id);

    bool was_maximized = focused->is_maximized;
    if (now_maximized)
    {
        focused->is_maximized = true;

        gf_ws_id_t max_ws = _find_or_create_maximized_ws (m);

        _move_window_between_workspaces (m, focused, max_ws);

        // Handle switching between maximized windows (e.g. Alt+Tab)
        // Ensure other windows in the same maximized workspace are minimized
        _minimize_workspace_windows (m, focused->workspace_id, focused->id);

        // Hide dock when maximizing
        if (!m->state.dock_hidden && platform->dock_hide)
        {
            platform->dock_hide (platform);
            m->state.dock_hidden = true;
        }
    }
    else if (!now_maximized && was_maximized)
    {
        gf_ws_id_t old_ws_id = focused->workspace_id;
        focused->is_maximized = false;

        gf_ws_id_t normal_ws = _find_or_create_ws (m);

        _move_window_between_workspaces (m, focused, normal_ws);

        if (m->state.dock_hidden && platform->dock_restore)
        {
            platform->dock_restore (platform);
            m->state.dock_hidden = false;
        }
    }

    gf_ws_id_t current_workspace = focused->workspace_id;

    _detect_minimization_changes (m, current_workspace);

    bool workspace_changed = (m->state.last_active_workspace != current_workspace);
    bool has_previous_window = (m->state.last_active_window != 0);

    if (workspace_changed && has_previous_window)
    {
        _handle_workspace_switch (m, current_workspace);
    }

    m->state.last_active_window = curr_win_id;
    m->state.last_active_workspace = current_workspace;
    workspaces->active_workspace = current_workspace;
}
