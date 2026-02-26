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

static bool
_workspace_is_rule_target (const gf_config_t *cfg, gf_ws_id_t ws_id)
{
    if (!cfg)
        return false;
    for (uint32_t i = 0; i < cfg->window_rules_count; i++)
    {
        if (cfg->window_rules[i].workspace_id == ws_id)
            return true;
    }
    return false;
}

static bool
_window_has_rule (const gf_config_t *cfg, const char *wm_class)
{
    return gf_rules_find (cfg, wm_class) != NULL;
}

// Evict a non-rule window from a workspace to make room for a rule-bound window
static void
_evict_non_rule_window (gf_wm_t *m, gf_ws_id_t ws_id)
{
    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    // Find a non-rule window in this workspace to move out
    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *win = &windows->items[i];
        if (win->workspace_id != ws_id || !win->is_valid)
            continue;

        char name[256];
        gf_wm_window_class (m, win->id, name, sizeof (name));

        if (_window_has_rule (m->config, name))
            continue; // Don't evict rule-bound windows

        // Find a free workspace for this window
        gf_ws_id_t dst_id = -1;
        for (uint32_t j = 0; j < workspaces->count; j++)
        {
            gf_ws_info_t *dst = &workspaces->items[j];
            if (dst->id == ws_id || dst->is_locked || dst->has_maximized_state)
                continue;
            if (dst->window_count < max_per_ws)
            {
                dst_id = dst->id;
                break;
            }
        }

        if (dst_id < 0)
        {
            // Create a new workspace
            dst_id = gf_workspace_create (workspaces, max_per_ws, false, false);
        }

        if (dst_id >= 0)
        {
            GF_LOG_INFO ("Evicting window %p (%s) from workspace %d to %d for rule",
                         (void *)win->id, name, ws_id, dst_id);
            win->workspace_id = dst_id;
            _rebuild_workspace_stats (m, workspaces, windows, max_per_ws);
        }
        return;
    }
}

gf_ws_info_t *
_get_workspace (gf_ws_list_t *workspaces, gf_ws_id_t id)
{
    if (!workspaces || id < GF_FIRST_WORKSPACE_ID)
        return NULL;
    return gf_workspace_list_find_by_id (workspaces, id);
}

void
_cleanup_unused_workspace (gf_wm_t *m, gf_ws_list_t *list, uint32_t index)
{
    if (!list || index >= list->count)
        return;

    gf_ws_id_t ws_id = list->items[index].id;
    if (_workspace_is_rule_target (m->config, ws_id))
        return;

    memmove (&list->items[index], &list->items[index + 1],
             (list->count - index - 1) * sizeof (gf_ws_info_t));

    list->count--;
    memset (&list->items[list->count], 0, sizeof (gf_ws_info_t));
}

bool
_workspace_is_valid (gf_ws_list_t *workspaces, gf_ws_id_t id)
{
    return _get_workspace (workspaces, id) != NULL;
}

void
_move_window_between_workspaces (gf_wm_t *m, gf_win_info_t *win, gf_ws_id_t new_ws_id)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_win_list_t *windows = wm_windows (m);

    gf_ws_info_t *old = gf_workspace_list_find_by_id (workspaces, win->workspace_id);
    gf_ws_info_t *new = gf_workspace_list_find_by_id (workspaces, new_ws_id);

    if (!old || !new || old == new)
        return;

    gf_workspace_list_remove_window (old, windows, win->id);
    gf_workspace_list_add_window (new, windows, win->id);

    win->workspace_id = new_ws_id;
}

bool
_workspace_has_capacity (gf_ws_info_t *ws, uint32_t max_per_ws)
{
    return !ws->is_locked && ws->window_count < max_per_ws;
}

void
_rebuild_workspace_stats (gf_wm_t *m, gf_ws_list_t *workspaces, gf_win_list_t *windows,
                          uint32_t max_per_ws)
{
    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        workspaces->items[i].window_count = 0;
        workspaces->items[i].available_space = max_per_ws;
    }

    for (uint32_t i = 0; i < windows->count; i++)
    {
        if (windows->items[i].is_valid && !wm_is_excluded (m, windows->items[i].id))
        {
            gf_ws_id_t ws_id = windows->items[i].workspace_id;
            gf_ws_info_t *ws = gf_workspace_list_find_by_id (workspaces, ws_id);
            if (ws)
            {
                ws->window_count++;
                ws->available_space--;
            }
        }
    }
}

bool
_window_has_valid_workspace (gf_win_info_t *win, gf_ws_list_t *workspaces)
{
    return win->workspace_id >= GF_FIRST_WORKSPACE_ID
           && _workspace_is_valid (workspaces, win->workspace_id);
}

void
_handle_workspace_switch (gf_wm_t *m, gf_ws_id_t current_workspace)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);

    GF_LOG_DEBUG ("Workspace changed from %d to %d", m->state.last_active_workspace,
                  current_workspace);

    // Minimize windows in other workspaces
    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_ws_id_t ws_id = workspaces->items[i].id;
        if (ws_id == current_workspace)
            continue;

        _minimize_workspace_windows (m, ws_id, 0);
    }

    gf_platform_t *platform = wm_platform (m);

    // Get current active window to preserve focus
    gf_handle_t active_window = 0;
    if (platform->window_get_focused)
        active_window = platform->window_get_focused (*wm_display (m));

    // Unminimize windows in current workspace
    _unminimize_workspace_windows (m, current_workspace, active_window);

    // Toggle dock based on target workspace type
    gf_ws_info_t *target_ws
        = gf_workspace_list_find_by_id (workspaces, current_workspace);

    if (target_ws && target_ws->has_maximized_state)
    {
        if (!m->state.dock_hidden && platform->dock_hide)
        {
            platform->dock_hide (platform);
            m->state.dock_hidden = true;
        }
    }
    else
    {
        if (m->state.dock_hidden && platform->dock_restore)
        {
            platform->dock_restore (platform);
            m->state.dock_hidden = false;
        }
    }
}

void
_sync_workspaces (gf_wm_t *m)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    uint32_t platform_count = platform->workspace_get_count (display);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    for (uint32_t i = GF_FIRST_WORKSPACE_ID; i <= platform_count; i++)
    {
        gf_workspace_list_ensure (workspaces, i, max_per_ws);
        gf_ws_info_t *ws = gf_workspace_list_find_by_id (workspaces, i);
        if (!ws)
            continue;

        ws->is_locked = gf_config_workspace_is_locked (m->config, i);
        ws->available_space = ws->is_locked ? 0 : (max_per_ws - ws->window_count);
        ws->max_windows = max_per_ws;
    }
}

void
_build_workspace_candidate (gf_wm_t *m)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_win_list_t *windows = wm_windows (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    // Ensure workspaces targeted by rules always exist
    for (uint32_t i = 0; i < m->config->window_rules_count; i++)
    {
        gf_ws_id_t rule_ws = m->config->window_rules[i].workspace_id;
        gf_workspace_list_ensure (workspaces, rule_ws, max_per_ws);
    }

    // First pass: preserve existing workspace assignments
    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *win = &windows->items[i];

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
        gf_win_info_t *win = &windows->items[i];

        if (!win->is_valid || _window_has_valid_workspace (win, workspaces))
            continue;

        // Check if this window has a rule — if so, it was already handled
        char class_name[256];
        gf_wm_window_class (m, win->id, class_name, sizeof (class_name));
        const gf_window_rule_t *rule = gf_rules_find (m->config, class_name);
        if (rule)
        {
            win->workspace_id = rule->workspace_id;
            continue;
        }

        // Find next available unlocked, non-rule-reserved workspace with space
        while (ws_id < workspaces->count)
        {
            gf_ws_info_t *check_ws = gf_workspace_list_find_by_id (workspaces, ws_id);
            bool is_rule_reserved = _workspace_is_rule_target (m->config, ws_id);

            if (!check_ws || check_ws->is_locked || is_rule_reserved
                || slot >= max_per_ws)
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
    _rebuild_workspace_stats (m, workspaces, windows, max_per_ws);

    if (workspaces->active_workspace >= workspaces->count)
        workspaces->active_workspace = GF_FIRST_WORKSPACE_ID;

    _sync_workspaces (m);
}

gf_ws_id_t
_find_or_create_maximized_ws (gf_wm_t *m)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        if (workspaces->items[i].has_maximized_state)
        {
            return workspaces->items[i].id;
        }
    }

    return gf_workspace_create (workspaces, m->config->max_windows_per_workspace, true,
                                true);
}

gf_ws_id_t
_find_or_create_ws (gf_wm_t *m)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        if (!workspaces->items[i].has_maximized_state
            && workspaces->items[i].available_space > 0)
        {
            return workspaces->items[i].id;
        }
    }

    return gf_workspace_create (workspaces, m->config->max_windows_per_workspace, false,
                                false);
}

gf_ws_id_t
_assign_workspace_for_window (gf_wm_t *m, gf_win_info_t *win, gf_ws_info_t *current_ws)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    if (current_ws && current_ws->available_space > 0)
        return current_ws->id;

    if (platform->window_is_maximized && platform->window_is_maximized (display, win->id))
    {
        win->is_maximized = true;
        return _find_or_create_maximized_ws (m);
    }

    return _find_or_create_ws (m);
}

void
_handle_new_window (gf_wm_t *m, gf_win_info_t *win, gf_ws_info_t *current_ws)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    char class_name[256];
    gf_wm_window_class (m, win->id, class_name, sizeof (class_name));

    // Check if this window is initially maximized
    if (platform->window_is_maximized && platform->window_is_maximized (display, win->id))
    {
        win->is_maximized = true;
        win->workspace_id = _find_or_create_maximized_ws (m);
    }
    else
    {
        // Not maximized, check if this window matches a rule
        const gf_window_rule_t *rule = gf_rules_find (m->config, class_name);
        if (rule)
        {
            gf_ws_id_t target = rule->workspace_id;
            gf_workspace_list_ensure (workspaces, target, max_per_ws);

            gf_ws_info_t *target_ws = gf_workspace_list_find_by_id (workspaces, target);

            // If workspace is full, evict a non-rule window
            if (target_ws && target_ws->window_count >= max_per_ws)
            {
                _evict_non_rule_window (m, target);
            }

            win->workspace_id = target;
            GF_LOG_INFO ("Rule matched: %s → workspace %d", class_name, target);
        }
        else
        {
            win->workspace_id = _assign_workspace_for_window (m, win, current_ws);
        }
    }

    win->is_minimized = false;

    gf_window_list_add (windows, win);

    platform->window_unminimize (display, win->id);

    m->state.last_active_window = win->id;
    m->state.last_active_workspace = win->workspace_id;
    workspaces->active_workspace = win->workspace_id;

    if (m->config->enable_borders && platform->border_add)
        platform->border_add (platform, win->id, m->config->border_color, 3);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_ws_id_t ws_id = workspaces->items[i].id;
        if (ws_id == win->workspace_id)
            continue;

        _minimize_workspace_windows (m, ws_id, 0);
    }

    GF_LOG_INFO ("New window %p → workspace %u (%s)", (void *)win->id, win->workspace_id,
                 class_name);
}

gf_err_t
gf_wm_workspace_lock (gf_wm_t *m, gf_ws_id_t workspace_id)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_ws_list_t *workspaces = wm_workspaces (m);

    if (workspace_id < GF_FIRST_WORKSPACE_ID
        || workspace_id >= m->config->max_workspaces + GF_FIRST_WORKSPACE_ID)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_list_ensure (workspaces, workspace_id,
                              m->config->max_windows_per_workspace);

    gf_ws_info_t *ws = gf_workspace_list_find_by_id (workspaces, workspace_id);

    if (ws->is_locked)
        return GF_ERROR_ALREADY_LOCKED;

    ws->is_locked = true;

    gf_config_workspace_lock (m->config, workspace_id);

    _rebuild_workspace_stats (m, workspaces, wm_windows (m),
                              m->config->max_windows_per_workspace);
    _sync_workspaces (m);

    return GF_SUCCESS;
}

gf_err_t
gf_wm_workspace_unlock (gf_wm_t *m, gf_ws_id_t workspace_id)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_ws_list_t *workspaces = wm_workspaces (m);

    if (workspace_id < GF_FIRST_WORKSPACE_ID
        || workspace_id >= m->config->max_workspaces + GF_FIRST_WORKSPACE_ID)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_list_ensure (workspaces, workspace_id,
                              m->config->max_windows_per_workspace);

    gf_ws_info_t *ws = gf_workspace_list_find_by_id (workspaces, workspace_id);

    if (!ws->is_locked)
        return GF_ERROR_ALREADY_UNLOCKED;

    ws->is_locked = false;

    gf_config_workspace_unlock (m->config, workspace_id);

    _rebuild_workspace_stats (m, workspaces, wm_windows (m),
                              m->config->max_windows_per_workspace);
    _sync_workspaces (m);

    return GF_SUCCESS;
}
