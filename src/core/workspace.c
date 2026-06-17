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
workspace_is_rule_target (const gf_config_t *cfg, gf_ws_id_t ws_id)
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
window_has_rule (const gf_config_t *cfg, const char *wm_class)
{
    return gf_rules_find (cfg, wm_class) != NULL;
}

/* Evict a non-rule window from a workspace to make room for a rule-bound window */
static void
evict_non_rule_window (gf_wm_t *m, gf_ws_id_t ws_id)
{
    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *win = &windows->items[i];
        if (win->workspace_id != ws_id || !win->is_valid)
            continue;

        char name[256];
        gf_wm_window_class (m, win->id, name, sizeof (name));

        if (window_has_rule (m->config, name))
            continue;

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
            dst_id = gf_workspace_create (workspaces, max_per_ws, false, false);

        if (dst_id >= 0)
        {
            GF_LOG_INFO ("Evicting window %p (%s) from workspace %d to %d for rule",
                         (void *)win->id, name, ws_id, dst_id);
            win->workspace_id = dst_id;
            recount_workspace_windows (m, workspaces, windows, max_per_ws);
        }
        return;
    }
}

gf_ws_info_t *
find_workspace (gf_ws_list_t *workspaces, gf_ws_id_t id)
{
    if (!workspaces || id < GF_FIRST_WORKSPACE_ID)
        return NULL;
    return gf_workspace_list_find_by_id (workspaces, id);
}

void
cleanup_unused_workspace (gf_wm_t *m, gf_ws_list_t *list, uint32_t index)
{
    if (!list || index >= list->count)
        return;

    gf_ws_id_t ws_id = list->items[index].id;
    if (workspace_is_rule_target (m->config, ws_id))
        return;

    memmove (&list->items[index], &list->items[index + 1],
             (list->count - index - 1) * sizeof (gf_ws_info_t));

    list->count--;
    memset (&list->items[list->count], 0, sizeof (gf_ws_info_t));
}

bool
ws_is_valid (gf_ws_list_t *workspaces, gf_ws_id_t id)
{
    return find_workspace (workspaces, id) != NULL;
}

void
move_window_to_workspace (gf_wm_t *m, gf_win_info_t *win, gf_ws_id_t new_ws_id)
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
    m->platform->window_minimize (m->display, win->id);
}

bool
ws_has_capacity (gf_ws_info_t *ws, uint32_t max_per_ws)
{
    return !ws->is_locked && ws->window_count < max_per_ws;
}

void
recount_workspace_windows (gf_wm_t *m, gf_ws_list_t *workspaces, gf_win_list_t *windows,
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
win_has_assigned_workspace (gf_win_info_t *win, gf_ws_list_t *workspaces)
{
    return win->workspace_id >= GF_FIRST_WORKSPACE_ID
           && ws_is_valid (workspaces, win->workspace_id);
}

void
switch_workspace (gf_wm_t *m, gf_ws_id_t current_workspace)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_monitor_id_t active_monitor = find_active_monitor (m);

    GF_LOG_DEBUG ("Workspace changed from %d to %d", m->state.last_active_workspace,
                  current_workspace);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_ws_id_t ws_id = workspaces->items[i].id;
        if (ws_id == current_workspace)
            continue;
        minimize_workspace_windows (m, ws_id, 0, active_monitor);
    }

    gf_platform_t *platform = wm_platform (m);
    gf_handle_t active_window = 0;
    if (platform->window_get_focused)
        active_window = platform->window_get_focused (*wm_display (m));

    restore_workspace_windows (m, current_workspace, active_window, active_monitor);

    gf_ws_info_t *target_ws
        = gf_workspace_list_find_by_id (workspaces, current_workspace);

    if (target_ws && target_ws->has_maximized_state)
    {
        if (!m->state.dock_hidden && platform->dock_hide)
        {
            platform->dock_hide (platform);
            m->state.dock_hidden = true;

            if (!target_ws->is_custom_layout)
                gf_window_list_mark_all_needs_update (wm_windows (m), &current_workspace);
        }
    }
    else
    {
        if (m->state.dock_hidden && platform->dock_restore)
        {
            platform->dock_restore (platform);
            m->state.dock_hidden = false;

            if (target_ws && !target_ws->is_custom_layout)
                gf_window_list_mark_all_needs_update (wm_windows (m), &current_workspace);
        }
    }
}

void
sync_workspaces (gf_wm_t *m)
{
    if (!m || !m->config)
        return;

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

        ws->is_locked
            = ws->has_rule ? true : gf_config_workspace_is_locked (m->config, i);
        ws->max_windows = max_per_ws;
        ws->available_space = ws->is_locked ? 0 : (max_per_ws - ws->window_count);
    }
}

/* --- assign_windows_to_workspaces helpers --- */

static void
create_rule_workspaces (gf_wm_t *m)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    for (uint32_t i = 0; i < m->config->window_rules_count; i++)
    {
        gf_ws_id_t rule_ws = m->config->window_rules[i].workspace_id;
        gf_workspace_list_ensure (workspaces, rule_ws, max_per_ws);
    }
}

static void
preserve_existing_assignments (gf_wm_t *m)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_win_list_t *windows = wm_windows (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *win = &windows->items[i];
        if (!win->is_valid)
            continue;
        if (win_has_assigned_workspace (win, workspaces))
            gf_workspace_list_ensure (workspaces, win->workspace_id, max_per_ws);
    }
}

static void
advance_ws_slot (uint32_t *ws_id, uint32_t *slot, uint32_t max_per_ws)
{
    (*ws_id)++;
    if (*slot >= max_per_ws)
        *slot = 0;
}

static void
assign_unassigned_windows (gf_wm_t *m, uint32_t max_per_ws)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_win_list_t *windows = wm_windows (m);
    uint32_t ws_id = GF_FIRST_WORKSPACE_ID;
    uint32_t slot = 0;

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *win = &windows->items[i];

        if (!win->is_valid || win_has_assigned_workspace (win, workspaces))
            continue;

        char class_name[256];
        gf_wm_window_class (m, win->id, class_name, sizeof (class_name));
        const gf_window_rule_t *rule = gf_rules_find (m->config, class_name);
        if (rule)
        {
            win->workspace_id = rule->workspace_id;
            continue;
        }

        while (ws_id < workspaces->count)
        {
            gf_ws_info_t *ws = gf_workspace_list_find_by_id (workspaces, ws_id);
            bool rule_reserved = workspace_is_rule_target (m->config, ws_id);

            if (!ws || ws->is_locked || ws->has_maximized_state || rule_reserved
                || slot >= max_per_ws)
                advance_ws_slot (&ws_id, &slot, max_per_ws);
            else
                break;
        }

        gf_workspace_list_ensure (workspaces, ws_id, max_per_ws);
        win->workspace_id = ws_id;
        slot++;

        if (slot >= max_per_ws)
            advance_ws_slot (&ws_id, &slot, max_per_ws);
    }
}

void
assign_windows_to_workspaces (gf_wm_t *m)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_win_list_t *windows = wm_windows (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    create_rule_workspaces (m);
    preserve_existing_assignments (m);
    assign_unassigned_windows (m, max_per_ws);

    recount_workspace_windows (m, workspaces, windows, max_per_ws);

    for (uint32_t mon_idx = 0; mon_idx < GF_MAX_MONITORS; mon_idx++)
    {
        if (workspaces->active_workspace[mon_idx] >= workspaces->count)
            workspaces->active_workspace[mon_idx] = GF_FIRST_WORKSPACE_ID;
    }

    sync_workspaces (m);
}

gf_ws_id_t
lookup_or_create_maximized_ws (gf_wm_t *m)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        if (workspaces->items[i].has_maximized_state
            && workspaces->items[i].window_count == 0)
            return workspaces->items[i].id;
    }

    return gf_workspace_create (workspaces, m->config->max_windows_per_workspace, true,
                                true);
}

void
cleanup_empty_maximized_ws (gf_wm_t *m, gf_ws_id_t ws_id)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_win_list_t *windows = wm_windows (m);

    for (uint32_t i = 0; i < windows->count; i++)
    {
        if (windows->items[i].is_valid && windows->items[i].workspace_id == ws_id)
            return;
    }

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        if (workspaces->items[i].id == ws_id && workspaces->items[i].has_maximized_state)
        {
            cleanup_unused_workspace (m, workspaces, i);
            GF_LOG_INFO ("Cleaned up empty maximized workspace %d", ws_id);
            break;
        }
    }
}

gf_ws_id_t
lookup_or_create_ws (gf_wm_t *m)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        if (!workspaces->items[i].has_maximized_state
            && workspaces->items[i].available_space > 0 && !workspaces->items[i].has_rule)
            return workspaces->items[i].id;
    }

    return gf_workspace_create (workspaces, m->config->max_windows_per_workspace, false,
                                false);
}

gf_ws_id_t
assign_window_workspace (gf_wm_t *m, gf_win_info_t *win, gf_ws_info_t *current_ws)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    if (platform->window_is_maximized && platform->window_is_maximized (display, win->id))
    {
        win->is_maximized = true;
        return lookup_or_create_maximized_ws (m);
    }

    if (current_ws && !current_ws->has_maximized_state && current_ws->available_space > 0)
        return current_ws->id;

    return lookup_or_create_ws (m);
}

/* --- register_new_window helpers --- */

static void
assign_maximized_window (gf_wm_t *m, gf_win_info_t *win)
{
    win->is_maximized = true;
    win->workspace_id = lookup_or_create_maximized_ws (m);
}

static void
apply_rule_to_window (gf_wm_t *m, gf_win_info_t *win, const gf_window_rule_t *rule)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;
    gf_ws_id_t target = rule->workspace_id;
    gf_ws_info_t *target_ws = gf_workspace_list_find_by_id (workspaces, target);

    if (target_ws && target_ws->window_count >= max_per_ws)
    {
        evict_non_rule_window (m, target);
    }
    else if (!target_ws)
    {
        gf_ws_info_t info = { .id = target,
                              .window_count = 1,
                              .max_windows = max_per_ws,
                              .available_space = max_per_ws,
                              .has_rule = true,
                              .has_maximized_state = false,
                              .is_locked = true };
        gf_workspace_list_add (workspaces, &info);
        GF_LOG_INFO ("Creating workspace rule id : %d", target);
    }
    else
    {
        move_window_to_workspace (m, win, target);
        target_ws->has_rule = true;
    }

    win->workspace_id = target;
    GF_LOG_INFO ("Rule matched: %s → workspace %d", "", target);
}

static void
finalize_window_registration (gf_wm_t *m, gf_win_info_t *win)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);

    gf_window_list_mark_all_needs_update (windows, &win->workspace_id);
    m->state.resize_active = false;
    win->is_minimized = false;

    gf_window_list_add (windows, win);
    platform->window_unminimize (display, win->id);

    m->state.last_active_window[win->monitor_id] = win->id;
    m->state.last_active_workspace[win->monitor_id] = win->workspace_id;
    workspaces->active_workspace[win->monitor_id] = win->workspace_id;

    if (m->config->enable_borders && platform->border_add)
        platform->border_add (platform, win->id, m->config->border_color,
                              GF_BORDER_WIDTH);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_ws_id_t ws_id = workspaces->items[i].id;
        if (ws_id != win->workspace_id)
            minimize_workspace_windows (m, ws_id, 0, win->monitor_id);
    }
}

void
register_new_window (gf_wm_t *m, gf_win_info_t *win, gf_ws_info_t *current_ws)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    char class_name[256];
    gf_wm_window_class (m, win->id, class_name, sizeof (class_name));

    if (platform->window_is_maximized && platform->window_is_maximized (display, win->id))
    {
        assign_maximized_window (m, win);
    }
    else
    {
        const gf_window_rule_t *rule = gf_rules_find (m->config, class_name);
        if (rule)
            apply_rule_to_window (m, win, rule);
        else
            win->workspace_id = assign_window_workspace (m, win, current_ws);
    }

    if (current_ws)
        current_ws->is_custom_layout = false;

    GF_LOG_INFO ("New window %p → workspace %u (%s)", (void *)win->id, win->workspace_id,
                 class_name);

    finalize_window_registration (m, win);
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

    recount_workspace_windows (m, workspaces, wm_windows (m),
                               m->config->max_windows_per_workspace);
    sync_workspaces (m);

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

    recount_workspace_windows (m, workspaces, wm_windows (m),
                               m->config->max_windows_per_workspace);
    sync_workspaces (m);

    return GF_SUCCESS;
}
