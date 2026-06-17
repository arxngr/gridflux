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
            result = platform->screen_get_bounds (display, &workspace_bounds);
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

        gf_err_t result = platform->window_set_geometry (
            display, windows[i].id, &geometry[i], GF_GEOMETRY_CHANGE_ALL, m->config);

        if (result != GF_SUCCESS)
            GF_LOG_WARN ("Failed to set geometry for window %p", (void *)windows[i].id);

        gf_wm_window_sync (m, windows[i].id, windows[i].workspace_id);
        gf_window_list_clear_update_flags (window_list, windows[i].workspace_id);
    }
}

static uint32_t
enumerate_monitors (gf_platform_t *platform, gf_display_t display, gf_monitor_t *monitors)
{
    uint32_t count = 0;
    if (platform->monitor_enumerate)
    {
        count = GF_MAX_MONITORS;
        platform->monitor_enumerate (platform, monitors, &count);
    }

    if (count == 0)
    {
        count = 1;
        monitors[0].id = 0;
        monitors[0].is_primary = true;
        platform->screen_get_bounds (display, &monitors[0].bounds);
    }

    return count;
}

static void
filter_monitor_windows (gf_win_info_t *ws_wins, uint32_t ws_count, gf_monitor_t *mon,
                        uint32_t monitor_count, gf_win_info_t *out, uint32_t *out_count,
                        gf_wm_t *m, gf_ws_info_t *ws)
{
    *out_count = 0;
    for (uint32_t j = 0; j < ws_count; j++)
    {
        if (ws_wins[j].is_maximized && !ws->has_maximized_state)
            continue;
        if (ws_wins[j].is_minimized || wm_is_excluded (m, ws_wins[j].id))
            continue;
        if (monitor_count > 1 && ws_wins[j].monitor_id != mon->id)
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

    if (gf_window_list_get_by_workspace (windows, ws->id, &ws_wins, &ws_count)
            != GF_SUCCESS
        || ws_count == 0)
    {
        gf_free (ws_wins);
        return;
    }

    for (uint32_t mon_idx = 0; mon_idx < monitor_count; mon_idx++)
        apply_layout_to_monitor (m, ws, &monitors[mon_idx], ws_wins, ws_count,
                                 monitor_count);

    gf_free (ws_wins);
}

gf_err_t
gf_wm_layout_apply (gf_wm_t *m)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    if (m->state.resize_active)
        return GF_SUCCESS;

    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_win_list_t *windows = wm_windows (m);
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    if (windows->count == 0 || !windows->items)
        return GF_SUCCESS;

    assign_windows_to_workspaces (m);

    gf_monitor_t monitors[GF_MAX_MONITORS];
    uint32_t monitor_count = enumerate_monitors (platform, display, monitors);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_ws_info_t *ws = &workspaces->items[i];
        if (!ws->has_maximized_state)
            apply_layout_to_workspace (m, ws, monitors, monitor_count);
    }

    return GF_SUCCESS;
}

static gf_ws_id_t
find_overflow_target (gf_wm_t *m, gf_ws_info_t *src_ws)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;
    gf_monitor_id_t active_monitor = find_active_monitor (m);
    gf_ws_info_t *active_ws = gf_workspace_list_find_by_id (
        workspaces, workspaces->active_workspace[active_monitor]);

    if (active_ws && active_ws->available_space < 1)
    {
        gf_ws_id_t free_ws = gf_workspace_list_find_free (workspaces);
        if (free_ws == -1)
            return gf_workspace_create (workspaces, max_per_ws, false, false);
        return free_ws;
    }

    return active_ws ? active_ws->id : -1;
}

static void
relocate_overflow_window (gf_wm_t *m, gf_ws_info_t *src_ws, gf_ws_id_t dst_id,
                          gf_win_info_t *win)
{
    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_platform_t *platform = wm_platform (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    for (uint32_t w = 0; w < windows->count; w++)
    {
        if (windows->items[w].id != win->id || wm_is_excluded (m, win->id))
            continue;

        GF_LOG_INFO ("Move window %p from workspace %u to workspace %u",
                     (void *)windows->items[w].id, windows->items[w].workspace_id,
                     dst_id);

        windows->items[w].workspace_id = dst_id;

        gf_ws_info_t *dst_ws = gf_workspace_list_find_by_id (workspaces, dst_id);
        if (dst_ws)
        {
            dst_ws->window_count++;
            dst_ws->available_space--;
        }
        src_ws->window_count--;
        src_ws->available_space++;

        gf_window_list_mark_all_needs_update (windows, &src_ws->id);
        src_ws->is_custom_layout = false;
        gf_window_list_mark_all_needs_update (windows, &dst_id);
        if (dst_ws)
            dst_ws->is_custom_layout = false;

        /* Re-layout destination */
        gf_win_info_t *mon_wins = gf_malloc (windows->count * sizeof (gf_win_info_t));
        if (!mon_wins)
            break;

        uint32_t mon_count = 0;
        for (uint32_t k = 0; k < windows->count; k++)
        {
            if (windows->items[k].workspace_id == dst_id
                && !windows->items[k].is_minimized)
                mon_wins[mon_count++] = windows->items[k];
        }

        if (mon_count > 0)
        {
            gf_rect_t *new_geoms = NULL;
            if (gf_wm_calculate_layout (m, mon_wins, mon_count, win->monitor_id,
                                        &new_geoms)
                == GF_SUCCESS)
            {
                gf_wm_apply_layout (m, mon_wins, new_geoms, mon_count);
                if (m->config->enable_borders && platform->border_update)
                    platform->border_update (platform, m->config);
                gf_free (new_geoms);
            }
        }
        gf_free (mon_wins);
        break;
    }
}

static gf_err_t
rebalance_workspace (gf_wm_t *m, gf_ws_info_t *src_ws)
{
    gf_win_list_t *windows = wm_windows (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;
    uint32_t overflow = src_ws->window_count - max_per_ws;

    for (uint32_t j = 0; j < overflow; j++)
    {
        gf_ws_id_t dst_id = find_overflow_target (m, src_ws);
        if (dst_id < 0)
        {
            GF_LOG_ERROR ("Failed to find free workspace for overflow");
            return GF_ERROR_INVALID_PARAMETER;
        }

        gf_win_info_t *list = NULL;
        uint32_t count = 0;

        if (gf_window_list_get_by_workspace (windows, src_ws->id, &list, &count)
                != GF_SUCCESS
            || count == 0)
        {
            gf_free (list);
            break;
        }

        if (src_ws->id != dst_id)
            relocate_overflow_window (m, src_ws, dst_id, &list[0]);

        gf_free (list);
    }

    return GF_SUCCESS;
}

gf_err_t
gf_wm_layout_rebalance (gf_wm_t *m)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_ws_list_t *workspaces = wm_workspaces (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_ws_info_t *src_ws = &workspaces->items[i];
        if (src_ws->has_maximized_state || src_ws->window_count <= max_per_ws)
            continue;
        rebalance_workspace (m, src_ws);
    }

    return GF_SUCCESS;
}

void
enforce_fullscreen (gf_wm_t *m)
{
    gf_win_list_t *windows = wm_windows (m);
    gf_handle_t active = m->platform->window_get_focused (m->display);

    if (active == 0
        || !m->platform->window_is_fullscreen (m->display, (gf_handle_t)active))
        return;

    gf_monitor_id_t active_monitor = find_active_monitor (m);

    for (uint32_t i = 0; i < windows->count; i++)
    {
        if (windows->items[i].id == active)
            continue;
        if (active_monitor != (gf_monitor_id_t)-1
            && windows->items[i].monitor_id != active_monitor)
            continue;

        m->platform->window_minimize (m->display, windows->items[i].id);
        windows->items[i].is_minimized = true;
    }
}
