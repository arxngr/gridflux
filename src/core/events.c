#include "../config/config.h"
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
        GF_LOG_DEBUG ("Keymap: only %u workspace(s), nothing to switch",
                      workspaces->count);
        return;
    }

    gf_monitor_id_t active_monitor = find_active_monitor (m);

    int current_idx = -1;
    for (uint32_t i = 0; i < workspaces->count; i++)
    {
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
        if (workspaces->items[idx].window_count > 0)
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

    switch_workspace (m, target_ws);

    workspaces->active_workspace[active_monitor] = target_ws;
    m->state.last_active_workspace[active_monitor] = target_ws;

    gf_win_list_t *windows = wm_windows (m);
    for (uint32_t i = 0; i < windows->count; i++)
    {
        if (windows->items[i].workspace_id == target_ws && windows->items[i].is_valid)
        {
            if (platform->window_unminimize)
                platform->window_unminimize (display, windows->items[i].id);
            m->state.last_active_window[active_monitor] = windows->items[i].id;
            break;
        }
    }

    GF_LOG_INFO ("Keymap: switched to workspace %d", target_ws);
}

// Register a new window, or refresh an existing one keeping its workspace.
static void
watch_process_window (gf_wm_t *m, gf_win_info_t *win)
{
    gf_platform_t *platform = wm_platform (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_win_list_t *windows = wm_windows (m);

    if (!win->is_valid || wm_is_excluded (m, win->id))
        return;

    if (platform->monitor_from_window)
        win->monitor_id = platform->monitor_from_window (platform, win->id);

    gf_win_info_t *existing = gf_window_list_find_by_window_id (windows, win->id);
    if (!existing)
    {
        register_new_window (m, win, NULL);
        return;
    }

    win->workspace_id = existing->workspace_id;
    gf_wm_resolve_window_name (m, win->id, existing->name, win->name, sizeof (win->name));

    const gf_window_rule_t *rule = gf_rules_find (m->config, win->name);
    gf_ws_info_t *current_ws
        = gf_workspace_list_find_by_id (workspaces, win->workspace_id);

    if (current_ws && !rule && current_ws->has_rule)
    {
        gf_ws_id_t free_ws = gf_workspace_list_find_free (workspaces);
        if (gf_workspace_list_find_by_id (workspaces, free_ws))
            move_window_to_workspace (m, win, free_ws);
    }

    win->is_maximized = existing->is_maximized;
    win->is_minimized = existing->is_minimized;
    if (win->is_minimized)
        win->geometry = existing->geometry;

    gf_window_list_update (windows, win);
}

// New-window placement depends on visit order (workspaces fill up), so keep it
// deterministic in desktop order, as the old per-workspace scan was.
static void
sort_by_workspace (gf_win_info_t *wins, uint32_t count)
{
    for (uint32_t i = 1; i < count; i++)
    {
        gf_win_info_t key = wins[i];
        uint32_t j = i;
        while (j > 0 && wins[j - 1].workspace_id > key.workspace_id)
        {
            wins[j] = wins[j - 1];
            j--;
        }
        wins[j] = key;
    }
}

void
gf_wm_watch (gf_wm_t *m)
{
    if (!m)
        return;

    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    sync_workspaces (m);
    enforce_fullscreen (m);

    // Enumerate once, not once per workspace; placement is decided per window.
    gf_win_info_t *wins = NULL;
    uint32_t count = 0;
    if (!platform->window_enumerate
        || platform->window_enumerate (display, NULL, &wins, &count) != GF_SUCCESS)
        return;

    sort_by_workspace (wins, count);

    for (uint32_t i = 0; i < count; i++)
        watch_process_window (m, &wins[i]);

    gf_free (wins);
}

static void
enter_maximized_mode (gf_wm_t *m, gf_win_info_t *focused, gf_handle_t curr_win_id)
{
    gf_platform_t *platform = wm_platform (m);
    gf_win_list_t *windows = wm_windows (m);

    focused->is_maximized = true;

    /* Capture the origin workspace BEFORE the move overwrites focused->workspace_id;
     * otherwise we would minimize the (empty) maximized workspace instead of the
     * windows the maximized window is covering. */
    gf_ws_id_t origin_ws = focused->workspace_id;

    gf_ws_id_t max_ws = lookup_or_create_maximized_ws (m);
    move_window_to_workspace (m, focused, max_ws);
    minimize_workspace_windows (m, origin_ws, focused->id, focused->monitor_id);

    if (!m->state.dock_hidden && platform->dock_hide)
    {
        platform->dock_hide (platform);
        m->state.dock_hidden = true;
        gf_window_list_mark_all_needs_update (windows, &focused->workspace_id);
    }
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

    const gf_window_rule_t *rule = gf_rules_find (m->config, focused->name);
    gf_ws_id_t target_ws = rule ? rule->workspace_id : lookup_or_create_ws (m);
    move_window_to_workspace (m, focused, target_ws);
    cleanup_empty_maximized_ws (m, old_ws_id);

    if (m->state.dock_hidden && platform->dock_restore)
    {
        platform->dock_restore (platform);
        m->state.dock_hidden = false;
    }

    /* Mark the target workspace dirty and clear custom layout flag so the
     * next tick's gf_wm_layout_apply re-tiles everything correctly.
     * Calling gf_wm_layout_apply here directly would race with gf_wm_watch
     * re-syncing geometry from the platform in the same tick. */
    gf_ws_info_t *target_ws_info
        = gf_workspace_list_find_by_id (workspaces, focused->workspace_id);
    if (target_ws_info)
        target_ws_info->is_custom_layout = false;

    gf_window_list_mark_all_needs_update (windows, &focused->workspace_id);
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

    bool now_maximized = platform->window_is_maximized
                         && platform->window_is_maximized (display, curr_win_id);
    bool was_maximized = focused->is_maximized;

    if (now_maximized && !was_maximized)
        enter_maximized_mode (m, focused, curr_win_id);
    else if (!now_maximized && was_maximized)
        exit_maximized_mode (m, focused);

    gf_ws_id_t current_workspace = focused->workspace_id;
    gf_monitor_id_t monitor_id = focused->monitor_id;

    detect_minimize_changes (m, current_workspace);

    bool workspace_changed
        = (m->state.last_active_workspace[monitor_id] != current_workspace);
    bool has_previous_window = (m->state.last_active_window[monitor_id] != 0);

    if (workspace_changed && has_previous_window)
        switch_workspace (m, current_workspace);

    m->state.last_active_window[monitor_id] = curr_win_id;
    m->state.last_active_workspace[monitor_id] = current_workspace;
    workspaces->active_workspace[monitor_id] = current_workspace;
}
