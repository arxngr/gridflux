#include "../config/config.h"
#include "../config/excludes.h"
#include "../config/rules.h"
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

// Alt+E: toggle the focused window's app in the exclude list. The actual work —
// parking the window on the excluded workspace or bringing it back and re-tiling
// — is done by wm_reconcile_excluded_windows on the next tick.
static void
exclude_focused_window (gf_wm_t *m)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    // Prefer the window captured at keypress time; the live foreground window
    // may already have changed as the arrangement loop shifts focus.
    gf_handle_t focused = 0;
    if (platform->keymap_focused_window)
        focused = platform->keymap_focused_window (platform);
    if (!focused && platform->window_get_focused)
        focused = platform->window_get_focused (display);
    if (!focused)
        return;

    // Never toggle GridFlux's own GUI, the shell, or other system windows.
    if (platform->window_is_excluded && platform->window_is_excluded (display, focused))
    {
        GF_LOG_DEBUG ("Keymap: ignoring exclude for system/non-app window");
        return;
    }

    char class_name[256] = { 0 };
    gf_wm_window_class (m, focused, class_name, sizeof (class_name));
    if (class_name[0] == '\0')
        return;

    // Restoring is always allowed; the guard below only blocks new exclusions.
    if (gf_exclude_list_contains (&m->config->excluded_apps, class_name))
    {
        if (gf_excludes_remove (m->config, class_name) == GF_SUCCESS)
        {
            GF_LOG_INFO ("Keymap: restored focused app '%s'", class_name);
            // Reconcile now so the border is redrawn and the layout re-flows
            // immediately, rather than a tick later.
            wm_reconcile_excluded_windows (m);
        }
        return;
    }

    if (gf_excludes_add (m->config, class_name) == GF_SUCCESS)
    {
        GF_LOG_INFO ("Keymap: excluded focused app '%s'", class_name);
        wm_reconcile_excluded_windows (m);
    }
}

void
gf_wm_keymap_event (gf_wm_t *m)
{
    if (m->state.monitors_paused)
        return;
    gf_platform_t *platform = wm_platform (m);

    if (!m->state.keymap_initialized || !platform->keymap_poll)
        return;

    gf_display_t display = *wm_display (m);
    gf_key_action_t action = platform->keymap_poll (platform, display);

    if (action == GF_KEY_NONE)
        return;

    if (action == GF_KEY_EXCLUDE_FOCUSED)
    {
        exclude_focused_window (m);
        return;
    }

    gf_ws_list_t *workspaces = wm_workspaces (m);

    if (workspaces->count < 2)
    {
        GF_LOG_DEBUG ("Keymap: only %u workspace(s), nothing to switch",
                      workspaces->count);
        return;
    }

    gf_monitor_id_t active_monitor = wm_find_active_monitor (m);
    gf_handle_t key_window = platform->keymap_focused_window
                                 ? platform->keymap_focused_window (platform)
                                 : 0;
    if (key_window)
    {
        gf_win_info_t *focused
            = gf_window_list_find_by_window_id (wm_windows (m), key_window);
        if (focused)
            active_monitor = focused->monitor_id;
        else if (platform->monitor_from_window)
            active_monitor = platform->monitor_from_window (platform, key_window);
    }
    if (active_monitor >= GF_MAX_MONITORS)
        return;

    int current_idx = -1;
    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        if (!gf_workspace_has_monitor (&workspaces->items[i], active_monitor))
            continue;
        if (workspaces->items[i].id == workspaces->active_workspace[active_monitor])
        {
            current_idx = (int)i;
            break;
        }
    }

    if (current_idx < 0)
        return;

    int step = (action == GF_KEY_WORKSPACE_PREV) ? -1 : 1;
    int n = (int)workspaces->count;
    int target_idx = -1;

    for (int offset = 1; offset < n; offset++)
    {
        int idx = (current_idx + step * offset + n) % n;
        if (gf_workspace_has_monitor (&workspaces->items[idx], active_monitor)
            && wm_workspace_monitor_window_count (m, workspaces->items[idx].id,
                                                  active_monitor)
                   > 0)
        {
            target_idx = idx;
            break;
        }
    }

    if (target_idx < 0)
        return;

    gf_ws_id_t target_ws = workspaces->items[target_idx].id;

    if (target_ws == workspaces->active_workspace[active_monitor])
        return;

    wm_switch_workspace (m, target_ws, active_monitor);

    workspaces->active_workspace[active_monitor] = target_ws;
    m->state.last_active_workspace[active_monitor] = target_ws;

    gf_win_list_t *windows = wm_windows (m);
    for (uint32_t i = 0; i < windows->count; i++)
    {
        if (windows->items[i].workspace_id == target_ws && windows->items[i].is_valid
            && !windows->items[i].is_minimized
            && !wm_is_system_excluded (m, windows->items[i].id))
        {
            if (windows->items[i].monitor_id != active_monitor)
                continue;
            m->state.last_active_window[active_monitor] = windows->items[i].id;
            if (platform->window_focus)
                platform->window_focus (display, windows->items[i].id);
            break;
        }
    }

    wm_sync_monitor_activity (m, active_monitor);
    GF_LOG_INFO ("Keymap: switched to workspace %d", target_ws);
}

static void enter_maximized_mode (gf_wm_t *m, gf_win_info_t *focused,
                                  gf_handle_t curr_win_id);
static void enter_background_maximized_mode (gf_wm_t *m, gf_win_info_t *window);
static void exit_maximized_mode (gf_wm_t *m, gf_win_info_t *focused);

static bool
another_window_focused_on_monitor (gf_wm_t *m, gf_handle_t window,
                                   gf_monitor_id_t monitor_id)
{
    gf_platform_t *platform = wm_platform (m);
    if (!platform->window_get_focused)
        return false;

    gf_handle_t focused = platform->window_get_focused (*wm_display (m));
    if (!focused || focused == window)
        return false;

    gf_monitor_id_t focused_monitor = (gf_monitor_id_t)-1;
    if (platform->monitor_from_window)
        focused_monitor = platform->monitor_from_window (platform, focused);
    else
    {
        gf_win_info_t *tracked
            = gf_window_list_find_by_window_id (wm_windows (m), focused);
        if (tracked)
            focused_monitor = tracked->monitor_id;
    }

    return focused_monitor == monitor_id;
}

static bool
window_live_minimized (gf_wm_t *m, gf_handle_t window, bool fallback)
{
    gf_platform_t *platform = wm_platform (m);
    if (!platform->window_is_minimized)
        return fallback;
    return platform->window_is_minimized (*wm_display (m), window);
}

static void
sync_existing_window_state (gf_wm_t *m, gf_win_info_t *live, gf_win_info_t *existing)
{
    bool live_minimized = window_live_minimized (m, live->id, live->is_minimized);
    gf_ws_info_t *workspace
        = gf_workspace_list_find_by_id (wm_workspaces (m), existing->workspace_id);
    if (wm_user_excluded (m, live->id) || (workspace && workspace->is_excluded_ws))
    {
        // Excluded apps share visibility acknowledgement and monitor transfer,
        // while retaining their native geometry and maximize mode.
        bool native_minimized = live_minimized, ignored_maximized = false;
        existing->mode_wait = 0;
        wm_observe_window_state (m, existing, &live_minimized, &ignored_maximized);
        existing->is_minimized = live_minimized;
        existing->is_maximized = false;
        if (!live_minimized && !existing->visibility_request)
            existing->monitor_suspended = false;
        if (live_minimized || native_minimized)
            live->monitor_id = existing->monitor_id;
        if (workspace && workspace->is_excluded_ws && wm_user_excluded (m, live->id)
            && (!wm_platform (m)->window_is_interacting
                || !wm_platform (m)->window_is_interacting (*wm_display (m))))
            wm_move_window_to_monitor (m, existing, live->monitor_id);
        live->workspace_id = existing->workspace_id;
        live->restore_workspace_id = existing->restore_workspace_id;
        live->monitor_id = existing->monitor_id;
        live->monitor_suspended = existing->monitor_suspended;
        live->visibility_request = existing->visibility_request;
        live->visibility_wait = existing->visibility_wait;
        live->visibility_attempts = existing->visibility_attempts;
        live->visibility_settle = existing->visibility_settle;
        live->is_minimized = existing->is_minimized;
        live->is_maximized = false;
        if (live_minimized)
            live->geometry = existing->geometry;
        return;
    }
    bool native_minimized = live_minimized;
    bool mode_pending = existing->mode_wait != 0;
    bool live_maximized
        = wm_platform (m)->window_is_maximized
              ? wm_platform (m)->window_is_maximized (*wm_display (m), live->id)
              : live->is_maximized;
    wm_observe_window_state (m, existing, &live_minimized, &live_maximized);

    // Minimized windows can report stale rectangles. Keep their last monitor
    // only while they remain minimized, then follow their live position.
    if ((live_minimized || native_minimized || mode_pending)
        && existing->monitor_id < m->state.monitor_count)
        live->monitor_id = existing->monitor_id;

    if (existing->is_minimized != live_minimized)
    {
        gf_window_list_mark_all_needs_update (wm_windows (m), &existing->workspace_id);
        gf_ws_info_t *ws
            = gf_workspace_list_find_by_id (wm_workspaces (m), existing->workspace_id);
        if (ws)
            ws->is_custom_layout = false;
    }
    existing->is_minimized = live_minimized;
    if (!live_minimized && !existing->visibility_request)
        existing->monitor_suspended = false;

    bool interacting = wm_platform (m)->window_is_interacting
                       && wm_platform (m)->window_is_interacting (*wm_display (m));
    if (!interacting)
        wm_move_window_to_monitor (m, existing, live->monitor_id);

    // A window can maximize and lose focus before gf_wm_event sees it. Apply
    // every live transition here so it cannot remain in a normal workspace.
    // Iconifying a maximized window may clear the native maximized flag.
    // Its logical mode changes only after a visible restore by the user.
    if (live_minimized)
        live_maximized = existing->is_maximized || live->is_maximized;
    else if (!existing->mode_wait && wm_platform (m)->window_is_maximized)
        live_maximized = wm_platform (m)->window_is_maximized (*wm_display (m), live->id);
    if (existing->mode_wait)
        live_maximized = existing->is_maximized;
    if (interacting)
        live_maximized = existing->is_maximized;
    if (live_maximized && !existing->is_maximized)
    {
        if (another_window_focused_on_monitor (m, existing->id, existing->monitor_id))
            enter_background_maximized_mode (m, existing);
        else
        {
            enter_maximized_mode (m, existing, existing->id);
            if (existing->monitor_id < GF_MAX_MONITORS)
            {
                gf_ws_list_t *workspaces = wm_workspaces (m);
                workspaces->active_workspace[existing->monitor_id]
                    = existing->workspace_id;
                m->state.last_active_workspace[existing->monitor_id]
                    = existing->workspace_id;
                m->state.last_active_window[existing->monitor_id] = existing->id;
            }
        }
    }
    else if (!live_maximized && existing->is_maximized && !live_minimized)
        exit_maximized_mode (m, existing);

    live->workspace_id = existing->workspace_id;
    live->restore_workspace_id = existing->restore_workspace_id;
    live->monitor_id = existing->monitor_id;
    live->monitor_suspended = existing->monitor_suspended;
    live->visibility_request = existing->visibility_request;
    live->visibility_wait = existing->visibility_wait;
    live->visibility_attempts = existing->visibility_attempts;
    live->visibility_settle = existing->visibility_settle;
    live->mode_wait = existing->mode_wait;
    live->is_maximized = existing->is_maximized;
    live->is_minimized
        = existing->visibility_request && existing->visibility_wait
              ? existing->is_minimized
              : window_live_minimized (m, live->id, existing->is_minimized);

    if (live->is_minimized)
        live->geometry = existing->geometry;
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

    // Restore saved monitor placement before native relocation can be mistaken
    // for a user transfer. Workspace IDs and selections stay untouched.
    if (!wm_poll_monitors (m))
        return;

    wm_sync_workspaces (m);
    wm_enforce_fullscreen (m);

    for (uint32_t mon_idx = 0; mon_idx < GF_MAX_MONITORS; mon_idx++)
    {
        gf_ws_info_t *current_ws = gf_workspace_list_find_by_id (
            workspaces, workspaces->active_workspace[mon_idx]);
        (void)current_ws;
    }

    uint32_t native_workspace_count
        = platform->workspace_get_count ? platform->workspace_get_count (display) : 1;
    for (uint32_t wsi = 0; wsi < workspaces->count; wsi++)
    {
        gf_ws_info_t *workspace = &workspaces->items[wsi];
        // Native desktop enumeration is global on Linux. Walk each native
        // desktop once through monitor 0's normal workspace records; monitor-
        // local and special workspace IDs are GridFlux identities, not EWMH IDs.
        if (workspace->monitor_id != 0 || workspace->local_id < GF_FIRST_WORKSPACE_ID
            || workspace->local_id > (gf_ws_id_t)native_workspace_count
            || workspace->has_maximized_state || workspace->is_excluded_ws)
            continue;
        gf_ws_id_t ws_id = workspace->local_id - GF_FIRST_WORKSPACE_ID;

        gf_win_info_t *ws_wins = NULL;
        uint32_t ws_count = 0;

        if (!platform->window_enumerate
            || platform->window_enumerate (display, &ws_id, &ws_wins, &ws_count)
                   != GF_SUCCESS)
            continue;

        for (uint32_t i = 0; i < ws_count; i++)
        {
            gf_win_info_t *win = &ws_wins[i];

            if (!win->is_valid
                || (platform->window_is_excluded
                    && platform->window_is_excluded (display, win->id)))
                continue;

            if (platform->monitor_from_window)
                win->monitor_id = platform->monitor_from_window (platform, win->id);
            if (win->monitor_id >= GF_MAX_MONITORS)
                continue;

            gf_win_info_t *existing = gf_window_list_find_by_window_id (windows, win->id);

            if (!existing)
            {
                win->restore_workspace_id = 0;
                wm_register_new_window (m, win, NULL);
            }
            else
            {
                gf_wm_resolve_window_name (m, win->id, existing->name, win->name,
                                           sizeof (win->name));

                sync_existing_window_state (m, win, existing);

                const gf_window_rule_t *rule = gf_rules_find (m->config, win->name);

                gf_ws_info_t *current_ws
                    = gf_workspace_list_find_by_id (workspaces, win->workspace_id);

                if (current_ws && !rule && current_ws->has_rule
                    && !wm_user_excluded (m, win->id))
                {
                    gf_ws_id_t free_ws
                        = wm_lookup_or_create_ws_for_monitor (m, win->monitor_id);
                    if (gf_workspace_list_find_by_id (workspaces, free_ws))
                        wm_move_window_to_workspace (m, win, free_ws);
                }

                gf_window_list_update (windows, win);
            }
        }

        gf_free (ws_wins);
    }

    wm_reconcile_excluded_windows (m);
}

static void
enter_maximized_mode (gf_wm_t *m, gf_win_info_t *focused, gf_handle_t curr_win_id)
{
    gf_platform_t *platform = wm_platform (m);
    gf_win_list_t *windows = wm_windows (m);

    focused->is_maximized = true;
    focused->monitor_suspended = false;
    if (platform->border_remove)
        platform->border_remove (platform, focused->id);

    /* Capture the origin workspace BEFORE the move overwrites focused->workspace_id;
     * otherwise we would minimize the (empty) maximized workspace instead of the
     * windows the maximized window is covering. */
    gf_ws_id_t origin_ws = focused->workspace_id;
    focused->restore_workspace_id = origin_ws;

    gf_ws_id_t max_ws = wm_lookup_or_create_maximized_ws (m, focused->monitor_id);
    wm_move_window_to_workspace (m, focused, max_ws);
    wm_minimize_workspace_windows (m, origin_ws, focused->id, focused->monitor_id);
    gf_window_list_mark_all_needs_update (windows, &origin_ws);

    wm_sync_dock_visibility (m);
}

static void
enter_background_maximized_mode (gf_wm_t *m, gf_win_info_t *window)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_win_list_t *windows = wm_windows (m);

    window->is_maximized = true;
    window->monitor_suspended = true;
    if (platform->border_remove)
        platform->border_remove (platform, window->id);

    gf_ws_id_t origin_ws = window->workspace_id;
    window->restore_workspace_id = origin_ws;
    gf_ws_id_t max_ws = wm_lookup_or_create_maximized_ws (m, window->monitor_id);
    wm_move_window_to_workspace (m, window, max_ws);

    wm_request_visibility (m, window, true);
    gf_window_list_mark_all_needs_update (windows, &origin_ws);

    wm_sync_dock_visibility (m);
}

static void
exit_maximized_mode (gf_wm_t *m, gf_win_info_t *focused)
{
    gf_platform_t *platform = wm_platform (m);
    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_display_t display = *wm_display (m);

    gf_ws_id_t old_ws_id = focused->workspace_id;
    focused->is_maximized = false;
    focused->monitor_suspended = false;

    const gf_window_rule_t *rule = gf_rules_find (m->config, focused->name);
    gf_ws_id_t target_ws = rule ? gf_workspace_id_for_monitor_local (focused->monitor_id,
                                                                     rule->workspace_id)
                                : focused->restore_workspace_id;
    gf_ws_info_t *restore_ws = gf_workspace_list_find_by_id (workspaces, target_ws);
    bool rule_target = rule && restore_ws && restore_ws->monitor_id == focused->monitor_id
                       && restore_ws->rule_target_id == rule->workspace_id;
    if (!restore_ws || restore_ws->has_maximized_state || restore_ws->is_excluded_ws
        || restore_ws->monitor_id != focused->monitor_id
        || (restore_ws->is_locked && !rule_target))
    {
        gf_monitor_id_t monitor = focused->monitor_id;
        gf_ws_id_t active_id
            = monitor < GF_MAX_MONITORS ? workspaces->active_workspace[monitor] : -1;
        restore_ws = gf_workspace_list_find_by_id (workspaces, active_id);
        if (restore_ws && restore_ws->monitor_id == monitor
            && !restore_ws->has_maximized_state && !restore_ws->is_excluded_ws
            && !restore_ws->is_locked)
            target_ws = active_id;
        else
            target_ws = wm_lookup_or_create_ws_for_monitor (m, monitor);
    }
    wm_move_window_to_workspace (m, focused, target_ws);
    focused->restore_workspace_id = 0;
    wm_cleanup_empty_maximized_ws (m, old_ws_id);

    wm_sync_dock_visibility (m);

    /* Mark the target workspace dirty and clear custom layout flag so the
     * next tick's gf_wm_layout_apply re-tiles everything correctly.
     * Calling gf_wm_layout_apply here directly would race with gf_wm_watch
     * re-syncing geometry from the platform in the same tick. */
    gf_ws_info_t *target_ws_info
        = gf_workspace_list_find_by_id (workspaces, focused->workspace_id);
    if (target_ws_info)
        target_ws_info->is_custom_layout = false;

    if (!focused->is_minimized && m->config->enable_borders && platform->border_add
        && !wm_is_excluded (m, focused->id))
        platform->border_add (platform, focused->id, m->config->border_color,
                              GF_BORDER_WIDTH);

    gf_window_list_mark_all_needs_update (windows, &focused->workspace_id);
}

void
gf_wm_event (gf_wm_t *m)
{
    if (m->state.monitors_paused)
        return;
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);

    gf_handle_t curr_win_id = platform->window_get_focused (display);

    if (platform->window_is_interacting && platform->window_is_interacting (display))
        return;

    if (curr_win_id == 0)
    {
        GF_LOG_WARN ("[EVENT] No active window");
        return;
    }

    if (wm_is_system_excluded (m, curr_win_id))
    {
        if (platform->border_remove)
            platform->border_remove (platform, curr_win_id);
        gf_monitor_id_t monitor_id = wm_find_active_monitor (m);
        if (monitor_id < GF_MAX_MONITORS)
        {
            m->state.active_monitor_id = monitor_id;
            m->state.active_monitor_valid = true;
        }
        wm_sync_dock_visibility (m);
        return;
    }

    gf_win_info_t *focused = gf_window_list_find_by_window_id (windows, curr_win_id);

    if (!focused)
    {
        if (!wm_is_excluded (m, curr_win_id))
            GF_LOG_WARN ("[EVENT] Active window %lu not tracked yet", curr_win_id);
        return;
    }

    // The foreground window is the user's visibility selection. Cancel a
    // stale minimize request before selecting its monitor-local workspace.
    // An iconified foreground handle can linger during a native focus change;
    // it must not reverse minimization or wake a user-minimized app.
    if (window_live_minimized (m, curr_win_id, focused->is_minimized)
        && focused->visibility_request != 2)
        return;
    wm_request_visibility (m, focused, false);

    gf_monitor_id_t monitor_id = focused->monitor_id;
    if (platform->monitor_from_window && !focused->mode_wait
        && !window_live_minimized (m, curr_win_id, focused->is_minimized))
    {
        gf_monitor_id_t actual_monitor
            = platform->monitor_from_window (platform, curr_win_id);
        if (actual_monitor < GF_MAX_MONITORS && actual_monitor != monitor_id)
        {
            wm_move_window_to_monitor (m, focused, actual_monitor);
            monitor_id = actual_monitor;
        }
    }

    bool minimized = window_live_minimized (m, curr_win_id, focused->is_minimized);
    bool now_maximized
        = minimized || focused->mode_wait
              ? focused->is_maximized
              : platform->window_is_maximized
                    && platform->window_is_maximized (display, curr_win_id);
    bool was_maximized = focused->is_maximized;

    if (wm_user_excluded (m, curr_win_id))
        focused->is_maximized = false;
    else if (now_maximized && !was_maximized)
        enter_maximized_mode (m, focused, curr_win_id);
    else if (!now_maximized && was_maximized)
        exit_maximized_mode (m, focused);

    gf_ws_id_t current_workspace = focused->workspace_id;
    monitor_id = focused->monitor_id;
    if (monitor_id >= GF_MAX_MONITORS)
        return;

    wm_detect_minimize_changes (m, current_workspace, monitor_id);

    // Reconcile actual visibility even when a cached workspace ID already
    // matches. Delayed native restores can otherwise leave a maximized app
    // visible behind the selected normal workspace.
    wm_switch_workspace (m, current_workspace, monitor_id);

    m->state.last_active_window[monitor_id] = curr_win_id;
    m->state.last_active_workspace[monitor_id] = current_workspace;
    workspaces->active_workspace[monitor_id] = current_workspace;
    wm_sync_monitor_activity (m, monitor_id);
    wm_sync_dock_visibility (m);
}
