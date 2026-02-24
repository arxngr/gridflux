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
gf_wm_gesture_event (gf_wm_t *m)
{
    gf_platform_t *platform = wm_platform (m);

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

                gf_handle_t active = 0;
                if (platform->window_get_focused)
                    active = platform->window_get_focused (display);

                gf_win_info_t *max_wins = NULL;
                uint32_t max_count = _get_maximized_windows (m, &max_wins);

                if (max_count >= 2 && max_wins)
                {
                    int current_idx = _find_maximized_index (max_wins, max_count, active);

                    if (current_idx >= 0)
                    {
                        int next_idx = swipe_left ? (current_idx + 1) % (int)max_count
                                                  : (current_idx - 1 + (int)max_count)
                                                        % (int)max_count;

                        gf_handle_t next_win = max_wins[next_idx].id;

                        if (platform->window_minimize)
                            platform->window_minimize (display, active);
                        if (platform->window_unminimize)
                            platform->window_unminimize (display, next_win);

                        GF_LOG_INFO ("Gesture swipe %s: switched to window %lu",
                                     swipe_left ? "left" : "right", next_win);
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
}
