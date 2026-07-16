/*
 * resize.c -- live tiling resize.
 *
 * For a picture-first walkthrough (drag the demos in a browser), open
 * docs/resize.html -- it uses the same steps and numbers as this header.
 *
 * When you drag one window bigger or smaller, the windows next to it should
 * resize too so the tiles stay packed together with no gaps. That is what this
 * file does.
 *
 * The easiest way to picture it is two windows side by side sharing a wall. The
 * right edge of the left window and the left edge of the right window are the
 * same line. If you push that wall to the right, the left window grows and the
 * right window shrinks by the same amount. So a resize is really just: a shared
 * wall moved, and now every window touching it has to follow.
 *
 * A drag only ever moves one thing. Dragging an edge moves a single line -- a
 * left or right drag moves a vertical line (an x value), a top or bottom drag
 * moves a horizontal line (a y value). Dragging a corner moves a single point
 * (an x and a y at once).
 *
 * Some words used below. A window is a rectangle: x and y are its top-left
 * corner, so its left edge is x, its right edge is x + width, its top is y and
 * its bottom is y + height. The "source" is the window you are dragging. A
 * "neighbour" is a window that follows it. When we find a neighbour we remember
 * which of its edges is touching the source (its "align": left, right, top or
 * bottom); for corner drags we remember which of its corners is touching.
 *
 *
 * The drag runs as this sequence of function calls:
 *
 *   1. gf_wm_resize_event   -- poll the platform; get the drag event each tick.
 *   2. _propagate_resize    -- handle one tick (repeats while the mouse is held):
 *        a. _clamp_source_edges        find neighbours, stop them under min size
 *             (_clamp_edge per edge, _clamp_all_corner_neighbors for corners)
 *        b. _enforce_source_min_size   keep the dragged window above min size
 *        c. window_set_geometry        apply the new size to the dragged window
 *        d. _apply_edges_to_neighbors  move the neighbours to follow
 *             (_propagate_edge_to_neighbors, _propagate_all_corner_neighbors)
 *   3. _commit_resize       -- only when released: save where windows ended up.
 *
 * Finding the neighbours (inside steps a and d) is itself a few calls:
 *   _find_segment_neighbors (edges): _edge_line_from_dir, _find_nearest_edge_dist,
 *       _expand_neighbor_set, _collect_edge_neighbors
 *   _find_all_corner_neighbors (corners): _corner_point_from_dir,
 *       _find_min_corner_dist_sq, _collect_close_corners
 *
 *
 * The math, with real numbers, so it does not have to be worked out again.
 *
 * Say the source starts at x = 100 and is 300 wide. Its right edge is therefore
 * at 100 + 300 = 400. You drag that right edge to 550, so the source is now
 * 550 - 100 = 450 wide.
 *
 * To decide whether another window N is a neighbour of that edge, two things
 * must both be true. N must be touching the wall, meaning its left edge is at
 * 400 (within 5 pixels, so a small padding gap still counts). And N must line up
 * with the source vertically, meaning their top-to-bottom ranges overlap; a
 * window that touches the wall but sits entirely above or below the source does
 * not really share it. If a whole column of windows lines up against the wall,
 * they are all found by starting from the closest one and repeatedly adding any
 * other window that also touches and overlaps, widening the range as it goes.
 *
 * The clamp stops a neighbour from being squashed to nothing. Suppose the
 * minimum size is 50 and N runs from 400 to 600. N is on the right of the wall,
 * so it shrinks as the wall moves right, and the wall is not allowed past
 * 600 - 50 = 550. If you try to drag to 580 it stops at 550 and N stays exactly
 * 50 wide. (The same idea applies on every side: a neighbour above the wall is
 * capped at its own bottom minus the minimum, one below at its top plus the
 * minimum, and so on.)
 *
 * Once the final wall position is known, each neighbour follows it: the edge
 * that was touching jumps to the wall and the opposite edge stays where it was.
 * For N above, its left edge moves to 550 while its right edge stays at 600, so
 * its new width is 600 - 550 = 50.
 *
 * Corners work the same way, just on a point instead of a line. Picture four
 * windows in a 2x2 grid that all meet at the middle, say at (500, 400). Your
 * window is the bottom-left one and you drag its top-right corner, which is that
 * middle point. To find the neighbours, the code looks at every other window's
 * corners and keeps the corner nearest the point you grabbed. It compares
 * distances as (corner_x - 500)^2 + (corner_y - 400)^2; squaring is only there
 * to avoid a square root, the order comes out the same. In a tidy grid the other
 * three windows each already have a corner sitting on (500, 400), so their
 * distance is 0 and all three are neighbours. Each then moves its touching
 * corner to wherever you drag the middle, changing its width and height
 * together -- the same edge logic as above, just done on both axes at once.
 */
#include "../platform/platform.h"
#include "../utils/logger.h"
#include "../utils/memory.h"
#include "internal.h"
#include "types.h"
#include "wm.h"
#include <stdlib.h>

// Collapse the dragged edge into a single coordinate ("line") plus orientation.
// A LEFT/RIGHT drag moves a vertical line (an x); a TOP/BOTTOM drag moves a
// horizontal line (a y). Returns false if `dir` is not a single edge.
static bool
_edge_line_from_dir (const gf_rect_t *r, gf_resize_dir_t dir, int32_t *out_line,
                     bool *out_horiz)
{
    if (dir == GF_RESIZE_BOTTOM)
    {
        *out_line = r->y + (int32_t)r->height;
        *out_horiz = true;
    }
    else if (dir == GF_RESIZE_TOP)
    {
        *out_line = r->y;
        *out_horiz = true;
    }
    else if (dir == GF_RESIZE_RIGHT)
    {
        *out_line = r->x + (int32_t)r->width;
        *out_horiz = false;
    }
    else if (dir == GF_RESIZE_LEFT)
    {
        *out_line = r->x;
        *out_horiz = false;
    }
    else
        return false;
    return true;
}

// For every other window, measure how far its nearest edge is from `line` and
// record which of its edges that was (its alignment). Returns the distance of
// the closest window that ALSO overlaps the source's span [seg_min, seg_max].
// dists[i] holds each window's distance (-1 if skipped); aligns[i] its edge.
// The "+ 5"/"- 5" are slop so tiles with a small padding gap still count.
static int32_t
_find_nearest_edge_dist (gf_win_list_t *windows, gf_handle_t source_id,
                         gf_ws_id_t workspace_id, gf_monitor_id_t monitor_id,
                         int32_t line, bool is_horiz, int32_t seg_min, int32_t seg_max,
                         int32_t search_range, int32_t *dists, gf_align_type_t *aligns)
{
    int32_t min_dist = search_range;
    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *w = &windows->items[i];
        dists[i] = -1;
        // Skip the source itself, dead/minimised windows, and anything on a
        // different workspace or monitor.
        if (w->id == source_id || !w->is_valid || w->is_minimized)
            continue;
        if (w->workspace_id != workspace_id || w->monitor_id != monitor_id)
            continue;

        const gf_rect_t *wr = &w->geometry;
        int32_t d1, d2;
        // Distance from each of the window's two relevant edges to the line;
        // keep the smaller, and remember which edge it was (the alignment).
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
        // Only consider it the nearest if it overlaps the source's span.
        int32_t w_min = is_horiz ? wr->x : wr->y;
        int32_t w_max
            = is_horiz ? (wr->x + (int32_t)wr->width) : (wr->y + (int32_t)wr->height);
        if (!(w_min > seg_max + 5 || w_max < seg_min - 5))
            if (dists[i] >= 0 && dists[i] < min_dist)
                min_dist = dists[i];
    }
    return min_dist;
}

// Flood-fill the neighbour set. Starting from windows within ~min_dist of the
// line, mark each "affected" and GROW the span [seg_min, seg_max] to cover it;
// repeat until a pass adds nothing. This is what lets a whole stack of windows
// share one edge and move together, not just the single closest one.
static void
_expand_neighbor_set (gf_win_list_t *windows, int32_t *dists, int32_t min_dist,
                      bool is_horiz, bool *affected, int32_t *seg_min, int32_t *seg_max)
{
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (uint32_t i = 0; i < windows->count; i++)
        {
            // Skip ones already taken, skipped, or too far from the line.
            if (affected[i] || dists[i] < 0 || dists[i] > min_dist + 5)
                continue;
            const gf_rect_t *wr = &windows->items[i].geometry;
            int32_t w_min = is_horiz ? wr->x : wr->y;
            int32_t w_max
                = is_horiz ? (wr->x + (int32_t)wr->width) : (wr->y + (int32_t)wr->height);
            // Must overlap the (growing) span to join the set.
            if (w_min > *seg_max + 5 || w_max < *seg_min - 5)
                continue;
            affected[i] = true;
            changed = true;
            if (w_min < *seg_min)
                *seg_min = w_min;
            if (w_max > *seg_max)
                *seg_max = w_max;
        }
    }
}

// Copy the affected windows (and the touching edge of each) into `out`.
static uint32_t
_collect_edge_neighbors (gf_win_list_t *windows, bool *affected, gf_align_type_t *aligns,
                         gf_segment_neighbor_t *out, uint32_t max_out)
{
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

// Full edge-neighbour search for one dragged edge: turn the edge into a line,
// find the nearest touching distance, flood-fill the stack, then collect them.
// seg_min/seg_max start as the source's span perpendicular to the drag.
static uint32_t
_find_segment_neighbors (gf_win_list_t *windows, gf_handle_t source_id,
                         const gf_rect_t *initial, gf_resize_dir_t dir,
                         gf_segment_neighbor_t *out, uint32_t max_out,
                         gf_ws_id_t workspace_id, gf_monitor_id_t monitor_id)
{
    int32_t line = 0;
    bool is_horiz = false;
    if (!_edge_line_from_dir (initial, dir, &line, &is_horiz) || windows->count == 0)
        return 0;

    int32_t seg_min = is_horiz ? initial->x : initial->y;
    int32_t seg_max = is_horiz ? (initial->x + (int32_t)initial->width)
                               : (initial->y + (int32_t)initial->height);
    // Only look as far as twice the source's perpendicular size for a neighbour.
    int32_t search_range
        = is_horiz ? (int32_t)initial->height * 2 : (int32_t)initial->width * 2;

    // These are indexed by GLOBAL window position, so they must span the whole
    // list. A fixed per-workspace size overflowed the stack once >10 windows
    // existed anywhere.
    int32_t *dists = gf_malloc (windows->count * sizeof (int32_t));
    gf_align_type_t *aligns = gf_malloc (windows->count * sizeof (gf_align_type_t));
    bool *affected = gf_calloc (windows->count, sizeof (bool));
    if (!dists || !aligns || !affected)
    {
        gf_free (dists);
        gf_free (aligns);
        gf_free (affected);
        return 0;
    }

    int32_t min_dist = _find_nearest_edge_dist (windows, source_id, workspace_id,
                                                monitor_id, line, is_horiz, seg_min,
                                                seg_max, search_range, dists, aligns);
    _expand_neighbor_set (windows, dists, min_dist, is_horiz, affected, &seg_min,
                          &seg_max);
    uint32_t n = _collect_edge_neighbors (windows, affected, aligns, out, max_out);

    gf_free (dists);
    gf_free (aligns);
    gf_free (affected);
    return n;
}

// Pull a dragged edge's target line back so this one neighbour keeps at least
// min_size, based on which of its edges touches the source.
static int32_t
_clamp_line_to_neighbor (int32_t target_line, const gf_win_info_t *nb,
                         gf_align_type_t align, uint32_t min_size)
{
    if (align == GF_ALIGN_TOP)
    {
        int32_t limit = nb->geometry.y + (int32_t)nb->geometry.height - (int32_t)min_size;
        return (target_line > limit) ? limit : target_line;
    }
    if (align == GF_ALIGN_BOTTOM)
    {
        int32_t limit = nb->geometry.y + (int32_t)min_size;
        return (target_line < limit) ? limit : target_line;
    }
    if (align == GF_ALIGN_LEFT)
    {
        int32_t limit = nb->geometry.x + (int32_t)nb->geometry.width - (int32_t)min_size;
        return (target_line > limit) ? limit : target_line;
    }
    if (align == GF_ALIGN_RIGHT)
    {
        int32_t limit = nb->geometry.x + (int32_t)min_size;
        return (target_line < limit) ? limit : target_line;
    }
    return target_line;
}

// Where the dragged edge sits, expressed as a single coordinate, given the
// source's current x/y/w/h. Shared by the clamp and propagate paths.
static int32_t
_edge_target_line (gf_resize_dir_t dir, int32_t cx, int32_t cy, int32_t cw, int32_t ch)
{
    if (dir == GF_RESIZE_BOTTOM)
        return cy + ch;
    if (dir == GF_RESIZE_TOP)
        return cy;
    if (dir == GF_RESIZE_RIGHT)
        return cx + cw;
    return cx; // GF_RESIZE_LEFT
}

// Write a (clamped) edge line back into the source's x/y/w/h: the touching edge
// moves to `line`, the opposite edge stays fixed.
static void
_edge_apply_line (gf_resize_dir_t dir, int32_t line, int32_t *cx, int32_t *cy,
                  int32_t *cw, int32_t *ch)
{
    if (dir == GF_RESIZE_BOTTOM)
        *ch = line - *cy;
    else if (dir == GF_RESIZE_TOP)
    {
        int32_t diff = line - *cy;
        *cy = line;
        *ch -= diff;
    }
    else if (dir == GF_RESIZE_RIGHT)
        *cw = line - *cx;
    else if (dir == GF_RESIZE_LEFT)
    {
        int32_t diff = line - *cx;
        *cx = line;
        *cw -= diff;
    }
}

// Limit how far the dragged edge may travel so no neighbour drops below
// min_size, then write the clamped line back into the source's x/y/w/h.
static void
_clamp_edge (gf_win_list_t *windows, gf_win_info_t *source, const gf_rect_t *initial,
             gf_resize_dir_t dir, uint32_t min_size, int32_t *clamp_x, int32_t *clamp_y,
             int32_t *clamp_w, int32_t *clamp_h)
{
    if (windows->count == 0)
        return;
    gf_segment_neighbor_t *neighbors
        = gf_malloc (windows->count * sizeof (gf_segment_neighbor_t));
    if (!neighbors)
        return;

    uint32_t nc = _find_segment_neighbors (windows, source->id, initial, dir, neighbors,
                                           windows->count, source->workspace_id,
                                           source->monitor_id);

    int32_t target_line
        = _edge_target_line (dir, *clamp_x, *clamp_y, *clamp_w, *clamp_h);
    for (uint32_t i = 0; i < nc; i++)
        target_line = _clamp_line_to_neighbor (target_line, neighbors[i].win,
                                               neighbors[i].align, min_size);
    _edge_apply_line (dir, target_line, clamp_x, clamp_y, clamp_w, clamp_h);

    gf_free (neighbors);
}

// Move one neighbour's touching edge to `new_line` (the far edge stays fixed),
// clamp it to min_size, and push the new geometry to the platform.
static void
_resize_neighbor_edge (gf_win_info_t *nb, gf_align_type_t align, int32_t new_line,
                       uint32_t min_size, gf_platform_t *platform, gf_display_t display,
                       gf_config_t *config)
{
    int32_t old_x = nb->geometry.x, old_y = nb->geometry.y;
    int32_t old_w = (int32_t)nb->geometry.width, old_h = (int32_t)nb->geometry.height;
    int32_t old_right = old_x + old_w;
    int32_t old_bottom = old_y + old_h;

    int32_t new_x = old_x, new_y = old_y, new_w = old_w, new_h = old_h;

    // Move only the touching edge to new_line; the far edge stays put.
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

    // Never let a neighbour shrink below the minimum; pin the moved edge if so.
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

    platform->window_set_geometry (display, nb->id, &nb->geometry, GF_GEOMETRY_CHANGE_ALL,
                                   config);
}

// Slide each neighbour's touching edge to the source's NEW edge so they stay
// flush; the opposite edge stays fixed, so the neighbour's size absorbs the
// change. Enforce min_size, then push the new geometry to the platform.
static void
_propagate_edge_to_neighbors (gf_win_list_t *windows, gf_win_info_t *source,
                              const gf_rect_t *initial, const gf_rect_t *current,
                              gf_resize_dir_t dir, uint32_t min_size,
                              gf_platform_t *platform, gf_display_t display,
                              gf_config_t *config)
{
    if (windows->count == 0)
        return;
    gf_segment_neighbor_t *neighbors
        = gf_malloc (windows->count * sizeof (gf_segment_neighbor_t));
    if (!neighbors)
        return;

    uint32_t nc = _find_segment_neighbors (windows, source->id, initial, dir, neighbors,
                                           windows->count, source->workspace_id,
                                           source->monitor_id);

    // The source's edge in its new position -- the line neighbours follow.
    int32_t new_line = _edge_target_line (dir, current->x, current->y,
                                          (int32_t)current->width,
                                          (int32_t)current->height);

    for (uint32_t i = 0; i < nc; i++)
        _resize_neighbor_edge (neighbors[i].win, neighbors[i].align, new_line, min_size,
                               platform, display, config);

    gf_free (neighbors);
}

// For a corner drag the moving thing is a POINT (the dragged corner), not a
// line. Returns its (cx, cy). False if `dir` is not a corner.
static bool
_corner_point_from_dir (const gf_rect_t *r, gf_resize_dir_t dir, int32_t *out_cx,
                        int32_t *out_cy)
{
    if ((dir & GF_RESIZE_RIGHT) && (dir & GF_RESIZE_BOTTOM))
    {
        *out_cx = r->x + (int32_t)r->width;
        *out_cy = r->y + (int32_t)r->height;
    }
    else if ((dir & GF_RESIZE_LEFT) && (dir & GF_RESIZE_BOTTOM))
    {
        *out_cx = r->x;
        *out_cy = r->y + (int32_t)r->height;
    }
    else if ((dir & GF_RESIZE_RIGHT) && (dir & GF_RESIZE_TOP))
    {
        *out_cx = r->x + (int32_t)r->width;
        *out_cy = r->y;
    }
    else if ((dir & GF_RESIZE_LEFT) && (dir & GF_RESIZE_TOP))
    {
        *out_cx = r->x;
        *out_cy = r->y;
    }
    else
        return false;
    return true;
}

// Smallest distance SQUARED (squared to avoid a sqrt) from the dragged corner
// (cx, cy) to any corner of any candidate window -- i.e. how close the nearest
// neighbour corner is.
static int32_t
_find_min_corner_dist_sq (gf_win_list_t *windows, gf_handle_t source_id,
                          gf_ws_id_t workspace_id, gf_monitor_id_t monitor_id, int32_t cx,
                          int32_t cy)
{
    int32_t min_dist_sq = 1000000;
    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *w = &windows->items[i];
        if (w->id == source_id || !w->is_valid || w->is_minimized)
            continue;
        if (w->workspace_id != workspace_id || w->monitor_id != monitor_id)
            continue;
        const gf_rect_t *wr = &w->geometry;
        int32_t dx, dy, dsq;
        // Test all four of this window's corners against (cx, cy).
        dx = wr->x - cx;
        dy = wr->y - cy;
        dsq = dx * dx + dy * dy;
        if (dsq < min_dist_sq)
            min_dist_sq = dsq;
        dx = wr->x + (int32_t)wr->width - cx;
        dy = wr->y - cy;
        dsq = dx * dx + dy * dy;
        if (dsq < min_dist_sq)
            min_dist_sq = dsq;
        dx = wr->x - cx;
        dy = wr->y + (int32_t)wr->height - cy;
        dsq = dx * dx + dy * dy;
        if (dsq < min_dist_sq)
            min_dist_sq = dsq;
        dx = wr->x + (int32_t)wr->width - cx;
        dy = wr->y + (int32_t)wr->height - cy;
        dsq = dx * dx + dy * dy;
        if (dsq < min_dist_sq)
            min_dist_sq = dsq;
    }
    return min_dist_sq;
}

// Gather windows whose nearest corner sits within slop (+100 ~= 10px squared)
// of the closest found, recording WHICH of their four corners is the toucher.
// The chained checks pick the nearest of the window's four corners.
static uint32_t
_collect_close_corners (gf_win_list_t *windows, gf_handle_t source_id,
                        gf_ws_id_t workspace_id, gf_monitor_id_t monitor_id, int32_t cx,
                        int32_t cy, int32_t min_dist_sq, gf_corner_neighbor_t *out,
                        uint32_t max_out)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < windows->count && count < max_out; i++)
    {
        gf_win_info_t *w = &windows->items[i];
        if (w->id == source_id || !w->is_valid || w->is_minimized)
            continue;
        if (w->workspace_id != workspace_id || w->monitor_id != monitor_id)
            continue;
        const gf_rect_t *wr = &w->geometry;
        int32_t dx, dy, dsq;
        gf_corner_type_t corner;

        // Start at top-left, then fall through to each further corner only if
        // the previous one wasn't close enough -- so `corner` ends on the
        // nearest of the four.
        dx = wr->x - cx;
        dy = wr->y - cy;
        dsq = dx * dx + dy * dy;
        corner = GF_CORNER_TOP_LEFT;
        if (dsq > min_dist_sq + 100)
        {
            dx = wr->x + (int32_t)wr->width - cx;
            dy = wr->y - cy;
            dsq = dx * dx + dy * dy;
            corner = GF_CORNER_TOP_RIGHT;
        }
        if (dsq > min_dist_sq + 100)
        {
            dx = wr->x - cx;
            dy = wr->y + (int32_t)wr->height - cy;
            dsq = dx * dx + dy * dy;
            corner = GF_CORNER_BOTTOM_LEFT;
        }
        if (dsq > min_dist_sq + 100)
        {
            dx = wr->x + (int32_t)wr->width - cx;
            dy = wr->y + (int32_t)wr->height - cy;
            dsq = dx * dx + dy * dy;
            corner = GF_CORNER_BOTTOM_RIGHT;
        }
        if (dsq <= min_dist_sq + 100)
        {
            out[count].win = w;
            out[count].corner = corner;
            count++;
        }
    }
    return count;
}

// Full corner-neighbour search: only proceeds if some corner is genuinely near
// the dragged corner (within half the source's width); otherwise none.
static uint32_t
_find_all_corner_neighbors (gf_win_list_t *windows, gf_handle_t source_id,
                            const gf_rect_t *source_rect, gf_resize_dir_t dir,
                            gf_ws_id_t workspace_id, gf_monitor_id_t monitor_id,
                            gf_corner_neighbor_t *out_neighbors, uint32_t max_out)
{
    int32_t cx = 0, cy = 0;
    if (!_corner_point_from_dir (source_rect, dir, &cx, &cy))
        return 0;

    int32_t min_dist_sq
        = _find_min_corner_dist_sq (windows, source_id, workspace_id, monitor_id, cx, cy);
    // Ignore everything if the nearest corner is more than half a window away.
    int32_t radius_sq
        = (int32_t)(source_rect->width / 2) * (int32_t)(source_rect->width / 2);
    if (min_dist_sq > radius_sq)
        return 0;

    return _collect_close_corners (windows, source_id, workspace_id, monitor_id, cx, cy,
                                   min_dist_sq, out_neighbors, max_out);
}

// Pull the dragged corner (target_x, target_y) back so this one corner-neighbour
// keeps at least min_size. `is_left`/`is_top` capture which corner of the
// neighbour touches: a left corner is shrunk from the right, etc.
static void
_clamp_corner_to_neighbor (const gf_win_info_t *nb, gf_corner_type_t corner,
                           uint32_t min_size, int32_t *target_x, int32_t *target_y)
{
    int32_t n_left = nb->geometry.x;
    int32_t n_right = nb->geometry.x + (int32_t)nb->geometry.width;
    int32_t n_top = nb->geometry.y;
    int32_t n_bottom = nb->geometry.y + (int32_t)nb->geometry.height;

    bool is_left = (corner == GF_CORNER_TOP_LEFT || corner == GF_CORNER_BOTTOM_LEFT);
    bool is_top = (corner == GF_CORNER_TOP_LEFT || corner == GF_CORNER_TOP_RIGHT);

    if (is_left)
    {
        if (*target_x > n_right - (int32_t)min_size)
            *target_x = n_right - (int32_t)min_size;
    }
    else if (*target_x < n_left + (int32_t)min_size)
        *target_x = n_left + (int32_t)min_size;

    if (is_top)
    {
        if (*target_y > n_bottom - (int32_t)min_size)
            *target_y = n_bottom - (int32_t)min_size;
    }
    else if (*target_y < n_top + (int32_t)min_size)
        *target_y = n_top + (int32_t)min_size;
}

// Corner version of clamp_edge: cap the target corner in BOTH x and y so no
// diagonal neighbour shrinks below min_size, then write it back into x/y/w/h.
static void
_clamp_all_corner_neighbors (gf_win_list_t *windows, gf_handle_t source_id,
                             const gf_rect_t *initial, gf_resize_dir_t dir,
                             uint32_t min_size, int32_t *clamp_x, int32_t *clamp_y,
                             int32_t *clamp_w, int32_t *clamp_h, gf_ws_id_t workspace_id,
                             gf_monitor_id_t monitor_id)
{
    if (windows->count == 0)
        return;
    gf_corner_neighbor_t *nbs
        = gf_malloc (windows->count * sizeof (gf_corner_neighbor_t));
    if (!nbs)
        return;

    uint32_t count
        = _find_all_corner_neighbors (windows, source_id, initial, dir, workspace_id,
                                      monitor_id, nbs, windows->count);
    if (!count)
    {
        gf_free (nbs);
        return;
    }

    // The dragged corner's wanted position.
    int32_t target_x = (dir & GF_RESIZE_RIGHT) ? (*clamp_x + *clamp_w) : *clamp_x;
    int32_t target_y = (dir & GF_RESIZE_BOTTOM) ? (*clamp_y + *clamp_h) : *clamp_y;

    // Each neighbour's touching corner limits how far target_x/y may go.
    for (uint32_t i = 0; i < count; i++)
        _clamp_corner_to_neighbor (nbs[i].win, nbs[i].corner, min_size, &target_x,
                                   &target_y);

    // Write the clamped corner back into the source's x/w (and y/h).
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

    gf_free (nbs);
}

// Move one neighbour's matching corner to the dragged corner (target_cx,
// target_cy) -- both axes -- clamp to min_size, and push the new geometry.
// `is_left`/`is_top` capture which corner touches: a left corner moves the left
// edge and keeps the right fixed, etc.
static void
_resize_neighbor_corner (gf_win_info_t *nb, gf_corner_type_t corner, int32_t target_cx,
                         int32_t target_cy, uint32_t min_size, gf_platform_t *platform,
                         gf_display_t display, gf_config_t *config)
{
    int32_t old_x = nb->geometry.x, old_y = nb->geometry.y;
    int32_t old_right = old_x + (int32_t)nb->geometry.width;
    int32_t old_bottom = old_y + (int32_t)nb->geometry.height;

    bool is_left = (corner == GF_CORNER_TOP_LEFT || corner == GF_CORNER_BOTTOM_LEFT);
    bool is_top = (corner == GF_CORNER_TOP_LEFT || corner == GF_CORNER_TOP_RIGHT);

    int32_t new_x = old_x, new_y = old_y;
    int32_t new_w = (int32_t)nb->geometry.width, new_h = (int32_t)nb->geometry.height;

    if (is_left)
    {
        new_x = target_cx;
        new_w = old_right - target_cx;
    }
    else
        new_w = target_cx - old_x;

    if (is_top)
    {
        new_y = target_cy;
        new_h = old_bottom - target_cy;
    }
    else
        new_h = target_cy - old_y;

    // Clamp to min_size, anchoring the far corner if we hit the floor.
    if (new_w < (int32_t)min_size)
    {
        if (is_left)
            new_x = old_right - (int32_t)min_size;
        new_w = (int32_t)min_size;
    }
    if (new_h < (int32_t)min_size)
    {
        if (is_top)
            new_y = old_bottom - (int32_t)min_size;
        new_h = (int32_t)min_size;
    }

    nb->geometry.x = new_x;
    nb->geometry.y = new_y;
    nb->geometry.width = (gf_dimension_t)new_w;
    nb->geometry.height = (gf_dimension_t)new_h;

    platform->window_set_geometry (display, nb->id, &nb->geometry, GF_GEOMETRY_CHANGE_ALL,
                                   config);
}

// Corner version of propagate_edge_to_neighbors: move each neighbour's matching
// corner to the source's new dragged corner (both axes at once), enforce
// min_size, and push the geometry.
static void
_propagate_all_corner_neighbors (gf_win_list_t *windows, gf_win_info_t *source,
                                 const gf_rect_t *initial, const gf_rect_t *current,
                                 gf_resize_dir_t dir, uint32_t min_size,
                                 gf_platform_t *platform, gf_display_t display,
                                 gf_config_t *config, gf_ws_id_t workspace_id,
                                 gf_monitor_id_t monitor_id)
{
    if (windows->count == 0)
        return;
    gf_corner_neighbor_t *nbs
        = gf_malloc (windows->count * sizeof (gf_corner_neighbor_t));
    if (!nbs)
        return;

    uint32_t count
        = _find_all_corner_neighbors (windows, source->id, initial, dir, workspace_id,
                                      monitor_id, nbs, windows->count);
    if (!count)
    {
        gf_free (nbs);
        return;
    }

    // The dragged corner in its new position -- the point neighbours follow.
    int32_t target_cx
        = (dir & GF_RESIZE_RIGHT) ? (current->x + (int32_t)current->width) : current->x;
    int32_t target_cy
        = (dir & GF_RESIZE_BOTTOM) ? (current->y + (int32_t)current->height) : current->y;

    for (uint32_t i = 0; i < count; i++)
        _resize_neighbor_corner (nbs[i].win, nbs[i].corner, target_cx, target_cy,
                                 min_size, platform, display, config);

    gf_free (nbs);
}

// Clamp the source's proposed rect (cx,cy,cw,ch) against its neighbours: run the
// edge clamp for each edge the drag touches, then the corner clamp.
static void
_clamp_source_edges (gf_win_list_t *windows, gf_win_info_t *source, gf_resize_event_t *ev,
                     uint32_t min_size, int32_t *cx, int32_t *cy, int32_t *cw,
                     int32_t *ch)
{
    if (ev->direction & GF_RESIZE_RIGHT)
        _clamp_edge (windows, source, &ev->initial_rect, GF_RESIZE_RIGHT, min_size, cx,
                     cy, cw, ch);
    if (ev->direction & GF_RESIZE_LEFT)
        _clamp_edge (windows, source, &ev->initial_rect, GF_RESIZE_LEFT, min_size, cx, cy,
                     cw, ch);
    if (ev->direction & GF_RESIZE_BOTTOM)
        _clamp_edge (windows, source, &ev->initial_rect, GF_RESIZE_BOTTOM, min_size, cx,
                     cy, cw, ch);
    if (ev->direction & GF_RESIZE_TOP)
        _clamp_edge (windows, source, &ev->initial_rect, GF_RESIZE_TOP, min_size, cx, cy,
                     cw, ch);

    _clamp_all_corner_neighbors (windows, source->id, &ev->initial_rect, ev->direction,
                                 min_size, cx, cy, cw, ch, source->workspace_id,
                                 source->monitor_id);
}

// Final guard: the dragged window itself never shrinks below min_size.
static void
_enforce_source_min_size (gf_resize_event_t *ev, int32_t *cw, int32_t *ch,
                          uint32_t min_size)
{
    if (*cw < (int32_t)min_size)
        *cw = (int32_t)min_size;
    if (*ch < (int32_t)min_size)
        *ch = (int32_t)min_size;

    ev->current_rect.width = (gf_dimension_t)*cw;
    ev->current_rect.height = (gf_dimension_t)*ch;
}

// Push the committed resize outward to neighbours: corner neighbours first,
// then the neighbours of each edge the drag touches, so they stay flush with
// the source's new edges.
static void
_apply_edges_to_neighbors (gf_win_list_t *windows, gf_win_info_t *source,
                           gf_resize_event_t *ev, uint32_t min_size,
                           gf_platform_t *platform, gf_display_t display,
                           gf_config_t *config)
{
    _propagate_all_corner_neighbors (
        windows, source, &ev->initial_rect, &ev->current_rect, ev->direction, min_size,
        platform, display, config, source->workspace_id, source->monitor_id);

    if (ev->direction & GF_RESIZE_RIGHT)
        _propagate_edge_to_neighbors (windows, source, &ev->initial_rect,
                                      &ev->current_rect, GF_RESIZE_RIGHT, min_size,
                                      platform, display, config);
    if (ev->direction & GF_RESIZE_LEFT)
        _propagate_edge_to_neighbors (windows, source, &ev->initial_rect,
                                      &ev->current_rect, GF_RESIZE_LEFT, min_size,
                                      platform, display, config);
    if (ev->direction & GF_RESIZE_BOTTOM)
        _propagate_edge_to_neighbors (windows, source, &ev->initial_rect,
                                      &ev->current_rect, GF_RESIZE_BOTTOM, min_size,
                                      platform, display, config);
    if (ev->direction & GF_RESIZE_TOP)
        _propagate_edge_to_neighbors (windows, source, &ev->initial_rect,
                                      &ev->current_rect, GF_RESIZE_TOP, min_size,
                                      platform, display, config);
}

// One resize tick: clamp the source against its neighbours, enforce its own
// minimum, commit the source's geometry, then propagate the change outward.
static void
_propagate_resize (gf_wm_t *m, gf_resize_event_t *ev)
{
    gf_win_list_t *windows = wm_windows (m);
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    gf_win_info_t *source = gf_window_list_find_by_window_id (windows, ev->window);
    if (!source)
    {
        GF_LOG_WARN ("[RESIZE] Source window %p not found", (void *)ev->window);
        return;
    }

    uint32_t min_size = (m->config && m->config->min_window_size > 0)
                            ? m->config->min_window_size
                            : GF_MIN_WINDOW_SIZE;

    // Start from where the user dragged the source to, then clamp it.
    int32_t cx = ev->current_rect.x, cy = ev->current_rect.y;
    int32_t cw = (int32_t)ev->current_rect.width;
    int32_t ch = (int32_t)ev->current_rect.height;

    _clamp_source_edges (windows, source, ev, min_size, &cx, &cy, &cw, &ch);
    _enforce_source_min_size (ev, &cw, &ch, min_size);
    ev->current_rect = (gf_rect_t){ cx, cy, (gf_dimension_t)cw, (gf_dimension_t)ch };

    platform->window_set_geometry (display, ev->window, &ev->current_rect,
                                   GF_GEOMETRY_CHANGE_ALL, m->config);

    _apply_edges_to_neighbors (windows, source, ev, min_size, platform, display,
                               m->config);
    source->geometry = ev->current_rect;
}

// On release: re-sync every window's cached geometry from its real on-screen
// position, and mark the workspace as a custom layout so auto-arrange won't snap
// it back to a grid.
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
    if (ws_id > 0)
    {
        gf_ws_info_t *ws = gf_workspace_list_find_by_id (wm_workspaces (m), ws_id);
        if (ws)
        {
            ws->is_custom_layout = true;
            GF_LOG_INFO ("Workspace %d marked as having custom layout", ws_id);
        }
    }
}

// Entry point, called from the event loop. Polls the platform for a resize
// event and dispatches by phase: ACTIVE = live update each drag tick,
// COMPLETE = final update then commit, IDLE = nothing.
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
