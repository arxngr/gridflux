#include "config/excludes.h"
#include "config/rules.h"
#include "core/internal.h"
#include "utils/memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// Only the native desktop and configuration persistence are fakes. All window,
// workspace, maximize, transfer, and arrangement transitions use production code.
static gf_rect_t actual[5];
static gf_monitor_id_t physical[5];
static bool minimized[5], maximized[5], bordered[5];
static bool dock_hidden[GF_MAX_MONITORS];
static bool filled[5];
static gf_handle_t rejected_fill;
static uint32_t fill_failures;
static bool delay_visibility[5], delay_mode[5], requested_mode[5];
static uint32_t mode_requests[5];
static uint32_t writes[5], minimizes[5], restores[5], failures;
static gf_handle_t focused, rejected;
static gf_handle_t rejected_visibility;
static bool dragging, missing_external;
static unsigned
index_of (gf_handle_t window)
{
    return (unsigned)(uintptr_t)window;
}
static gf_handle_t
handle (unsigned i)
{
    return (gf_handle_t)(uintptr_t)i;
}

void
gf_log (gf_log_level_t level, const char *format, ...)
{
    (void)level;
    (void)format;
}
const gf_window_rule_t *
gf_rules_find (const gf_config_t *cfg, const char *name)
{
    (void)cfg;
    (void)name;
    return NULL;
}
bool
gf_config_workspace_is_locked (const gf_config_t *cfg, gf_ws_id_t id)
{
    (void)cfg;
    (void)id;
    return false;
}
gf_err_t
gf_config_workspace_lock (gf_config_t *cfg, gf_ws_id_t id)
{
    (void)cfg;
    (void)id;
    return GF_SUCCESS;
}
gf_err_t
gf_config_workspace_unlock (gf_config_t *cfg, gf_ws_id_t id)
{
    (void)cfg;
    (void)id;
    return GF_SUCCESS;
}
bool
gf_exclude_list_contains (const gf_exclude_list_t *list, const char *name)
{
    (void)list;
    (void)name;
    return false;
}
gf_err_t
gf_excludes_add (gf_config_t *cfg, const char *name)
{
    (void)cfg;
    (void)name;
    return GF_SUCCESS;
}
gf_err_t
gf_excludes_remove (gf_config_t *cfg, const char *name)
{
    (void)cfg;
    (void)name;
    return GF_SUCCESS;
}

static gf_handle_t
get_focused (gf_display_t display)
{
    (void)display;
    return focused;
}
static uint32_t
workspace_count (gf_display_t display)
{
    (void)display;
    return 1;
}
static bool
is_minimized (gf_display_t display, gf_handle_t window)
{
    (void)display;
    return minimized[index_of (window)];
}
static bool
is_maximized (gf_display_t display, gf_handle_t window)
{
    (void)display;
    // Model platforms which clear their native maximize flag when iconified.
    return maximized[index_of (window)] && !minimized[index_of (window)];
}
static bool
no_state (gf_display_t display, gf_handle_t window)
{
    (void)display;
    (void)window;
    return false;
}
static bool
interacting (gf_display_t display)
{
    (void)display;
    return dragging;
}
static gf_monitor_id_t
monitor_from_window (gf_platform_t *platform, gf_handle_t window)
{
    (void)platform;
    // Minimized geometry may report the wrong display. The core must retain identity.
    return minimized[index_of (window)] ? 0 : physical[index_of (window)];
}
static gf_err_t
minimize (gf_display_t display, gf_handle_t window)
{
    (void)display;
    unsigned i = index_of (window);
    assert (!minimized[i]);
    minimizes[i]++;
    if (window == rejected_visibility)
        return GF_ERROR_PLATFORM_ERROR;
    if (!delay_visibility[i])
        minimized[i] = true;
    return GF_SUCCESS;
}
static gf_err_t
restore (gf_display_t display, gf_handle_t window)
{
    (void)display;
    unsigned i = index_of (window);
    if (minimized[i])
        restores[i]++;
    if (!delay_visibility[i])
        minimized[i] = false;
    return GF_SUCCESS;
}
static gf_err_t bounds (gf_display_t display, gf_monitor_id_t id, gf_rect_t *out);
static gf_err_t
set_maximized (gf_display_t display, gf_handle_t window, bool target)
{
    unsigned i = index_of (window);
    mode_requests[i]++;
    requested_mode[i] = target;
    if (!delay_mode[i])
        maximized[i] = target;
    if (target)
        bounds (display, physical[i], &actual[i]);
    return GF_SUCCESS;
}
static gf_err_t
get_geometry (gf_display_t display, gf_handle_t window, gf_rect_t *out)
{
    (void)display;
    *out = actual[index_of (window)];
    return GF_SUCCESS;
}
static gf_err_t
set_geometry (gf_display_t display, gf_handle_t window, const gf_rect_t *rect,
              gf_geom_flags_t flags, gf_config_t *cfg)
{
    (void)display;
    (void)flags;
    (void)cfg;
    unsigned i = index_of (window);
    assert (!minimized[i] && !maximized[i] && !dragging);
    assert (physical[i] == (rect->x >= 1920 ? 1u : 0u));
    if (window == rejected)
    {
        failures++;
        return GF_ERROR_INVALID_PARAMETER;
    }
    actual[i] = *rect;
    writes[i]++;
    return GF_SUCCESS;
}
static void
add_border (gf_platform_t *platform, gf_handle_t window, gf_color_t color, int thickness)
{
    (void)platform;
    (void)color;
    (void)thickness;
    assert (!minimized[index_of (window)] && !maximized[index_of (window)]);
    bordered[index_of (window)] = true;
}
static void
remove_border (gf_platform_t *platform, gf_handle_t window)
{
    (void)platform;
    bordered[index_of (window)] = false;
}
static gf_err_t
bounds (gf_display_t display, gf_monitor_id_t id, gf_rect_t *out)
{
    (void)display;
    if (id > 1 || (id == 1 && missing_external))
        return GF_ERROR_INVALID_PARAMETER;
    *out = (gf_rect_t){ id == 1 ? 1920 : 0, 0, id == 1 ? 1280 : 1920, 1080 };
    return GF_SUCCESS;
}
static gf_err_t
monitors (gf_platform_t *platform, gf_monitor_t *out, uint32_t *count)
{
    (void)platform;
    assert (*count >= 2);
    memset (out, 0, 2 * sizeof (*out));
    for (unsigned i = 0; i < 2; i++)
    {
        out[i].id = i;
        bounds (NULL, i, &out[i].bounds);
        out[i].full_bounds = out[i].bounds;
    }
    *count = 2;
    return GF_SUCCESS;
}
static gf_err_t
enumerate (gf_display_t display, gf_ws_id_t *ws, gf_win_info_t **out, uint32_t *count)
{
    (void)display;
    (void)ws;
    *count = 3;
    *out = gf_calloc (*count, sizeof (**out));
    for (unsigned i = 1; i <= 3; i++)
    {
        (*out)[i - 1]
            = (gf_win_info_t){ .id = handle (i),
                               .workspace_id = 1,
                               .monitor_id = physical[i],
                               .geometry = actual[i],
                               .is_valid = true,
                               .is_minimized = minimized[i],
                               .is_maximized = is_maximized (NULL, handle (i)) };
        strcpy ((*out)[i - 1].name, "test-app");
    }
    return GF_SUCCESS;
}
static void
layout (const gf_layout_engine_t *engine, const gf_win_info_t *windows, uint32_t count,
        const gf_rect_t *b, gf_rect_t *out)
{
    (void)engine;
    (void)windows;
    for (unsigned i = 0; i < count; i++)
        out[i] = (gf_rect_t){ b->x + (int32_t)(i * b->width / count), b->y,
                              b->width / count, b->height };
}
static gf_win_info_t *
win (gf_wm_t *m, unsigned i)
{
    return gf_window_list_find_by_window_id (&m->state.windows, handle (i));
}
static void
dock_sync (gf_platform_t *platform, const bool *hide_on_monitor, uint32_t count)
{
    (void)platform;
    assert (count == 2);
    memcpy (dock_hidden, hide_on_monitor, count * sizeof (*hide_on_monitor));
}
static gf_err_t
fill_maximized (gf_display_t display, gf_handle_t window, bool fill_monitor)
{
    (void)display;
    unsigned i = index_of (window);
    if (fill_monitor && window == rejected_fill)
    {
        fill_failures++;
        return GF_ERROR_PLATFORM_ERROR;
    }
    filled[i] = fill_monitor && maximized[i] && !minimized[i];
    return GF_SUCCESS;
}
static void
tick (gf_wm_t *m)
{
    gf_wm_watch (m);
    gf_wm_event (m);
    wm_sync_dock_visibility (m);
    gf_wm_layout_rebalance (m);
    gf_wm_layout_apply (m);
    for (unsigned i = 0; i < m->state.windows.count; i++)
    {
        gf_win_info_t *w = &m->state.windows.items[i];
        gf_ws_info_t *ws
            = gf_workspace_list_find_by_id (&m->state.workspaces, w->workspace_id);
        assert (ws && ws->monitor_id == w->monitor_id);
        if (w->restore_workspace_id)
        {
            ws = gf_workspace_list_find_by_id (&m->state.workspaces,
                                               w->restore_workspace_id);
            assert (ws && ws->monitor_id == w->monitor_id && !ws->has_maximized_state);
        }
    }
}
int
main (void)
{
    gf_config_t cfg = { .max_windows_per_workspace = 10,
                        .max_workspaces = 32,
                        .enable_borders = true };
    gf_layout_engine_t engine = { .apply_layout = layout };
    gf_platform_t platform = { .window_enumerate = enumerate,
                               .window_get_focused = get_focused,
                               .window_is_minimized = is_minimized,
                               .window_is_maximized = is_maximized,
                               .window_is_fullscreen = no_state,
                               .window_is_interacting = interacting,
                               .window_minimize = minimize,
                               .window_unminimize = restore,
                               .window_set_maximized = set_maximized,
                               .window_get_geometry = get_geometry,
                               .window_set_geometry = set_geometry,
                               .workspace_get_count = workspace_count,
                               .monitor_enumerate = monitors,
                               .monitor_from_window = monitor_from_window,
                               .screen_get_bounds_for_monitor = bounds,
                               .border_add = add_border,
                               .border_remove = remove_border,
                               .dock_sync = dock_sync };
    platform.window_fill_maximized = fill_maximized;
    gf_wm_t m = { .platform = &platform, .layout = &engine, .config = &cfg };
    assert (gf_window_list_init (&m.state.windows, 16) == GF_SUCCESS);
    assert (gf_workspace_list_init (&m.state.workspaces, 16) == GF_SUCCESS);
    focused = handle (1);
    physical[3] = 1;
    for (unsigned i = 1; i <= 3; i++)
        bounds (NULL, physical[i], &actual[i]);
    tick (&m);

    // A briefly stale foreground handle cannot restore a user-minimized app.
    minimized[1] = true;
    tick (&m);
    assert (minimized[1] && restores[1] == 0 && !win (&m, 1)->monitor_suspended);
    minimized[1] = false;
    tick (&m);

    maximized[1] = true;
    uint32_t tiled_writes = writes[1];
    tick (&m);
    gf_ws_id_t main_max_ws = win (&m, 1)->workspace_id;
    for (unsigned i = 0; i < 30; i++)
        tick (&m);
    assert (win (&m, 1)->is_maximized && win (&m, 1)->workspace_id == main_max_ws);
    assert (writes[1] == tiled_writes && minimizes[2] == 1);
    assert (!minimized[1] && !minimized[3]);
    assert (dock_hidden[0] && !dock_hidden[1]);
    assert (filled[1] && !filled[2] && !filled[3]);
    focused = handle (3);
    tick (&m);
    assert (!minimized[1] && !bordered[1] && bordered[3]);
    assert (dock_hidden[0] && !dock_hidden[1]);

    maximized[1] = false;
    focused = handle (1);
    tick (&m);
    assert (win (&m, 1)->workspace_id == 1 && !minimized[2]);
    assert (!dock_hidden[0] && !dock_hidden[1]);
    tiled_writes = writes[1];
    for (unsigned i = 0; i < 30; i++)
        tick (&m);
    assert (writes[1] == tiled_writes && !minimized[3]);

    maximized[3] = true;
    focused = handle (3);
    tick (&m);
    gf_ws_id_t external_max_ws = win (&m, 3)->workspace_id;
    assert (win (&m, 3)->is_maximized && !minimized[1] && !minimized[2]);
    assert (!dock_hidden[0] && dock_hidden[1]);
    assert (!filled[1] && !filled[2] && filled[3]);
    focused = handle (2);
    physical[2] = 1;
    actual[2].x = 2100;
    dragging = true;
    for (unsigned i = 0; i < 5; i++)
        tick (&m);
    assert (win (&m, 2)->monitor_id == 0 && !minimized[2] && !minimized[3]);
    dragging = false;
    tick (&m);
    assert (win (&m, 2)->monitor_id == 1 && win (&m, 2)->workspace_id == 33);
    assert (minimized[3] && win (&m, 3)->is_maximized && win (&m, 3)->monitor_suspended);
    assert (!dock_hidden[0] && !dock_hidden[1]);
    assert (actual[1].width == 1920 && actual[2].x == 1920 && !minimized[2]);
    assert (!filled[1] && !filled[2] && !filled[3]);
    assert (bordered[2] && !bordered[3] && bordered[1]);
    for (unsigned i = 0; i < 30; i++)
        tick (&m);
    assert (win (&m, 3)->workspace_id == external_max_ws && minimizes[3] == 1);

    minimized[3] = false;
    focused = handle (3);
    tick (&m);
    assert (minimized[2] && !minimized[1] && !minimized[3]);
    physical[3] = 0;
    actual[3].x = 0;
    tick (&m);
    assert (win (&m, 3)->monitor_id == 0 && win (&m, 3)->restore_workspace_id == 1);
    assert (!minimized[2] && minimized[1] && !minimized[3]);
    assert (m.state.workspaces.active_workspace[1] == 33);
    assert (dock_hidden[0] && !dock_hidden[1]);
    assert (!filled[1] && !filled[2] && filled[3]);

    // User minimization stays minimized; a native false-max flag is not unmaximize.
    minimized[3] = true;
    focused = handle (2);
    for (unsigned i = 0; i < 30; i++)
        tick (&m);
    assert (minimized[3] && win (&m, 3)->is_maximized && win (&m, 3)->monitor_id == 0);
    assert (!dock_hidden[0] && !dock_hidden[1]);
    maximized[3] = false;
    minimized[3] = false;
    focused = handle (3);
    tick (&m);
    assert (!win (&m, 3)->is_maximized && win (&m, 3)->workspace_id == 1);
    assert (!minimized[1] && !minimized[2]);

    // A title-bar drag temporarily clears the OS maximize flag. On release,
    // retain its logical mode, activate the destination, and restore the source.
    focused = handle (1);
    maximized[1] = true;
    tick (&m);
    dragging = true;
    maximized[1] = false;
    physical[1] = 1;
    actual[1].x = 2050;
    for (unsigned i = 0; i < 3; i++)
        tick (&m);
    assert (win (&m, 1)->monitor_id == 0 && win (&m, 1)->is_maximized);
    dragging = false;
    delay_mode[1] = true;
    platform.window_maximize_async = true;
    uint32_t previous_mode_requests = mode_requests[1];
    tick (&m);
    for (unsigned i = 0; i < 3; i++)
        tick (&m);
    assert (win (&m, 1)->monitor_id == 1 && win (&m, 1)->is_maximized);
    assert (mode_requests[1] == previous_mode_requests + 1 && requested_mode[1]);
    assert (actual[1].x == 1920 && actual[1].width == 1280);
    assert (minimized[2] && !minimized[3] && bordered[3] && !bordered[1]);
    assert (m.state.workspaces.active_workspace[0] == 1);
    assert (!dock_hidden[0] && dock_hidden[1]);
    // During native restore/maximize, a saved source rectangle can briefly
    // report the old display. It must not initiate a transfer back.
    physical[1] = 0;
    actual[1].x = 0;
    tick (&m);
    assert (win (&m, 1)->monitor_id == 1 && requested_mode[1]);
    assert (mode_requests[1] == previous_mode_requests + 1);
    physical[1] = 1;
    bounds (NULL, 1, &actual[1]);
    delay_mode[1] = false;
    maximized[1] = true;
    tick (&m);
    platform.window_maximize_async = false;

    // Delayed minimize acknowledgements cannot undo the normal selection,
    // repeatedly send minimize requests, or change another monitor's borders.
    focused = handle (2);
    minimized[2] = false;
    delay_visibility[1] = true;
    uint32_t previous_minimizes = minimizes[1];
    m.state.last_active_window[1] = 0;
    m.state.last_active_workspace[1] = 33;
    tick (&m);
    for (unsigned i = 0; i < 3; i++)
        tick (&m);
    assert (!minimized[1] && win (&m, 1)->is_minimized);
    assert (minimizes[1] == previous_minimizes + 1 && win (&m, 1)->is_maximized);
    assert (m.state.workspaces.active_workspace[1] == 33 && bordered[2] && bordered[3]);
    assert (!dock_hidden[0] && !dock_hidden[1]);
    minimized[1] = true;
    delay_visibility[1] = false;
    tick (&m);

    // Reopening the maximized app hides only its destination's normal tiles.
    minimized[1] = false;
    focused = handle (1);
    tick (&m);
    assert (minimized[2] && !minimized[3]);

    // Moving a normal tile onto that display selects normal mode even if the
    // OS snaps it to maximized at drop, and minimizes the old maximized app.
    focused = handle (3);
    dragging = true;
    physical[3] = 1;
    actual[3].x = 2050;
    tick (&m);
    maximized[3] = true;
    dragging = false;
    delay_visibility[2] = true;
    uint32_t previous_restores = restores[2];
    tick (&m);
    assert (win (&m, 3)->monitor_id == 1 && !win (&m, 3)->is_maximized);
    assert (!maximized[3] && !requested_mode[3] && minimized[1]);
    for (unsigned i = 0; i < 3; i++)
        tick (&m);
    assert (minimized[2] && !win (&m, 2)->is_minimized);
    assert (win (&m, 2)->monitor_id == 1 && restores[2] == previous_restores + 1);
    assert (!bordered[2] && bordered[3]);
    delay_visibility[2] = false;
    minimized[2] = false;
    tick (&m);
    assert (!minimized[2] && bordered[2] && bordered[3]);

    // Restore the fixture for geometry rejection checks below.
    focused = handle (1);
    minimized[1] = false;
    physical[1] = 0;
    actual[1].x = 0;
    tick (&m);
    maximized[1] = false;
    tick (&m);
    focused = handle (3);
    physical[3] = 0;
    actual[3].x = 0;
    tick (&m);
    assert (!minimized[1] && !minimized[2] && !win (&m, 1)->is_maximized);

    // A permanent native geometry rejection has bounded attempts and recovers
    // after the next actual state change, without disturbing the other display.
    rejected = handle (1);
    gf_ws_id_t normal = 1;
    gf_window_list_mark_all_needs_update (&m.state.windows, &normal);
    for (unsigned i = 0; i < 10; i++)
        tick (&m);
    assert (failures == 3 && !win (&m, 1)->needs_update);
    actual[1].x += 5;
    tick (&m);
    assert (failures == 4 && win (&m, 1)->needs_update);
    missing_external = true;
    gf_rect_t *rects = NULL;
    assert (gf_wm_calculate_layout (&m, win (&m, 2), 1, 1, &rects)
            == GF_ERROR_DISPLAY_CONNECTION);
    assert (!rects);

    // Failed maximized sizing cannot cause an endless polling/arrange loop.
    focused = handle (1);
    maximized[1] = true;
    rejected_fill = handle (1);
    for (unsigned i = 0; i < 30; i++)
    {
        tick (&m);
    }
    assert (fill_failures == 3 && win (&m, 1)->maximize_fill_failures == 3);
    maximized[1] = false;
    tick (&m);
    assert (fill_failures == 3 && win (&m, 1)->maximize_fill_failures == 0);
    rejected_fill = NULL;

    // A cancelled minimize can arrive late. Retain the restore selection until
    // it settles and repair the late iconify response without changing identity.
    gf_win_info_t pending = { .id = handle (4), .monitor_id = 1 };
    delay_visibility[4] = true;
    assert (wm_request_visibility (&m, &pending, true) == GF_SUCCESS);
    assert (wm_request_visibility (&m, &pending, false) == GF_SUCCESS);
    for (unsigned i = 0; i < 3; i++)
    {
        bool observed_min = false, observed_max = false;
        wm_observe_window_state (&m, &pending, &observed_min, &observed_max);
        wm_request_visibility (&m, &pending, false);
    }
    assert (pending.visibility_request == 2);
    minimized[4] = true;
    bool observed_min = true, observed_max = false;
    wm_observe_window_state (&m, &pending, &observed_min, &observed_max);
    delay_visibility[4] = false;
    wm_request_visibility (&m, &pending, false);
    assert (!minimized[4] && pending.monitor_id == 1 && !pending.visibility_request);

    // Native minimize failures also have bounded retries.
    rejected_visibility = handle (4);
    uint32_t previous_failures = minimizes[4];
    for (unsigned i = 0; i < 30; i++)
    {
        observed_min = false;
        wm_observe_window_state (&m, &pending, &observed_min, &observed_max);
        wm_request_visibility (&m, &pending, true);
    }
    assert (minimizes[4] == previous_failures + 3);

    // An asynchronous re-maximize can initially report the old "maximized"
    // flag, briefly restore, then maximize again. The first flag is not an
    // acknowledgement of the queued operation.
    pending.visibility_request = pending.visibility_wait = 0;
    pending.is_minimized = false;
    pending.is_maximized = maximized[4] = true;
    physical[4] = 1;
    platform.window_maximize_async = true;
    wm_request_maximized (&m, &pending);
    assert (pending.mode_wait == 6);
    for (unsigned i = 0; i < 6; i++)
    {
        observed_min = false;
        observed_max = i != 2;
        wm_observe_window_state (&m, &pending, &observed_min, &observed_max);
        assert (observed_max && pending.mode_wait == 5 - i);
    }
    gf_window_list_cleanup (&m.state.windows);
    gf_workspace_list_cleanup (&m.state.workspaces);
    puts ("Monitor isolation and maximize transition regressions passed");
    return 0;
}
