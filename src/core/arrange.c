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
                        gf_rect_t **out_geometries)
{
    if (!m || !windows || !out_geometries || window_count == 0)
        return GF_ERROR_INVALID_PARAMETER;

    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    gf_rect_t workspace_bounds;
    gf_err_t result = platform->screen_get_bounds (display, &workspace_bounds);

    if (result != GF_SUCCESS)
        return GF_ERROR_DISPLAY_CONNECTION;

    gf_rect_t *new_geometries = gf_malloc (window_count * sizeof (gf_rect_t));
    if (!new_geometries)
        return GF_ERROR_MEMORY_ALLOCATION;

    wm_geometry (m)->apply_layout (wm_geometry (m), windows, window_count,
                                   &workspace_bounds, new_geometries);

    *out_geometries = new_geometries;
    return GF_SUCCESS;
}

gf_err_t
gf_wm_layout_apply (gf_wm_t *m)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_win_list_t *windows = wm_windows (m);
    gf_platform_t *platform = wm_platform (m);

    if (windows->count == 0 || !windows->items)
        return GF_SUCCESS;

    _build_workspace_candidate (m);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_ws_info_t *ws = &workspaces->items[i];

        if (ws->has_maximized_state)
            continue;

        gf_win_info_t *ws_windows = NULL;
        uint32_t ws_window_count = 0;

        if (gf_window_list_get_by_workspace (windows, ws->id, &ws_windows,
                                             &ws_window_count)
                != GF_SUCCESS
            || ws_window_count == 0)
        {
            continue;
        }

        gf_win_info_t *non_minimized_windows = NULL;
        uint32_t non_minimized_count = 0;

        non_minimized_windows = gf_malloc (ws_window_count * sizeof (gf_win_info_t));
        if (!non_minimized_windows)
        {
            gf_free (ws_windows);
            continue;
        }

        for (uint32_t j = 0; j < ws_window_count; j++)
        {
            if (!ws_windows[j].is_minimized && !wm_is_excluded (m, ws_windows[j].id))
            {
                non_minimized_windows[non_minimized_count++] = ws_windows[j];
            }
        }

        // Only calculate layout if there are non-minimized windows
        if (non_minimized_count > 0)
        {
            gf_rect_t workspace_bounds;
            if (wm_platform (m)->screen_get_bounds (*wm_display (m), &workspace_bounds)
                == GF_SUCCESS)
            {
                gf_rect_t *new_geometries = NULL;
                if (gf_wm_calculate_layout (m, non_minimized_windows, non_minimized_count,
                                            &new_geometries)
                    == GF_SUCCESS)
                {
                    gf_wm_apply_layout (m, non_minimized_windows, new_geometries,
                                        non_minimized_count);
                    if (m->config->enable_borders && platform->border_update)
                        platform->border_update (platform, m->config);
                    gf_free (new_geometries);
                }
            }
        }

        gf_free (non_minimized_windows);
        gf_free (ws_windows);
    }

    return GF_SUCCESS;
}

uint32_t
_get_maximized_windows (gf_wm_t *m, gf_win_info_t **out_windows)
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
            {
                result[idx++] = ws_wins[j];
            }
            gf_free (ws_wins);
        }
    }

    *out_windows = result;
    return idx;
}

int
_find_maximized_index (gf_win_info_t *windows, uint32_t count, gf_handle_t handle)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (windows[i].id == handle)
            return (int)i;
    }
    return -1;
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

        if (windows[i].is_minimized || !windows[i].needs_update && !windows[i].is_valid)
            continue;

        gf_err_t result = platform->window_set_geometry (
            display, windows[i].id, &geometry[i],
            GF_GEOMETRY_CHANGE_ALL | GF_GEOMETRY_APPLY_PADDING, m->config);

        if (result != GF_SUCCESS)
        {
            GF_LOG_WARN ("Failed to set geometry for window %p", (void *)windows[i].id);
        }

        gf_wm_window_sync (m, windows[i].id, windows[i].workspace_id);
        gf_window_list_clear_update_flags (window_list, windows[i].workspace_id);
    }
}

gf_err_t
gf_wm_layout_rebalance (gf_wm_t *m)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_win_list_t *windows = wm_windows (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_ws_info_t *src_ws = &workspaces->items[i];
        if (src_ws->has_maximized_state)
            continue;

        if (src_ws->window_count <= max_per_ws)
            continue;

        uint32_t overflow = src_ws->window_count - max_per_ws;

        for (uint32_t j = 0; j < overflow; j++)
        {
            gf_ws_id_t dst_id = -1;
            gf_ws_info_t *active_ws_info
                = gf_workspace_list_find_by_id (workspaces, workspaces->active_workspace);

            if (active_ws_info->available_space < 1)
            {
                gf_ws_id_t free_ws = gf_workspace_list_find_free (workspaces);

                if (free_ws == -1)
                {
                    dst_id = gf_workspace_create (workspaces, max_per_ws, false, false);
                }
                else
                {
                    dst_id = free_ws;
                }
                if (dst_id < 0)
                {
                    GF_LOG_ERROR ("Failed to find free workspace for overflow");
                    break;
                }
            }
            else
            {
                dst_id = active_ws_info->id;
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

            if (src_ws->id == dst_id)
                continue;
            gf_win_info_t *win = &list[0];

            for (uint32_t w = 0; w < windows->count; w++)
            {
                if (wm_is_excluded (m, win->id))
                    continue;

                if (windows->items[w].id == win->id)
                {
                    GF_LOG_INFO ("Move window %p from workspace %u to workspace %u",
                                 (void *)windows->items[w].id,
                                 windows->items[w].workspace_id, dst_id);
                    windows->items[w].workspace_id = dst_id;

                    workspaces->items[dst_id].window_count++;
                    workspaces->items[dst_id].available_space--;
                    src_ws->window_count--;
                    src_ws->available_space++;

                    break;
                }
            }

            gf_free (list);
        }
    }

    return GF_SUCCESS;
}

void
_handle_fullscreen_windows (gf_wm_t *m)
{
    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_win_list_t *windows = wm_windows (m);
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_handle_t active_win_id = m->platform->window_get_focused (m->display);

    if (active_win_id != 0
        && m->platform->window_is_fullscreen (m->display, (gf_handle_t)active_win_id))
    {
        for (uint32_t i = 0; i < windows->count; i++)
        {
            if (windows->items[i].id == active_win_id)
                continue;

            m->platform->window_minimize (m->display, windows->items[i].id);
            windows->items[i].is_minimized = true;
        }
    }
}
