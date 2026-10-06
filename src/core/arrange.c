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

gf_err_t
gf_wm_calculate_layout (gf_wm_t *m, gf_win_info_t *windows, uint32_t window_count,
                        gf_monitor_id_t mon_id, gf_rect_t **out_geometries)
{
    if (!m || !windows || !out_geometries || window_count == 0)
        return GF_ERROR_INVALID_PARAMETER;

    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_rect_t workspace_bounds;

    if (platform->screen_get_bounds_for_monitor)
    {
        gf_err_t result = platform->screen_get_bounds_for_monitor (display, mon_id,
                                                                   &workspace_bounds);
        if (result != GF_SUCCESS)
            return GF_ERROR_DISPLAY_CONNECTION;
    }
    else
    {
        gf_err_t result = platform->screen_get_bounds (display, &workspace_bounds);
        if (result != GF_SUCCESS)
            return GF_ERROR_DISPLAY_CONNECTION;
    }

    gf_rect_t *new_geometries = gf_malloc (window_count * sizeof (gf_rect_t));
    if (!new_geometries)
        return GF_ERROR_MEMORY_ALLOCATION;

    wm_geometry (m)->apply_layout (wm_geometry (m), windows, window_count,
                                   &workspace_bounds, new_geometries);

    *out_geometries = new_geometries;
    return GF_SUCCESS;
}

static uint32_t
calc_ws_max_wins (const gf_rect_t *bounds, const gf_config_t *config)
{
    uint32_t min_size
        = config->min_window_size ? config->min_window_size : GF_MIN_WINDOW_SIZE;
    uint32_t cols = bounds->width / min_size;
    uint32_t rows = bounds->height / min_size;
    uint32_t auto_max = cols * rows;
    if (auto_max < 1)
        auto_max = 1;

    uint32_t config_max = config->max_windows_per_workspace;
    return (config_max < auto_max) ? config_max : auto_max;
}

void
gf_wm_apply_layout (gf_wm_t *m, gf_win_info_t *windows, gf_rect_t *geometry,
                    uint32_t window_count)
{
    if (!m || !windows || !geometry || window_count == 0)
        return;

    gf_win_list_t *window_list = wm_windows (m);
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    for (uint32_t i = 0; i < window_count; i++)
    {
        if (wm_is_excluded (m, windows[i].id))
            continue;
        if (!windows[i].needs_update)
            continue;
        if (windows[i].is_minimized || !windows[i].is_valid)
            continue;

        gf_win_info_t *tracked
            = gf_window_list_find_by_window_id (window_list, windows[i].id);
        gf_ws_info_t *ws
            = gf_workspace_list_find_by_id (wm_workspaces (m), windows[i].workspace_id);
        if (!tracked || !ws || ws->monitor_id != tracked->monitor_id
            || ws->has_maximized_state || tracked->is_maximized
            || tracked->monitor_id >= GF_MAX_MONITORS
            || wm_workspaces (m)->active_workspace[tracked->monitor_id] != ws->id
            || (platform->window_is_maximized
                && platform->window_is_maximized (display, tracked->id))
            || (platform->window_is_minimized
                && platform->window_is_minimized (display, tracked->id)))
            continue;

        gf_err_t result = platform->window_set_geometry (
            display, windows[i].id, &geometry[i], GF_GEOMETRY_CHANGE_ALL, m->config);

        if (result != GF_SUCCESS)
        {
            if (++tracked->arrange_failures >= 3)
                tracked->needs_update = false;
            GF_LOG_WARN ("Failed to set geometry for window %p (attempt %u/3)",
                         (void *)windows[i].id, tracked->arrange_failures);
            continue;
        }

        if (gf_wm_window_sync (m, windows[i].id, windows[i].workspace_id) == GF_SUCCESS)
        {
            gf_win_info_t *updated
                = gf_window_list_find_by_window_id (window_list, windows[i].id);
            if (updated)
            {
                updated->needs_update = false;
                updated->arrange_failures = 0;
            }
        }
    }
}

static uint32_t
enumerate_monitors (gf_platform_t *platform, gf_display_t display, gf_monitor_t *monitors)
{
    uint32_t count = 0;
    if (platform->monitor_enumerate)
    {
        count = GF_MAX_MONITORS;
        if (platform->monitor_enumerate (platform, monitors, &count) != GF_SUCCESS)
            count = 0;
    }

    if (count == 0)
    {
        count = 1;
        monitors[0].id = 0;
        monitors[0].is_primary = true;
        platform->screen_get_bounds (display, &monitors[0].bounds);
        monitors[0].full_bounds = monitors[0].bounds;
    }

    return count;
}

static void
filter_monitor_windows (gf_win_info_t *ws_wins, uint32_t ws_count, gf_monitor_t *mon,
                        uint32_t monitor_count, gf_win_info_t *out, uint32_t *out_count,
                        gf_wm_t *m, gf_ws_info_t *ws)
{
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    *out_count = 0;
    for (uint32_t j = 0; j < ws_count; j++)
    {
        // Consult the live maximized state, not just the tracked flag: a window
        // maximized this tick would otherwise be re-tiled (un-maximized) before
        // gf_wm_event detects the transition.
        bool maximized = ws_wins[j].is_maximized
                         || (platform->window_is_maximized
                             && platform->window_is_maximized (display, ws_wins[j].id));
        if (maximized && !ws->has_maximized_state)
            continue;
        if (ws_wins[j].is_minimized || wm_is_excluded (m, ws_wins[j].id))
            continue;
        if (ws_wins[j].monitor_id != mon->id)
            continue;
        out[(*out_count)++] = ws_wins[j];
    }
}

static void
apply_layout_to_monitor (gf_wm_t *m, gf_ws_info_t *ws, gf_monitor_t *mon,
                         gf_win_info_t *ws_wins, uint32_t ws_count,
                         uint32_t monitor_count)
{
    gf_platform_t *platform = wm_platform (m);

    gf_win_info_t *mon_wins = gf_malloc (ws_count * sizeof (gf_win_info_t));
    if (!mon_wins)
        return;

    uint32_t mon_count = 0;
    filter_monitor_windows (ws_wins, ws_count, mon, monitor_count, mon_wins, &mon_count,
                            m, ws);

    if (mon_count > 0)
    {
        gf_rect_t *new_geoms = NULL;
        if (gf_wm_calculate_layout (m, mon_wins, mon_count, mon->id, &new_geoms)
            == GF_SUCCESS)
        {
            gf_wm_apply_layout (m, mon_wins, new_geoms, mon_count);
            if (m->config->enable_borders && platform->border_update)
                platform->border_update (platform, m->config);
            gf_free (new_geoms);
        }
    }

    gf_free (mon_wins);
}

static void
apply_layout_to_workspace (gf_wm_t *m, gf_ws_info_t *ws, gf_monitor_t *monitors,
                           uint32_t monitor_count)
{
    gf_win_list_t *windows = wm_windows (m);
    gf_win_info_t *ws_wins = NULL;
    uint32_t ws_count = 0;

    bool pending = false;
    for (uint32_t i = 0; i < windows->count; i++)
        if (windows->items[i].workspace_id == ws->id && windows->items[i].needs_update)
            pending = true;
    if (!pending)
        return;

    if (gf_window_list_get_by_workspace (windows, ws->id, &ws_wins, &ws_count)
            != GF_SUCCESS
        || ws_count == 0)
    {
        gf_free (ws_wins);
        return;
    }

    for (uint32_t mon_idx = 0; mon_idx < monitor_count; mon_idx++)
    {
        if (monitors[mon_idx].id != ws->monitor_id)
            continue;
        apply_layout_to_monitor (m, ws, &monitors[mon_idx], ws_wins, ws_count,
                                 monitor_count);
        break;
    }

    gf_free (ws_wins);
}

gf_err_t
gf_wm_layout_apply (gf_wm_t *m)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    if (m->state.resize_active)
        return GF_SUCCESS;

    if (m->platform->window_is_interacting
        && m->platform->window_is_interacting (m->display))
        return GF_SUCCESS;

    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_win_list_t *windows = wm_windows (m);
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    if (windows->count == 0 || !windows->items)
        return GF_SUCCESS;

    wm_assign_windows_to_workspaces (m);

    gf_monitor_t monitors[GF_MAX_MONITORS];
    uint32_t monitor_count = enumerate_monitors (platform, display, monitors);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_ws_info_t *ws = &workspaces->items[i];
        if (!ws->has_maximized_state && !ws->is_excluded_ws && !ws->is_custom_layout
            && ws->monitor_id < GF_MAX_MONITORS
            && workspaces->active_workspace[ws->monitor_id] == ws->id)
            apply_layout_to_workspace (m, ws, monitors, monitor_count);
    }

    return GF_SUCCESS;
}

static bool
workspace_can_receive_monitor (gf_wm_t *m, gf_ws_info_t *ws, gf_monitor_id_t monitor_id)
{
    return ws && ws->monitor_id == monitor_id && !ws->is_locked
           && !ws->has_maximized_state && !ws->is_excluded_ws && !ws->has_rule
           && gf_window_list_count_by_workspace_monitor (wm_windows (m), ws->id,
                                                         monitor_id)
                  < m->config->max_windows_per_workspace;
}

static gf_ws_id_t
find_overflow_target (gf_wm_t *m, gf_ws_id_t source_id, gf_monitor_id_t monitor_id)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    gf_ws_id_t active_id
        = monitor_id < GF_MAX_MONITORS ? workspaces->active_workspace[monitor_id] : -1;
    gf_ws_info_t *active_ws = gf_workspace_list_find_by_id (workspaces, active_id);
    if (active_id != source_id
        && workspace_can_receive_monitor (m, active_ws, monitor_id))
        return active_id;

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_ws_info_t *ws = &workspaces->items[i];
        if (ws->id != source_id && workspace_can_receive_monitor (m, ws, monitor_id))
            return ws->id;
    }

    return gf_workspace_create (workspaces, max_per_ws, false, false, monitor_id, -1);
}

static bool
relocate_overflow_window (gf_wm_t *m, gf_ws_id_t source_id, gf_ws_id_t target_id,
                          gf_monitor_id_t monitor_id)
{
    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *win = &windows->items[i];
        if (!win->is_valid || win->workspace_id != source_id
            || win->monitor_id != monitor_id || wm_is_excluded (m, win->id))
            continue;

        GF_LOG_INFO ("Move window %p from workspace %u to workspace %u", (void *)win->id,
                     source_id, target_id);
        gf_ws_info_t *source_ws = gf_workspace_list_find_by_id (workspaces, source_id);
        gf_ws_info_t *target_ws = gf_workspace_list_find_by_id (workspaces, target_id);
        if (!source_ws || !target_ws)
            return false;
        win->workspace_id = target_id;
        if (source_ws->window_count > 0)
            source_ws->window_count--;
        source_ws->available_space++;
        target_ws->window_count++;
        target_ws->available_space--;
        gf_window_list_mark_all_needs_update (windows, &source_id);
        gf_window_list_mark_all_needs_update (windows, &target_id);

        source_ws->is_custom_layout = false;
        target_ws->is_custom_layout = false;
        return true;
    }

    return false;
}

static gf_err_t
rebalance_workspace (gf_wm_t *m, gf_ws_id_t source_id, gf_monitor_id_t monitor_id)
{
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    while (
        gf_window_list_count_by_workspace_monitor (wm_windows (m), source_id, monitor_id)
        > max_per_ws)
    {
        gf_ws_id_t target_id = find_overflow_target (m, source_id, monitor_id);
        if (target_id < 0 || target_id == source_id)
        {
            GF_LOG_ERROR ("Failed to find free workspace for overflow");
            return GF_ERROR_INVALID_PARAMETER;
        }

        if (!relocate_overflow_window (m, source_id, target_id, monitor_id))
            break;
    }

    return GF_SUCCESS;
}

gf_err_t
gf_wm_layout_rebalance (gf_wm_t *m)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    if (m->state.resize_active
        || (m->platform->window_is_interacting
            && m->platform->window_is_interacting (m->display)))
        return GF_SUCCESS;

    gf_ws_list_t *workspaces = wm_workspaces (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_ws_id_t source_id = workspaces->items[i].id;
        if (workspaces->items[i].has_maximized_state
            || workspaces->items[i].is_excluded_ws)
            continue;

        for (gf_monitor_id_t monitor = 0; monitor < GF_MAX_MONITORS; monitor++)
        {
            if (gf_window_list_count_by_workspace_monitor (wm_windows (m), source_id,
                                                           monitor)
                > max_per_ws)
                rebalance_workspace (m, source_id, monitor);
        }
    }

    return GF_SUCCESS;
}

void
wm_enforce_fullscreen (gf_wm_t *m)
{
    gf_win_list_t *windows = wm_windows (m);
    gf_handle_t active = m->platform->window_get_focused (m->display);

    if (active == 0 || wm_is_excluded (m, active)
        || !m->platform->window_is_fullscreen (m->display, (gf_handle_t)active))
        return;

    gf_monitor_id_t active_monitor = wm_find_active_monitor (m);

    for (uint32_t i = 0; i < windows->count; i++)
    {
        if (windows->items[i].id == active || wm_is_excluded (m, windows->items[i].id))
            continue;
        if (active_monitor != (gf_monitor_id_t)-1
            && windows->items[i].monitor_id != active_monitor)
            continue;

        m->platform->window_minimize (m->display, windows->items[i].id);
        windows->items[i].is_minimized = true;
    }
}
