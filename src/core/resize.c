#include "../platform/platform.h"
#include "../utils/logger.h"
#include "internal.h"
#include "types.h"
#include "wm.h"

static uint32_t
_find_segment_neighbors (gf_win_list_t *windows, gf_handle_t source_id,
                         const gf_rect_t *initial, gf_resize_dir_t dir,
                         gf_segment_neighbor_t *out, uint32_t max_out,
                         gf_ws_id_t workspace_id, gf_monitor_id_t monitor_id)
{
    int32_t line = 0;
    bool is_horiz = false;
    if (dir == GF_RESIZE_BOTTOM)
    {
        line = initial->y + (int32_t)initial->height;
        is_horiz = true;
    }
    else if (dir == GF_RESIZE_TOP)
    {
        line = initial->y;
        is_horiz = true;
    }
    else if (dir == GF_RESIZE_RIGHT)
    {
        line = initial->x + (int32_t)initial->width;
        is_horiz = false;
    }
    else if (dir == GF_RESIZE_LEFT)
    {
        line = initial->x;
        is_horiz = false;
    }
    else
        return 0;

    int32_t seg_min, seg_max;
    if (is_horiz)
    {
        seg_min = initial->x;
        seg_max = initial->x + (int32_t)initial->width;
    }
    else
    {
        seg_min = initial->y;
        seg_max = initial->y + (int32_t)initial->height;
    }

    int32_t search_range = is_horiz ? initial->height * 2 : initial->width * 2;
    int32_t min_dist = search_range;

    int32_t dists[GF_MAX_WINDOWS_PER_WORKSPACE];
    gf_align_type_t aligns[GF_MAX_WINDOWS_PER_WORKSPACE];

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *w = &windows->items[i];
        dists[i] = -1;
        if (w->id == source_id || !w->is_valid || w->is_minimized)
            continue;
        if (w->workspace_id != workspace_id || w->monitor_id != monitor_id)
            continue;

        const gf_rect_t *wr = &w->geometry;
        int32_t d1, d2;
        if (is_horiz)
        {
            d1 = abs (wr->y - line);
            d2 = abs (wr->y + (int32_t)wr->height - line);
            dists[i] = (d1 < d2) ? d1 : d2;
            aligns[i] = (d1 < d2) ? GF_ALIGN_TOP : GF_ALIGN_BOTTOM;
        }
        else
        {
            d1 = abs (wr->x - line);
            d2 = abs (wr->x + (int32_t)wr->width - line);
            dists[i] = (d1 < d2) ? d1 : d2;
            aligns[i] = (d1 < d2) ? GF_ALIGN_LEFT : GF_ALIGN_RIGHT;
        }

        int32_t w_min, w_max;
        if (is_horiz)
        {
            w_min = wr->x;
            w_max = wr->x + (int32_t)wr->width;
        }
        else
        {
            w_min = wr->y;
            w_max = wr->y + (int32_t)wr->height;
        }

        if (!(w_min > seg_max + 5 || w_max < seg_min - 5))
        {
            if (dists[i] >= 0 && dists[i] < min_dist)
                min_dist = dists[i];
        }
    }

    bool affected[GF_MAX_WINDOWS_PER_WORKSPACE] = { false };
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (uint32_t i = 0; i < windows->count; i++)
        {
            if (affected[i] || dists[i] < 0 || dists[i] > min_dist + 5)
                continue;

            gf_win_info_t *w = &windows->items[i];
            const gf_rect_t *wr = &w->geometry;
            int32_t w_min, w_max;
            if (is_horiz)
            {
                w_min = wr->x;
                w_max = wr->x + (int32_t)wr->width;
            }
            else
            {
                w_min = wr->y;
                w_max = wr->y + (int32_t)wr->height;
            }

            if (w_min > seg_max + 5 || w_max < seg_min - 5)
                continue;

            affected[i] = true;
            changed = true;
            if (w_min < seg_min)
                seg_min = w_min;
            if (w_max > seg_max)
                seg_max = w_max;
        }
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < windows->count && count < max_out; i++)
    {
        if (affected[i])
        {
            out[count].win = &windows->items[i];
            out[count].align = aligns[i];
            count++;
        }
    }
    return count;
}

static void
_clamp_edge (gf_win_list_t *windows, gf_win_info_t *source, const gf_rect_t *initial,
             gf_resize_dir_t dir, uint32_t min_size, int32_t *clamp_x, int32_t *clamp_y,
             int32_t *clamp_w, int32_t *clamp_h)
{
    gf_segment_neighbor_t neighbors[GF_MAX_WINDOWS_PER_WORKSPACE];
    uint32_t nc = _find_segment_neighbors (windows, source->id, initial, dir, neighbors,
                                           GF_MAX_WINDOWS_PER_WORKSPACE,
                                           source->workspace_id, source->monitor_id);

    int32_t target_line = 0;
    if (dir == GF_RESIZE_BOTTOM)
        target_line = *clamp_y + *clamp_h;
    else if (dir == GF_RESIZE_TOP)
        target_line = *clamp_y;
    else if (dir == GF_RESIZE_RIGHT)
        target_line = *clamp_x + *clamp_w;
    else if (dir == GF_RESIZE_LEFT)
        target_line = *clamp_x;

    for (uint32_t i = 0; i < nc; i++)
    {
        gf_win_info_t *nb = neighbors[i].win;
        gf_align_type_t align = neighbors[i].align;

        if (align == GF_ALIGN_TOP)
        {
            int32_t limit
                = nb->geometry.y + (int32_t)nb->geometry.height - (int32_t)min_size;
            if (target_line > limit)
                target_line = limit;
        }
        else if (align == GF_ALIGN_BOTTOM)
        {
            int32_t limit = nb->geometry.y + (int32_t)min_size;
            if (target_line < limit)
                target_line = limit;
        }
        else if (align == GF_ALIGN_LEFT)
        {
            int32_t limit
                = nb->geometry.x + (int32_t)nb->geometry.width - (int32_t)min_size;
            if (target_line > limit)
                target_line = limit;
        }
        else if (align == GF_ALIGN_RIGHT)
        {
            int32_t limit = nb->geometry.x + (int32_t)min_size;
            if (target_line < limit)
                target_line = limit;
        }
    }

    if (dir == GF_RESIZE_BOTTOM)
        *clamp_h = target_line - *clamp_y;
    else if (dir == GF_RESIZE_TOP)
    {
        int32_t diff = target_line - *clamp_y;
        *clamp_y = target_line;
        *clamp_h -= diff;
    }
    else if (dir == GF_RESIZE_RIGHT)
        *clamp_w = target_line - *clamp_x;
    else if (dir == GF_RESIZE_LEFT)
    {
        int32_t diff = target_line - *clamp_x;
        *clamp_x = target_line;
        *clamp_w -= diff;
    }
}

static void
_propagate_edge_to_neighbors (gf_win_list_t *windows, gf_win_info_t *source,
                              const gf_rect_t *initial, const gf_rect_t *current,
                              gf_resize_dir_t dir, uint32_t min_size,
                              gf_platform_t *platform, gf_display_t display,
                              gf_config_t *config)
{
    gf_segment_neighbor_t neighbors[GF_MAX_WINDOWS_PER_WORKSPACE];
    uint32_t nc = _find_segment_neighbors (windows, source->id, initial, dir, neighbors,
                                           GF_MAX_WINDOWS_PER_WORKSPACE,
                                           source->workspace_id, source->monitor_id);

    int32_t new_line = 0;
    if (dir == GF_RESIZE_BOTTOM)
        new_line = current->y + (int32_t)current->height;
    else if (dir == GF_RESIZE_TOP)
        new_line = current->y;
    else if (dir == GF_RESIZE_RIGHT)
        new_line = current->x + (int32_t)current->width;
    else if (dir == GF_RESIZE_LEFT)
        new_line = current->x;

    for (uint32_t i = 0; i < nc; i++)
    {
        gf_win_info_t *nb = neighbors[i].win;
        gf_align_type_t align = neighbors[i].align;

        int32_t old_x = nb->geometry.x, old_y = nb->geometry.y;
        int32_t old_w = (int32_t)nb->geometry.width, old_h = (int32_t)nb->geometry.height;
        int32_t old_right = old_x + old_w;
        int32_t old_bottom = old_y + old_h;

        int32_t new_x = old_x, new_y = old_y, new_w = old_w, new_h = old_h;

        if (align == GF_ALIGN_TOP)
        {
            new_y = new_line;
            new_h = old_bottom - new_line;
        }
        else if (align == GF_ALIGN_BOTTOM)
        {
            new_h = new_line - old_y;
        }
        else if (align == GF_ALIGN_LEFT)
        {
            new_x = new_line;
            new_w = old_right - new_line;
        }
        else if (align == GF_ALIGN_RIGHT)
        {
            new_w = new_line - old_x;
        }

        if (new_w < (int32_t)min_size)
        {
            if (align == GF_ALIGN_LEFT)
                new_x = old_right - (int32_t)min_size;
            new_w = (int32_t)min_size;
        }
        if (new_h < (int32_t)min_size)
        {
            if (align == GF_ALIGN_TOP)
                new_y = old_bottom - (int32_t)min_size;
            new_h = (int32_t)min_size;
        }

        nb->geometry.x = new_x;
        nb->geometry.y = new_y;
        nb->geometry.width = (gf_dimension_t)new_w;
        nb->geometry.height = (gf_dimension_t)new_h;

        platform->window_set_geometry (display, nb->id, &nb->geometry,
                                       GF_GEOMETRY_CHANGE_ALL, config);
    }
}

static uint32_t
_find_all_corner_neighbors (gf_win_list_t *windows, gf_handle_t source_id,
                            const gf_rect_t *source_rect, gf_resize_dir_t dir,
                            gf_ws_id_t workspace_id, gf_monitor_id_t monitor_id,
                            gf_corner_neighbor_t *out_neighbors, uint32_t max_out)
{
    int32_t src_cx = 0, src_cy = 0;

    if ((dir & GF_RESIZE_RIGHT) && (dir & GF_RESIZE_BOTTOM))
    {
        src_cx = source_rect->x + (int32_t)source_rect->width;
        src_cy = source_rect->y + (int32_t)source_rect->height;
    }
    else if ((dir & GF_RESIZE_LEFT) && (dir & GF_RESIZE_BOTTOM))
    {
        src_cx = source_rect->x;
        src_cy = source_rect->y + (int32_t)source_rect->height;
    }
    else if ((dir & GF_RESIZE_RIGHT) && (dir & GF_RESIZE_TOP))
    {
        src_cx = source_rect->x + (int32_t)source_rect->width;
        src_cy = source_rect->y;
    }
    else if ((dir & GF_RESIZE_LEFT) && (dir & GF_RESIZE_TOP))
    {
        src_cx = source_rect->x;
        src_cy = source_rect->y;
    }
    else
        return 0;

    int32_t min_dist_sq = 1000000;

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *w = &windows->items[i];
        if (w->id == source_id || !w->is_valid || w->is_minimized)
            continue;
        if (w->workspace_id != workspace_id || w->monitor_id != monitor_id)
            continue;

        gf_rect_t *wr = &w->geometry;
        int32_t dx, dy, dist_sq;

        dx = wr->x - src_cx;
        dy = wr->y - src_cy;
        dist_sq = dx * dx + dy * dy;
        if (dist_sq < min_dist_sq)
            min_dist_sq = dist_sq;
        dx = wr->x + (int32_t)wr->width - src_cx;
        dy = wr->y - src_cy;
        dist_sq = dx * dx + dy * dy;
        if (dist_sq < min_dist_sq)
            min_dist_sq = dist_sq;
        dx = wr->x - src_cx;
        dy = wr->y + (int32_t)wr->height - src_cy;
        dist_sq = dx * dx + dy * dy;
        if (dist_sq < min_dist_sq)
            min_dist_sq = dist_sq;
        dx = wr->x + (int32_t)wr->width - src_cx;
        dy = wr->y + (int32_t)wr->height - src_cy;
        dist_sq = dx * dx + dy * dy;
        if (dist_sq < min_dist_sq)
            min_dist_sq = dist_sq;
    }

    int32_t max_search_radius_sq = (source_rect->width / 2) * (source_rect->width / 2);
    if (min_dist_sq > max_search_radius_sq)
        return 0;

    uint32_t count = 0;
    for (uint32_t i = 0; i < windows->count && count < max_out; i++)
    {
        gf_win_info_t *w = &windows->items[i];
        if (w->id == source_id || !w->is_valid || w->is_minimized)
            continue;
        if (w->workspace_id != workspace_id || w->monitor_id != monitor_id)
            continue;

        gf_rect_t *wr = &w->geometry;
        int32_t dx, dy, dist_sq;

        dx = wr->x - src_cx;
        dy = wr->y - src_cy;
        dist_sq = dx * dx + dy * dy;
        if (dist_sq <= min_dist_sq + 100)
        {
            out_neighbors[count].win = w;
            out_neighbors[count].corner = GF_CORNER_TOP_LEFT;
            count++;
            continue;
        }

        dx = wr->x + (int32_t)wr->width - src_cx;
        dy = wr->y - src_cy;
        dist_sq = dx * dx + dy * dy;
        if (dist_sq <= min_dist_sq + 100)
        {
            out_neighbors[count].win = w;
            out_neighbors[count].corner = GF_CORNER_TOP_RIGHT;
            count++;
            continue;
        }

        dx = wr->x - src_cx;
        dy = wr->y + (int32_t)wr->height - src_cy;
        dist_sq = dx * dx + dy * dy;
        if (dist_sq <= min_dist_sq + 100)
        {
            out_neighbors[count].win = w;
            out_neighbors[count].corner = GF_CORNER_BOTTOM_LEFT;
            count++;
            continue;
        }

        dx = wr->x + (int32_t)wr->width - src_cx;
        dy = wr->y + (int32_t)wr->height - src_cy;
        dist_sq = dx * dx + dy * dy;
        if (dist_sq <= min_dist_sq + 100)
        {
            out_neighbors[count].win = w;
            out_neighbors[count].corner = GF_CORNER_BOTTOM_RIGHT;
            count++;
            continue;
        }
    }
    return count;
}

static void
_clamp_all_corner_neighbors (gf_win_list_t *windows, gf_handle_t source_id,
                             const gf_rect_t *initial, gf_resize_dir_t dir,
                             uint32_t min_size, int32_t *clamp_x, int32_t *clamp_y,
                             int32_t *clamp_w, int32_t *clamp_h, gf_ws_id_t workspace_id,
                             gf_monitor_id_t monitor_id)
{
    gf_corner_neighbor_t nbs[GF_MAX_WINDOWS_PER_WORKSPACE];
    uint32_t count
        = _find_all_corner_neighbors (windows, source_id, initial, dir, workspace_id,
                                      monitor_id, nbs, GF_MAX_WINDOWS_PER_WORKSPACE);
    if (!count)
        return;

    int32_t target_x = (dir & GF_RESIZE_RIGHT) ? (*clamp_x + *clamp_w) : *clamp_x;
    int32_t target_y = (dir & GF_RESIZE_BOTTOM) ? (*clamp_y + *clamp_h) : *clamp_y;

    for (uint32_t i = 0; i < count; i++)
    {
        gf_win_info_t *nb = nbs[i].win;
        gf_corner_type_t corner = nbs[i].corner;

        int32_t n_left = nb->geometry.x;
        int32_t n_right = nb->geometry.x + (int32_t)nb->geometry.width;
        int32_t n_top = nb->geometry.y;
        int32_t n_bottom = nb->geometry.y + (int32_t)nb->geometry.height;

        if (corner == GF_CORNER_TOP_LEFT)
        {
            if (target_x > n_right - (int32_t)min_size)
                target_x = n_right - (int32_t)min_size;
            if (target_y > n_bottom - (int32_t)min_size)
                target_y = n_bottom - (int32_t)min_size;
        }
        else if (corner == GF_CORNER_TOP_RIGHT)
        {
            if (target_x < n_left + (int32_t)min_size)
                target_x = n_left + (int32_t)min_size;
            if (target_y > n_bottom - (int32_t)min_size)
                target_y = n_bottom - (int32_t)min_size;
        }
        else if (corner == GF_CORNER_BOTTOM_LEFT)
        {
            if (target_x > n_right - (int32_t)min_size)
                target_x = n_right - (int32_t)min_size;
            if (target_y < n_top + (int32_t)min_size)
                target_y = n_top + (int32_t)min_size;
        }
        else if (corner == GF_CORNER_BOTTOM_RIGHT)
        {
            if (target_x < n_left + (int32_t)min_size)
                target_x = n_left + (int32_t)min_size;
            if (target_y < n_top + (int32_t)min_size)
                target_y = n_top + (int32_t)min_size;
        }
    }

    if (dir & GF_RESIZE_RIGHT)
        *clamp_w = target_x - *clamp_x;
    else
    {
        int32_t diff = target_x - *clamp_x;
        *clamp_x = target_x;
        *clamp_w -= diff;
    }

    if (dir & GF_RESIZE_BOTTOM)
        *clamp_h = target_y - *clamp_y;
    else
    {
        int32_t diff = target_y - *clamp_y;
        *clamp_y = target_y;
        *clamp_h -= diff;
    }
}

static void
_propagate_all_corner_neighbors (gf_win_list_t *windows, gf_win_info_t *source,
                                 const gf_rect_t *initial, const gf_rect_t *current,
                                 gf_resize_dir_t dir, uint32_t min_size,
                                 gf_platform_t *platform, gf_display_t display,
                                 gf_config_t *config, gf_ws_id_t workspace_id,
                                 gf_monitor_id_t monitor_id)
{
    gf_corner_neighbor_t nbs[GF_MAX_WINDOWS_PER_WORKSPACE];
    uint32_t count
        = _find_all_corner_neighbors (windows, source->id, initial, dir, workspace_id,
                                      monitor_id, nbs, GF_MAX_WINDOWS_PER_WORKSPACE);
    if (!count)
        return;

    int32_t target_cx
        = (dir & GF_RESIZE_RIGHT) ? (current->x + (int32_t)current->width) : current->x;
    int32_t target_cy
        = (dir & GF_RESIZE_BOTTOM) ? (current->y + (int32_t)current->height) : current->y;

    for (uint32_t i = 0; i < count; i++)
    {
        gf_win_info_t *nb = nbs[i].win;
        gf_corner_type_t corner = nbs[i].corner;

        int32_t old_right = nb->geometry.x + (int32_t)nb->geometry.width;
        int32_t old_bottom = nb->geometry.y + (int32_t)nb->geometry.height;
        int32_t old_x = nb->geometry.x;
        int32_t old_y = nb->geometry.y;

        int32_t new_x = old_x, new_y = old_y, new_w = (int32_t)nb->geometry.width,
                new_h = (int32_t)nb->geometry.height;

        if (corner == GF_CORNER_TOP_LEFT)
        {
            new_x = target_cx;
            new_y = target_cy;
            new_w = old_right - target_cx;
            new_h = old_bottom - target_cy;
        }
        else if (corner == GF_CORNER_TOP_RIGHT)
        {
            new_x = old_x;
            new_y = target_cy;
            new_w = target_cx - old_x;
            new_h = old_bottom - target_cy;
        }
        else if (corner == GF_CORNER_BOTTOM_LEFT)
        {
            new_x = target_cx;
            new_y = old_y;
            new_w = old_right - target_cx;
            new_h = target_cy - old_y;
        }
        else if (corner == GF_CORNER_BOTTOM_RIGHT)
        {
            new_x = old_x;
            new_y = old_y;
            new_w = target_cx - old_x;
            new_h = target_cy - old_y;
        }

        if (new_w < (int32_t)min_size)
        {
            if (corner == GF_CORNER_TOP_LEFT || corner == GF_CORNER_BOTTOM_LEFT)
                new_x = old_right - (int32_t)min_size;
            new_w = (int32_t)min_size;
        }
        if (new_h < (int32_t)min_size)
        {
            if (corner == GF_CORNER_TOP_LEFT || corner == GF_CORNER_TOP_RIGHT)
                new_y = old_bottom - (int32_t)min_size;
            new_h = (int32_t)min_size;
        }

        nb->geometry.x = new_x;
        nb->geometry.y = new_y;
        nb->geometry.width = (gf_dimension_t)new_w;
        nb->geometry.height = (gf_dimension_t)new_h;

        platform->window_set_geometry (display, nb->id, &nb->geometry,
                                       GF_GEOMETRY_CHANGE_ALL, config);
    }
}

static void
_propagate_resize (gf_wm_t *m, gf_resize_event_t *ev)
{
    gf_win_list_t *windows = wm_windows (m);
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    gf_win_info_t *source = gf_window_list_find_by_window_id (windows, ev->window);
    if (!source)
    {
        GF_LOG_WARN ("[RESIZE] Source window %p not found in WM list",
                     (void *)ev->window);
        return;
    }

    uint32_t min_size = GF_MIN_WINDOW_SIZE;
    if (m->config && m->config->min_window_size > 0)
        min_size = m->config->min_window_size;

    int32_t clamp_x = ev->current_rect.x;
    int32_t clamp_y = ev->current_rect.y;
    int32_t clamp_w = (int32_t)ev->current_rect.width;
    int32_t clamp_h = (int32_t)ev->current_rect.height;

    //  Calculate boundaries and clamp target current_rect
    if (ev->direction & GF_RESIZE_RIGHT)
        _clamp_edge (windows, source, &ev->initial_rect, GF_RESIZE_RIGHT, min_size,
                     &clamp_x, &clamp_y, &clamp_w, &clamp_h);
    if (ev->direction & GF_RESIZE_LEFT)
        _clamp_edge (windows, source, &ev->initial_rect, GF_RESIZE_LEFT, min_size,
                     &clamp_x, &clamp_y, &clamp_w, &clamp_h);
    if (ev->direction & GF_RESIZE_BOTTOM)
        _clamp_edge (windows, source, &ev->initial_rect, GF_RESIZE_BOTTOM, min_size,
                     &clamp_x, &clamp_y, &clamp_w, &clamp_h);
    if (ev->direction & GF_RESIZE_TOP)
        _clamp_edge (windows, source, &ev->initial_rect, GF_RESIZE_TOP, min_size,
                     &clamp_x, &clamp_y, &clamp_w, &clamp_h);

    _clamp_all_corner_neighbors (windows, source->id, &ev->initial_rect, ev->direction,
                                 min_size, &clamp_x, &clamp_y, &clamp_w, &clamp_h,
                                 source->workspace_id, source->monitor_id);

    //  Final min_size check for source
    if (clamp_w < (int32_t)min_size)
        clamp_w = (int32_t)min_size;
    if (clamp_h < (int32_t)min_size)
        clamp_h = (int32_t)min_size;

    ev->current_rect.x = clamp_x;
    ev->current_rect.y = clamp_y;
    ev->current_rect.width = (gf_dimension_t)clamp_w;
    ev->current_rect.height = (gf_dimension_t)clamp_h;

    // ENFORCE source window limits at platform layer
    platform->window_set_geometry (display, ev->window, &ev->current_rect,
                                   GF_GEOMETRY_CHANGE_ALL, m->config);

    // We perform corner logic FIRST so its findings are based on original geometry
    _propagate_all_corner_neighbors (
        windows, source, &ev->initial_rect, &ev->current_rect, ev->direction, min_size,
        platform, display, m->config, source->workspace_id, source->monitor_id);

    // Propagate changes to neighboring windows
    if (ev->direction & GF_RESIZE_RIGHT)
        _propagate_edge_to_neighbors (windows, source, &ev->initial_rect,
                                      &ev->current_rect, GF_RESIZE_RIGHT, min_size,
                                      platform, display, m->config);
    if (ev->direction & GF_RESIZE_LEFT)
        _propagate_edge_to_neighbors (windows, source, &ev->initial_rect,
                                      &ev->current_rect, GF_RESIZE_LEFT, min_size,
                                      platform, display, m->config);
    if (ev->direction & GF_RESIZE_BOTTOM)
        _propagate_edge_to_neighbors (windows, source, &ev->initial_rect,
                                      &ev->current_rect, GF_RESIZE_BOTTOM, min_size,
                                      platform, display, m->config);
    if (ev->direction & GF_RESIZE_TOP)
        _propagate_edge_to_neighbors (windows, source, &ev->initial_rect,
                                      &ev->current_rect, GF_RESIZE_TOP, min_size,
                                      platform, display, m->config);

    source->geometry = ev->current_rect;
}

static void
_commit_resize (gf_wm_t *m, gf_resize_event_t *ev)
{
    gf_win_list_t *windows = wm_windows (m);
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    // Sync all windows' geometry from their actual screen positions
    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *w = &windows->items[i];
        if (!w->is_valid || w->is_minimized)
            continue;

        gf_rect_t geom;
        if (platform->window_get_geometry (display, w->id, &geom) == GF_SUCCESS)
        {
            w->geometry = geom;
            w->needs_update = false;
        }
    }

    GF_LOG_INFO ("[RESIZE] Committed resize for window %p", (void *)ev->window);

    // Mark workspace as having a custom layout
    gf_ws_id_t ws_id
        = ev->window
              ? (gf_window_list_find_by_window_id (windows, ev->window)->workspace_id)
              : 0;
    if (ws_id != 0)
    {
        gf_ws_info_t *ws = gf_workspace_list_find_by_id (wm_workspaces (m), ws_id);
        if (ws)
        {
            ws->is_custom_layout = true;
            GF_LOG_INFO ("Workspace %d marked as having custom layout", ws_id);
        }
    }
}

void
gf_wm_resize_event (gf_wm_t *m)
{
    if (!m)
        return;

    if (m->config && !m->config->enable_live_resize)
        return;

    gf_platform_t *platform = wm_platform (m);
    if (!platform->resize_poll)
        return;

    gf_resize_event_t ev;
    if (!platform->resize_poll (platform, &ev))
        return;

    switch (ev.phase)
    {
    case GF_RESIZE_ACTIVE:
        if (!m->state.resize_active)
        {
            m->state.resize_active = true;
            GF_LOG_INFO ("[RESIZE] Resize started for window %p, dir=%d",
                         (void *)ev.window, ev.direction);
        }
        if (ev.direction != GF_RESIZE_NONE)
        {
            GF_LOG_DEBUG ("[RESIZE] Propagating dir=%d dw=%d dh=%d", ev.direction, ev.dw,
                          ev.dh);
            _propagate_resize (m, &ev);
        }
        break;

    case GF_RESIZE_COMPLETE:
        GF_LOG_INFO ("[RESIZE] Resize complete for window %p, dir=%d", (void *)ev.window,
                     ev.direction);
        _propagate_resize (m, &ev);
        _commit_resize (m, &ev);
        m->state.resize_active = false;
        break;

    case GF_RESIZE_IDLE:
    default:
        break;
    }
}
