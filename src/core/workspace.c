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
workspace_is_rule_target (const gf_config_t *cfg, const gf_ws_info_t *ws)
{
    if (!cfg || !ws)
        return false;
    if (ws->has_rule || ws->rule_target_id > 0)
        return true;
    for (uint32_t i = 0; i < cfg->window_rules_count; i++)
    {
        if (cfg->window_rules[i].workspace_id == ws->local_id)
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
evict_non_rule_window (gf_wm_t *m, gf_ws_id_t ws_id, gf_monitor_id_t monitor_id)
{
    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *win = &windows->items[i];
        if (win->workspace_id != ws_id || !win->is_valid || win->monitor_id != monitor_id)
            continue;

        if (window_has_rule (m->config, win->name))
            continue;

        gf_ws_id_t dst_id = -1;
        for (uint32_t j = 0; j < workspaces->count; j++)
        {
            gf_ws_info_t *dst = &workspaces->items[j];
            if (dst->monitor_id != monitor_id || dst->id == ws_id || dst->is_locked
                || dst->has_maximized_state || dst->is_excluded_ws || dst->has_rule)
                continue;
            if (wm_workspace_monitor_window_count (m, dst->id, monitor_id) < max_per_ws)
            {
                dst_id = dst->id;
                break;
            }
        }

        if (dst_id < 0)
            dst_id = gf_workspace_create (workspaces, max_per_ws, false, false,
                                          monitor_id, -1);

        if (dst_id >= 0)
        {
            GF_LOG_INFO ("Evicting window %p (%s) from workspace %d to %d for rule",
                         (void *)win->id, win->name, ws_id, dst_id);
            win->workspace_id = dst_id;
            wm_recount_workspace_windows (m, workspaces, windows, max_per_ws);
        }
        return;
    }
}

gf_ws_info_t *
wm_find_workspace (gf_ws_list_t *workspaces, gf_ws_id_t id)
{
    if (!workspaces || id < GF_FIRST_WORKSPACE_ID)
        return NULL;
    return gf_workspace_list_find_by_id (workspaces, id);
}

void
wm_cleanup_unused_workspace (gf_wm_t *m, gf_ws_list_t *list, uint32_t index)
{
    if (!list || index >= list->count)
        return;

    gf_ws_info_t *workspace = &list->items[index];
    gf_ws_id_t ws_id = workspace->id;
    if (workspace_is_rule_target (m->config, workspace))
        return;

    memmove (&list->items[index], &list->items[index + 1],
             (list->count - index - 1) * sizeof (gf_ws_info_t));

    list->count--;
    memset (&list->items[list->count], 0, sizeof (gf_ws_info_t));
}

bool
wm_ws_is_valid (gf_ws_list_t *workspaces, gf_ws_id_t id)
{
    return wm_find_workspace (workspaces, id) != NULL;
}

void
wm_move_window_to_workspace (gf_wm_t *m, gf_win_info_t *win, gf_ws_id_t new_ws_id)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_win_list_t *windows = wm_windows (m);

    gf_ws_info_t *old = gf_workspace_list_find_by_id (workspaces, win->workspace_id);
    gf_ws_info_t *new = gf_workspace_list_find_by_id (workspaces, new_ws_id);

    if (!old || !new || old == new || new->monitor_id != win->monitor_id)
        return;

    gf_win_info_t *tracked = gf_window_list_find_by_window_id (windows, win->id);
    if (tracked && tracked->workspace_id == old->id)
    {
        tracked->workspace_id = new->id;
        if (old->window_count > 0)
            old->window_count--;
        old->available_space++;
        new->window_count++;
        new->available_space--;
    }

    win->workspace_id = new_ws_id;
}

bool
wm_ws_has_capacity (gf_ws_info_t *ws, uint32_t max_per_ws)
{
    return !ws->is_locked && ws->window_count < max_per_ws;
}

void
wm_recount_workspace_windows (gf_wm_t *m, gf_ws_list_t *workspaces,
                              gf_win_list_t *windows, uint32_t max_per_ws)
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
            if (ws && ws->monitor_id == windows->items[i].monitor_id)
            {
                ws->window_count++;
                ws->available_space--;
            }
        }
    }
}

bool
wm_win_has_assigned_workspace (gf_win_info_t *win, gf_ws_list_t *workspaces)
{
    if (win->workspace_id < GF_FIRST_WORKSPACE_ID)
        return false;
    gf_ws_info_t *ws = gf_workspace_list_find_by_id (workspaces, win->workspace_id);
    return ws && ws->monitor_id == win->monitor_id;
}

void
wm_sync_dock_visibility (gf_wm_t *m)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_win_list_t *windows = wm_windows (m);
    bool should_hide = false;
    bool hide_on_monitor[GF_MAX_MONITORS] = { 0 };

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *win = &windows->items[i];
        if (!win->is_valid || !win->is_maximized || win->is_minimized
            || win->monitor_id >= GF_MAX_MONITORS || wm_is_excluded (m, win->id))
            continue;
        gf_ws_info_t *ws
            = gf_workspace_list_find_by_id (wm_workspaces (m), win->workspace_id);
        if (!ws || ws->monitor_id != win->monitor_id || !ws->has_maximized_state
            || wm_workspaces (m)->active_workspace[win->monitor_id] != ws->id)
            continue;
        if (platform->window_is_minimized
            && platform->window_is_minimized (display, win->id))
            continue;
        hide_on_monitor[win->monitor_id] = true;
        should_hide = true;
    }

    // An excluded foreground app uses the native taskbar on its own monitor,
    // even when a managed maximized workspace remains selected behind it.
    gf_handle_t focused
        = platform->window_get_focused ? platform->window_get_focused (display) : 0;
    if (focused && wm_user_excluded (m, focused))
    {
        gf_monitor_id_t monitor_id = wm_find_active_monitor (m);
        if (monitor_id < GF_MAX_MONITORS)
            hide_on_monitor[monitor_id] = false;
        should_hide = false;
        for (uint32_t i = 0; i < m->state.monitor_count; i++)
            should_hide |= hide_on_monitor[i];
    }

    if (platform->dock_sync)
    {
        platform->dock_sync (platform, hide_on_monitor, m->state.monitor_count);
        if (platform->window_fill_maximized)
        {
            for (uint32_t i = 0; i < windows->count; i++)
            {
                gf_win_info_t *win = &windows->items[i];
                if (!win->is_valid || win->maximize_fill_failures >= 3)
                    continue;
                bool fill_monitor
                    = win->is_maximized && !win->is_minimized
                      && win->monitor_id < m->state.monitor_count
                      && hide_on_monitor[win->monitor_id]
                      && wm_workspaces (m)->active_workspace[win->monitor_id]
                             == win->workspace_id
                      && !wm_is_excluded (m, win->id);
                gf_err_t result
                    = platform->window_fill_maximized (display, win->id, fill_monitor);
                if (result == GF_SUCCESS)
                    win->maximize_fill_failures = 0;
                else
                {
                    win->maximize_fill_failures++;
                    GF_LOG_WARN ("Window %p maximized bounds failed (attempt %u/3)",
                                 (void *)win->id, win->maximize_fill_failures);
                }
            }
        }
        m->state.dock_hidden = should_hide;
        return;
    }

    // These legacy dock callbacks change desktop-wide settings. On extended
    // displays the OS manages each taskbar; a maximize must not change the
    // other monitors' work areas and continuously invalidate their layouts.
    if (m->state.monitor_count > 1)
    {
        if (m->state.dock_hidden && platform->dock_restore)
            platform->dock_restore (platform);
        m->state.dock_hidden = false;
        return;
    }

    if (should_hide == m->state.dock_hidden)
        return;

    if (should_hide && platform->dock_hide)
        platform->dock_hide (platform);
    else if (!should_hide && platform->dock_restore)
        platform->dock_restore (platform);
    else
        return;

    m->state.dock_hidden = should_hide;
    gf_window_list_mark_all_needs_update (windows, NULL);
}

void
wm_switch_workspace (gf_wm_t *m, gf_ws_id_t current_workspace,
                     gf_monitor_id_t active_monitor)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);

    if (active_monitor >= GF_MAX_MONITORS)
        return;

    gf_ws_info_t *target = gf_workspace_list_find_by_id (workspaces, current_workspace);
    if (!target || target->monitor_id != active_monitor || target->is_excluded_ws)
        return;

    if (workspaces->active_workspace[active_monitor] != current_workspace)
        GF_LOG_DEBUG ("Monitor %u workspace changed from %d to %d", active_monitor,
                      workspaces->active_workspace[active_monitor], current_workspace);

    workspaces->active_workspace[active_monitor] = current_workspace;
    m->state.last_active_workspace[active_monitor] = current_workspace;

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_ws_id_t ws_id = workspaces->items[i].id;
        if (workspaces->items[i].monitor_id != active_monitor)
            continue;
        if (ws_id == current_workspace)
            continue;
        wm_minimize_workspace_windows (m, ws_id, 0, active_monitor);
    }

    gf_platform_t *platform = wm_platform (m);
    gf_handle_t active_window = 0;
    if (platform->window_get_focused)
        active_window = platform->window_get_focused (*wm_display (m));

    wm_restore_workspace_windows (m, current_workspace, active_window, active_monitor);

    wm_sync_dock_visibility (m);
}

/* Ensure every rule's target workspace exists and is flagged has_rule=true
 * immediately, rather than waiting for its app to actually appear. Without
 * this, a workspace reserved for a rule looks like ordinary free space to
 * wm_lookup_or_create_ws/gf_workspace_list_find_free until the first matching
 * window is placed, letting an unrelated window claim it first. */
static void
create_rule_workspaces (gf_wm_t *m)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;
    uint32_t monitor_count = m->state.monitor_count;

    if (monitor_count == 0 && wm_platform (m)->monitor_get_count)
        monitor_count = wm_platform (m)->monitor_get_count (wm_platform (m));
    if (monitor_count == 0)
        monitor_count = 1;
    if (monitor_count > GF_MAX_MONITORS)
        monitor_count = GF_MAX_MONITORS;

    for (uint32_t i = 0; i < m->config->window_rules_count; i++)
    {
        gf_ws_id_t local_id = m->config->window_rules[i].workspace_id;
        for (gf_monitor_id_t monitor_id = 0; monitor_id < monitor_count; monitor_id++)
        {
            gf_ws_id_t rule_ws_id
                = gf_workspace_id_for_monitor_local (monitor_id, local_id);
            if (rule_ws_id < GF_FIRST_WORKSPACE_ID)
                continue;

            gf_workspace_list_ensure (workspaces, rule_ws_id, max_per_ws, monitor_id,
                                      local_id);
            gf_ws_info_t *ws = gf_workspace_list_find_by_id (workspaces, rule_ws_id);
            if (ws && ws->monitor_id == monitor_id)
            {
                ws->has_rule = true;
                ws->rule_target_id = local_id;
                ws->is_locked = true;
            }
        }
    }
}

void
wm_sync_workspaces (gf_wm_t *m)
{
    if (!m || !m->config)
        return;

    create_rule_workspaces (m);

    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    uint32_t platform_count = platform->workspace_get_count (display);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    uint32_t monitor_count = m->state.monitor_count;
    if (monitor_count == 0 && platform->monitor_get_count)
        monitor_count = platform->monitor_get_count (platform);
    if (monitor_count == 0)
        monitor_count = 1;
    if (monitor_count > GF_MAX_MONITORS)
        monitor_count = GF_MAX_MONITORS;

    for (gf_monitor_id_t monitor_id = 0; monitor_id < monitor_count; monitor_id++)
    {
        for (gf_ws_id_t local_id = GF_FIRST_WORKSPACE_ID;
             local_id <= (gf_ws_id_t)platform_count; local_id++)
        {
            if (local_id > GF_MAX_WORKSPACES)
                break;
            gf_ws_id_t workspace_id
                = gf_workspace_id_for_monitor_local (monitor_id, local_id);
            gf_workspace_list_ensure (workspaces, workspace_id, max_per_ws, monitor_id,
                                      local_id);
            gf_ws_info_t *ws = gf_workspace_list_find_by_id (workspaces, workspace_id);
            if (!ws || ws->monitor_id != monitor_id)
                continue;

            ws->is_locked
                = ws->has_rule ? true : gf_config_workspace_is_locked (m->config, ws->id);
            ws->max_windows = max_per_ws;
            ws->available_space = ws->is_locked || ws->window_count >= max_per_ws
                                      ? 0
                                      : (int32_t)(max_per_ws - ws->window_count);
        }

        gf_ws_id_t active_id = workspaces->active_workspace[monitor_id];
        gf_ws_info_t *active = gf_workspace_list_find_by_id (workspaces, active_id);
        if (!active || active->monitor_id != monitor_id)
        {
            gf_ws_info_t *first = gf_workspace_list_find_by_monitor_local (
                workspaces, monitor_id, GF_FIRST_WORKSPACE_ID);
            if (first && !first->has_maximized_state && !first->is_excluded_ws)
                workspaces->active_workspace[monitor_id] = first->id;
        }
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
        if (wm_win_has_assigned_workspace (win, workspaces))
            continue;

        gf_ws_info_t *old_ws
            = gf_workspace_list_find_by_id (workspaces, win->workspace_id);
        const gf_window_rule_t *rule = gf_rules_find (m->config, win->name);
        gf_ws_id_t target;
        if (rule)
        {
            target
                = gf_workspace_id_for_monitor_local (win->monitor_id, rule->workspace_id);
            gf_workspace_list_ensure (workspaces, target, max_per_ws, win->monitor_id,
                                      rule->workspace_id);
        }
        else if (old_ws && old_ws->has_maximized_state)
            target = wm_lookup_or_create_maximized_ws (m, win->monitor_id);
        else if (old_ws && old_ws->is_excluded_ws)
            target = wm_lookup_or_create_excluded_ws (m, win->monitor_id);
        else
            target = wm_lookup_or_create_ws_for_monitor (m, win->monitor_id);
        if (target > 0)
            win->workspace_id = target;
        else
            gf_workspace_list_ensure (workspaces,
                                      gf_workspace_id_for_monitor_local (
                                          win->monitor_id, GF_FIRST_WORKSPACE_ID),
                                      max_per_ws, win->monitor_id, GF_FIRST_WORKSPACE_ID);
    }
}

static void
assign_unassigned_windows (gf_wm_t *m)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_win_list_t *windows = wm_windows (m);
    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *win = &windows->items[i];

        if (!win->is_valid || wm_win_has_assigned_workspace (win, workspaces))
            continue;

        const gf_window_rule_t *rule = gf_rules_find (m->config, win->name);
        if (rule)
        {
            gf_ws_id_t target_id
                = gf_workspace_id_for_monitor_local (win->monitor_id, rule->workspace_id);
            gf_workspace_list_ensure (workspaces, target_id,
                                      m->config->max_windows_per_workspace,
                                      win->monitor_id, rule->workspace_id);
            win->workspace_id = target_id;
            continue;
        }
        win->workspace_id = wm_lookup_or_create_ws_for_monitor (m, win->monitor_id);
    }
}

void
wm_assign_windows_to_workspaces (gf_wm_t *m)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_win_list_t *windows = wm_windows (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    create_rule_workspaces (m);
    preserve_existing_assignments (m);
    assign_unassigned_windows (m);

    wm_recount_workspace_windows (m, workspaces, windows, max_per_ws);

    for (uint32_t mon_idx = 0; mon_idx < GF_MAX_MONITORS; mon_idx++)
    {
        gf_ws_info_t *active = gf_workspace_list_find_by_id (
            workspaces, workspaces->active_workspace[mon_idx]);
        if (!active || active->monitor_id != mon_idx)
        {
            gf_ws_info_t *first = gf_workspace_list_find_by_monitor_local (
                workspaces, mon_idx, GF_FIRST_WORKSPACE_ID);
            workspaces->active_workspace[mon_idx]
                = first ? first->id
                        : gf_workspace_id_for_monitor_local (mon_idx,
                                                             GF_FIRST_WORKSPACE_ID);
        }
    }

    wm_sync_workspaces (m);
}

gf_ws_id_t
wm_lookup_or_create_maximized_ws (gf_wm_t *m, gf_monitor_id_t monitor_id)
{
    if (monitor_id >= GF_MAX_MONITORS)
        monitor_id = 0;
    gf_ws_list_t *workspaces = wm_workspaces (m);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        if (workspaces->items[i].has_maximized_state
            && workspaces->items[i].monitor_id == monitor_id
            && workspaces->items[i].window_count == 0)
            return workspaces->items[i].id;
    }

    return gf_workspace_create (workspaces, m->config->max_windows_per_workspace, true,
                                true, monitor_id, 0);
}

void
wm_cleanup_empty_maximized_ws (gf_wm_t *m, gf_ws_id_t ws_id)
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
            wm_cleanup_unused_workspace (m, workspaces, i);
            GF_LOG_INFO ("Cleaned up empty maximized workspace %d", ws_id);
            break;
        }
    }
}

gf_ws_id_t
wm_lookup_or_create_ws (gf_wm_t *m)
{
    return wm_lookup_or_create_ws_for_monitor (m, wm_find_active_monitor (m));
}

uint32_t
wm_workspace_monitor_window_count (gf_wm_t *m, gf_ws_id_t workspace_id,
                                   gf_monitor_id_t monitor_id)
{
    gf_ws_info_t *ws = gf_workspace_list_find_by_id (wm_workspaces (m), workspace_id);
    if (!ws || ws->monitor_id != monitor_id)
        return 0;

    gf_win_list_t *windows = wm_windows (m);
    uint32_t count = 0;

    for (uint32_t i = 0; i < windows->count; i++)
    {
        const gf_win_info_t *win = &windows->items[i];
        if (win->is_valid && win->workspace_id == workspace_id
            && win->monitor_id == monitor_id && !wm_is_excluded (m, win->id))
            count++;
    }

    return count;
}

gf_ws_id_t
wm_lookup_or_create_ws_for_monitor (gf_wm_t *m, gf_monitor_id_t monitor_id)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;
    gf_ws_id_t active_id
        = monitor_id < GF_MAX_MONITORS ? workspaces->active_workspace[monitor_id] : -1;

    gf_ws_info_t *active = gf_workspace_list_find_by_id (workspaces, active_id);
    if (active && active->monitor_id == monitor_id && !active->is_locked
        && !active->has_maximized_state && !active->is_excluded_ws && !active->has_rule
        && wm_workspace_monitor_window_count (m, active_id, monitor_id) < max_per_ws)
        return active_id;

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_ws_info_t *ws = &workspaces->items[i];
        if (ws->monitor_id == monitor_id && !ws->is_locked && !ws->has_maximized_state
            && !ws->is_excluded_ws && !ws->has_rule
            && wm_workspace_monitor_window_count (m, ws->id, monitor_id) < max_per_ws)
            return ws->id;
    }

    return gf_workspace_create (workspaces, max_per_ws, false, false, monitor_id, -1);
}

void
wm_move_window_to_monitor (gf_wm_t *m, gf_win_info_t *win, gf_monitor_id_t new_monitor)
{
    if (!m || !win || win->monitor_id == new_monitor)
        return;

    if (new_monitor >= GF_MAX_MONITORS)
        return;

    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_monitor_id_t old_monitor = win->monitor_id;
    gf_ws_id_t old_workspace = win->workspace_id;
    gf_ws_info_t *old_ws = gf_workspace_list_find_by_id (workspaces, old_workspace);
    gf_ws_id_t target_workspace = old_workspace;
    gf_ws_id_t old_restore = win->restore_workspace_id;
    gf_platform_t *platform = wm_platform (m);
    gf_handle_t focused_window = platform->window_get_focused
                                     ? platform->window_get_focused (*wm_display (m))
                                     : 0;
    bool window_is_focused = focused_window == win->id && !win->is_minimized;
    bool activates_monitor
        = window_is_focused || (win->is_maximized && !win->is_minimized);
    if (platform->border_remove)
        platform->border_remove (platform, win->id);
    win->monitor_id = new_monitor;

    if (win->is_maximized || (old_ws && old_ws->has_maximized_state))
    {
        target_workspace = wm_lookup_or_create_maximized_ws (m, new_monitor);
        const gf_window_rule_t *rule = gf_rules_find (m->config, win->name);
        if (rule)
        {
            win->restore_workspace_id
                = gf_workspace_id_for_monitor_local (new_monitor, rule->workspace_id);
            gf_workspace_list_ensure (workspaces, win->restore_workspace_id,
                                      m->config->max_windows_per_workspace, new_monitor,
                                      rule->workspace_id);
        }
        else
        {
            gf_ws_info_t *restore
                = gf_workspace_list_find_by_id (workspaces, old_restore);
            if (restore && restore->monitor_id == new_monitor
                && !restore->has_maximized_state && !restore->is_excluded_ws)
                win->restore_workspace_id = old_restore;
            else
                win->restore_workspace_id
                    = wm_lookup_or_create_ws_for_monitor (m, new_monitor);
        }
    }
    else if (old_ws && old_ws->is_excluded_ws)
        target_workspace = wm_lookup_or_create_excluded_ws (m, new_monitor);
    else if (old_ws && old_ws->rule_target_id > 0)
    {
        target_workspace
            = gf_workspace_id_for_monitor_local (new_monitor, old_ws->rule_target_id);
        gf_workspace_list_ensure (workspaces, target_workspace,
                                  m->config->max_windows_per_workspace, new_monitor,
                                  old_ws->rule_target_id);
    }
    else
        target_workspace = wm_lookup_or_create_ws_for_monitor (m, new_monitor);

    if (target_workspace != old_workspace)
        wm_move_window_to_workspace (m, win, target_workspace);

    // Native title-bar dragging can temporarily restore a maximized app, or
    // snap a normal app to maximized. Keep the mode it had on the source monitor.
    wm_request_maximized (m, win);

    gf_ws_info_t *target_ws = gf_workspace_list_find_by_id (workspaces, target_workspace);
    bool parked_excluded = target_ws && target_ws->is_excluded_ws;
    if (activates_monitor && target_ws && target_ws->monitor_id == new_monitor
        && !parked_excluded)
    {
        gf_ws_id_t previous_active = workspaces->active_workspace[new_monitor];
        if (previous_active != target_workspace)
        {
            wm_switch_workspace (m, target_workspace, new_monitor);
        }
        workspaces->active_workspace[new_monitor] = target_workspace;
        m->state.last_active_workspace[new_monitor] = target_workspace;
        m->state.last_active_window[new_monitor] = win->id;
    }
    else if (target_ws && target_ws->monitor_id == new_monitor && !parked_excluded
             && target_workspace != workspaces->active_workspace[new_monitor]
             && platform->window_minimize)
    {
        wm_request_visibility (m, win, true);
    }

    gf_window_list_mark_all_needs_update (wm_windows (m), &old_workspace);
    if (target_workspace != old_workspace)
        gf_window_list_mark_all_needs_update (wm_windows (m), &target_workspace);

    old_ws = gf_workspace_list_find_by_id (workspaces, old_workspace);
    gf_ws_info_t *new_ws = gf_workspace_list_find_by_id (workspaces, target_workspace);
    if (old_ws)
        old_ws->is_custom_layout = false;
    if (new_ws)
        new_ws->is_custom_layout = false;

    // A maximized workspace vacated by a transfer must reveal its own
    // monitor's normal workspace, without activating anything on the destination.
    if (old_monitor < GF_MAX_MONITORS
        && workspaces->active_workspace[old_monitor] == old_workspace && old_ws
        && old_ws->has_maximized_state
        && wm_workspace_monitor_window_count (m, old_workspace, old_monitor) == 0)
    {
        gf_ws_info_t *restore = gf_workspace_list_find_by_id (workspaces, old_restore);
        gf_ws_id_t source_target
            = restore && restore->monitor_id == old_monitor
                      && !restore->has_maximized_state && !restore->is_excluded_ws
                  ? restore->id
                  : wm_lookup_or_create_ws_for_monitor (m, old_monitor);
        wm_switch_workspace (m, source_target, old_monitor);
        m->state.last_active_window[old_monitor] = 0;
    }
    wm_cleanup_empty_maximized_ws (m, old_workspace);

    // The border reconciler uses the current physical monitor and workspace.
    if (activates_monitor)
        wm_sync_monitor_activity (m, new_monitor);

    if (!win->is_maximized && !win->is_minimized && m->config->enable_borders
        && platform->border_add
        && workspaces->active_workspace[new_monitor] == target_workspace)
        platform->border_add (platform, win->id, m->config->border_color,
                              GF_BORDER_WIDTH);
}

// The single workspace that parks user-excluded windows. Created locked so
// assignment never places normal windows on it.
gf_ws_id_t
wm_lookup_or_create_excluded_ws (gf_wm_t *m, gf_monitor_id_t monitor_id)
{
    if (monitor_id >= GF_MAX_MONITORS)
        monitor_id = 0;
    gf_ws_list_t *workspaces = wm_workspaces (m);

    for (uint32_t i = 0; i < workspaces->count; i++)
        if (workspaces->items[i].is_excluded_ws
            && workspaces->items[i].monitor_id == monitor_id)
            return workspaces->items[i].id;

    gf_ws_id_t id = gf_workspace_create (workspaces, m->config->max_windows_per_workspace,
                                         false, true, monitor_id, 0);
    gf_ws_info_t *ws = gf_workspace_list_find_by_id (workspaces, id);
    if (ws)
        ws->is_excluded_ws = true;
    return id;
}

gf_ws_id_t
wm_assign_window_workspace (gf_wm_t *m, gf_win_info_t *win, gf_ws_info_t *current_ws)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    if (platform->window_is_maximized && platform->window_is_maximized (display, win->id))
    {
        win->is_maximized = true;
        return wm_lookup_or_create_maximized_ws (m, win->monitor_id);
    }

    if (current_ws && current_ws->monitor_id == win->monitor_id && !current_ws->is_locked
        && !current_ws->has_rule && !current_ws->has_maximized_state
        && !current_ws->is_excluded_ws
        && wm_workspace_monitor_window_count (m, current_ws->id, win->monitor_id)
               < m->config->max_windows_per_workspace)
        return current_ws->id;

    return wm_lookup_or_create_ws_for_monitor (m, win->monitor_id);
}

static void
assign_maximized_window (gf_wm_t *m, gf_win_info_t *win)
{
    win->is_maximized = true;
    gf_monitor_id_t monitor = win->monitor_id;
    if (monitor < GF_MAX_MONITORS)
    {
        gf_ws_id_t origin = wm_workspaces (m)->active_workspace[monitor];
        gf_ws_info_t *origin_ws
            = gf_workspace_list_find_by_id (wm_workspaces (m), origin);
        if (!win->restore_workspace_id && origin_ws && !origin_ws->has_maximized_state)
            win->restore_workspace_id = origin;
    }
    win->workspace_id = wm_lookup_or_create_maximized_ws (m, win->monitor_id);

    // Claim the slot immediately: the window isn't in the tracked list yet
    // (that happens later in finalize_window_registration), so
    // wm_recount_workspace_windows won't see it until the end of this tick.
    // Without this, several already-maximized windows discovered in the
    // same tick would all see window_count == 0 and pile onto one workspace.
    gf_ws_info_t *ws
        = gf_workspace_list_find_by_id (wm_workspaces (m), win->workspace_id);
    if (ws)
        ws->window_count++;
}

static void
apply_rule_to_window (gf_wm_t *m, gf_win_info_t *win, const gf_window_rule_t *rule)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;
    gf_ws_id_t target
        = gf_workspace_id_for_monitor_local (win->monitor_id, rule->workspace_id);
    if (target < GF_FIRST_WORKSPACE_ID)
        return;
    gf_workspace_list_ensure (workspaces, target, max_per_ws, win->monitor_id,
                              rule->workspace_id);
    gf_ws_info_t *target_ws = gf_workspace_list_find_by_id (workspaces, target);
    if (!target_ws || target_ws->monitor_id != win->monitor_id)
        return;

    if (wm_workspace_monitor_window_count (m, target, win->monitor_id) >= max_per_ws)
    {
        evict_non_rule_window (m, target, win->monitor_id);
    }
    else
    {
        if (win->workspace_id != target)
            wm_move_window_to_workspace (m, win, target);
        target_ws->has_rule = true;
        target_ws->rule_target_id = rule->workspace_id;
        target_ws->is_locked = true;
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

    gf_handle_t focused_window
        = platform->window_get_focused ? platform->window_get_focused (display) : 0;
    bool is_focused = focused_window == win->id;
    bool activates_monitor = is_focused;
    gf_ws_info_t *assigned_ws
        = gf_workspace_list_find_by_id (workspaces, win->workspace_id);
    bool belongs_to_active_workspace
        = win->monitor_id < GF_MAX_MONITORS && assigned_ws
          && assigned_ws->monitor_id == win->monitor_id
          && workspaces->active_workspace[win->monitor_id] == win->workspace_id;
    bool should_be_visible
        = activates_monitor || (belongs_to_active_workspace && !win->is_maximized);

    if (should_be_visible)
    {
        wm_request_visibility (m, win, false);
    }
    else
    {
        wm_request_visibility (m, win, true);
    }

    gf_window_list_add (windows, win);

    if (activates_monitor && win->monitor_id < GF_MAX_MONITORS)
    {
        m->state.last_active_window[win->monitor_id] = win->id;
        m->state.last_active_workspace[win->monitor_id] = win->workspace_id;
        workspaces->active_workspace[win->monitor_id] = win->workspace_id;
    }

    if (win->is_maximized)
    {
        if (platform->border_remove)
            platform->border_remove (platform, win->id);
    }
    else if (m->config->enable_borders && platform->border_add)
    {
        if (win->monitor_id < GF_MAX_MONITORS
            && workspaces->active_workspace[win->monitor_id] == win->workspace_id)
            platform->border_add (platform, win->id, m->config->border_color,
                                  GF_BORDER_WIDTH);
    }

    for (uint32_t i = 0; activates_monitor && i < workspaces->count; i++)
    {
        gf_ws_id_t ws_id = workspaces->items[i].id;
        if (ws_id != win->workspace_id)
            wm_minimize_workspace_windows (m, ws_id, 0, win->monitor_id);
    }

    wm_sync_dock_visibility (m);
}

void
wm_register_new_window (gf_wm_t *m, gf_win_info_t *win, gf_ws_info_t *current_ws)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);

    if (!current_ws && win->monitor_id < GF_MAX_MONITORS)
        current_ws = gf_workspace_list_find_by_id (
            workspaces, workspaces->active_workspace[win->monitor_id]);
    if (!current_ws || current_ws->monitor_id != win->monitor_id)
    {
        gf_ws_id_t first_id
            = gf_workspace_id_for_monitor_local (win->monitor_id, GF_FIRST_WORKSPACE_ID);
        gf_workspace_list_ensure (workspaces, first_id,
                                  m->config->max_windows_per_workspace, win->monitor_id,
                                  GF_FIRST_WORKSPACE_ID);
        current_ws = gf_workspace_list_find_by_id (workspaces, first_id);
    }
    gf_ws_id_t current_ws_id = current_ws ? current_ws->id : -1;

    gf_wm_resolve_window_name (m, win->id, NULL, win->name, sizeof (win->name));

    if (wm_user_excluded (m, win->id))
    {
        win->workspace_id = wm_lookup_or_create_excluded_ws (m, win->monitor_id);
        win->restore_workspace_id = 0;
        win->is_maximized = false;
        gf_window_list_add (wm_windows (m), win);
        if (platform->border_remove)
            platform->border_remove (platform, win->id);
        return;
    }

    if (platform->window_is_maximized && platform->window_is_maximized (display, win->id))
    {
        if (!win->restore_workspace_id && current_ws && !current_ws->has_maximized_state)
            win->restore_workspace_id = current_ws->id;
        assign_maximized_window (m, win);
    }
    else
    {
        const gf_window_rule_t *rule = gf_rules_find (m->config, win->name);
        if (rule)
            apply_rule_to_window (m, win, rule);
        else
            win->workspace_id = wm_assign_window_workspace (m, win, current_ws);
    }

    current_ws = gf_workspace_list_find_by_id (workspaces, current_ws_id);
    if (current_ws)
        current_ws->is_custom_layout = false;

    GF_LOG_INFO ("New window %p → workspace %u (%s)", (void *)win->id, win->workspace_id,
                 win->name);

    finalize_window_registration (m, win);
}

gf_err_t
gf_wm_workspace_lock (gf_wm_t *m, gf_ws_id_t workspace_id)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_ws_list_t *workspaces = wm_workspaces (m);

    if (workspace_id < GF_FIRST_WORKSPACE_ID)
        return GF_ERROR_INVALID_PARAMETER;

    gf_ws_info_t *ws = gf_workspace_list_find_by_id (workspaces, workspace_id);
    if (!ws)
    {
        gf_monitor_id_t monitor_id = wm_find_active_monitor (m);
        gf_ws_id_t local_id = workspace_id;
        if (local_id > GF_MAX_WORKSPACES)
            return GF_ERROR_INVALID_PARAMETER;
        gf_ws_id_t actual_id = gf_workspace_id_for_monitor_local (monitor_id, local_id);
        gf_workspace_list_ensure (workspaces, actual_id,
                                  m->config->max_windows_per_workspace, monitor_id,
                                  local_id);
        ws = gf_workspace_list_find_by_id (workspaces, actual_id);
    }
    if (!ws)
        return GF_ERROR_INVALID_PARAMETER;

    if (ws->local_id < GF_FIRST_WORKSPACE_ID
        || ws->local_id > (gf_ws_id_t)m->config->max_workspaces)
        return GF_ERROR_INVALID_PARAMETER;

    if (ws->is_locked)
        return GF_ERROR_ALREADY_LOCKED;

    gf_err_t lock_result = gf_config_workspace_lock (m->config, ws->id);
    if (lock_result != GF_SUCCESS)
        return lock_result;
    ws->is_locked = true;

    wm_recount_workspace_windows (m, workspaces, wm_windows (m),
                                  m->config->max_windows_per_workspace);
    wm_sync_workspaces (m);

    return GF_SUCCESS;
}

gf_err_t
gf_wm_workspace_unlock (gf_wm_t *m, gf_ws_id_t workspace_id)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_ws_list_t *workspaces = wm_workspaces (m);

    if (workspace_id < GF_FIRST_WORKSPACE_ID)
        return GF_ERROR_INVALID_PARAMETER;

    gf_ws_info_t *ws = gf_workspace_list_find_by_id (workspaces, workspace_id);
    if (!ws)
    {
        gf_monitor_id_t monitor_id = wm_find_active_monitor (m);
        gf_ws_id_t local_id = workspace_id;
        if (local_id > GF_MAX_WORKSPACES)
            return GF_ERROR_INVALID_PARAMETER;
        gf_ws_id_t actual_id = gf_workspace_id_for_monitor_local (monitor_id, local_id);
        gf_workspace_list_ensure (workspaces, actual_id,
                                  m->config->max_windows_per_workspace, monitor_id,
                                  local_id);
        ws = gf_workspace_list_find_by_id (workspaces, actual_id);
    }
    if (!ws)
        return GF_ERROR_INVALID_PARAMETER;

    if (ws->local_id < GF_FIRST_WORKSPACE_ID
        || ws->local_id > (gf_ws_id_t)m->config->max_workspaces)
        return GF_ERROR_INVALID_PARAMETER;

    if (!ws->is_locked)
        return GF_ERROR_ALREADY_UNLOCKED;

    gf_err_t unlock_result = gf_config_workspace_unlock (m->config, ws->id);
    if (unlock_result != GF_SUCCESS)
        return unlock_result;
    ws->is_locked = false;

    wm_recount_workspace_windows (m, workspaces, wm_windows (m),
                                  m->config->max_windows_per_workspace);
    wm_sync_workspaces (m);

    return GF_SUCCESS;
}
