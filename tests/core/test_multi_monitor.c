#include "core/internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static gf_rect_t actual[4];
static uint32_t writes[4];
static gf_handle_t failed;
static bool dragging;
static gf_resize_event_t gesture;
static bool fake_minimized[4];
static bool fake_bordered[4];

bool
wm_user_excluded (gf_wm_t *m, gf_handle_t window)
{
    (void)m;
    (void)window;
    return false;
}

static uintptr_t
fake_index (gf_handle_t window)
{
    return (uintptr_t)window;
}

static gf_err_t
fake_minimize (gf_display_t display, gf_handle_t window)
{
    (void)display;
    fake_minimized[fake_index (window)] = true;
    return GF_SUCCESS;
}

static gf_err_t
fake_unminimize (gf_display_t display, gf_handle_t window)
{
    (void)display;
    fake_minimized[fake_index (window)] = false;
    return GF_SUCCESS;
}

static bool
fake_is_minimized (gf_display_t display, gf_handle_t window)
{
    (void)display;
    return fake_minimized[fake_index (window)];
}

static void
fake_border_add (gf_platform_t *platform, gf_handle_t window, gf_color_t color,
                 int thickness)
{
    (void)platform;
    (void)color;
    (void)thickness;
    fake_bordered[fake_index (window)] = true;
}

static void
fake_border_remove (gf_platform_t *platform, gf_handle_t window)
{
    (void)platform;
    fake_bordered[fake_index (window)] = false;
}

void
gf_log (gf_log_level_t level, const char *format, ...)
{
    (void)level;
    (void)format;
}

// Workspace assignment is independent of these layout and gesture regressions.
void
wm_assign_windows_to_workspaces (gf_wm_t *m)
{
    (void)m;
}
gf_monitor_id_t
wm_find_active_monitor (gf_wm_t *m)
{
    (void)m;
    return 0;
}

// This target exercises layout and resize behavior without linking the
// workspace manager. The production transfer path owns workspace reassignment.
void
wm_move_window_to_monitor (gf_wm_t *m, gf_win_info_t *win, gf_monitor_id_t monitor_id)
{
    gf_ws_id_t old_workspace = win->workspace_id;
    gf_ws_info_t *old_ws
        = gf_workspace_list_find_by_id (&m->state.workspaces, old_workspace);
    gf_ws_id_t target_workspace
        = old_ws ? gf_workspace_id_for_monitor_local (monitor_id, old_ws->local_id) : -1;
    gf_ws_info_t *target_ws
        = gf_workspace_list_find_by_id (&m->state.workspaces, target_workspace);
    win->monitor_id = monitor_id;
    if (target_ws && target_ws->monitor_id == monitor_id)
        win->workspace_id = target_workspace;
    gf_window_list_mark_all_needs_update (&m->state.windows, &old_workspace);
    if (win->workspace_id != old_workspace)
        gf_window_list_mark_all_needs_update (&m->state.windows, &win->workspace_id);
    for (uint32_t i = 0; i < m->state.workspaces.count; i++)
        if (m->state.workspaces.items[i].id == old_workspace
            || m->state.workspaces.items[i].id == win->workspace_id)
            m->state.workspaces.items[i].is_custom_layout = false;
}

void
wm_place_window_on_monitor (gf_wm_t *m, gf_win_info_t *win, gf_monitor_id_t monitor_id)
{
    wm_move_window_to_monitor (m, win, monitor_id);
}

void
wm_return_window_to_monitor (gf_wm_t *m, gf_win_info_t *win)
{
    wm_move_window_to_monitor (m, win, win->monitor_return.monitor_id);
    win->monitor_return.pending = false;
}

static gf_err_t
get_geometry (gf_display_t display, gf_handle_t window, gf_rect_t *out)
{
    (void)display;
    *out = actual[(uintptr_t)window];
    return GF_SUCCESS;
}

gf_err_t
gf_wm_window_sync (gf_wm_t *m, gf_handle_t window, gf_ws_id_t workspace)
{
    (void)workspace;
    gf_win_info_t *w = gf_window_list_find_by_window_id (&m->state.windows, window);
    w->geometry = actual[(uintptr_t)window];
    return GF_SUCCESS;
}

static gf_err_t
set_geometry (gf_display_t display, gf_handle_t window, const gf_rect_t *rect,
              gf_geom_flags_t flags, gf_config_t *config)
{
    (void)display;
    (void)flags;
    (void)config;
    if (window == failed)
        return GF_ERROR_PLATFORM_ERROR;
    actual[(uintptr_t)window] = *rect;
    writes[(uintptr_t)window]++;
    return GF_SUCCESS;
}

static gf_err_t
monitor_bounds (gf_display_t display, gf_monitor_id_t id, gf_rect_t *rect)
{
    (void)display;
    *rect = id == 0 ? (gf_rect_t){ 0, 0, 1920, 1080 }
                    : (gf_rect_t){ -1280, -200, 1280, 1024 };
    return GF_SUCCESS;
}

static gf_err_t
monitors (gf_platform_t *platform, gf_monitor_t *out, uint32_t *count)
{
    (void)platform;
    assert (*count >= 2);
    memset (out, 0, 2 * sizeof (*out));
    for (uint32_t i = 0; i < 2; i++)
    {
        out[i].id = i;
        monitor_bounds (NULL, i, &out[i].bounds);
    }
    *count = 2;
    return GF_SUCCESS;
}

static gf_monitor_id_t
monitor_from_window (gf_platform_t *platform, gf_handle_t window)
{
    (void)platform;
    gf_rect_t r = actual[(uintptr_t)window];
    int32_t cx = r.x + (int32_t)r.width / 2;
    return cx < 0 ? 1 : 0;
}

static void
layout (const gf_layout_engine_t *engine, const gf_win_info_t *windows, uint32_t count,
        const gf_rect_t *bounds, gf_rect_t *out)
{
    (void)engine;
    (void)windows;
    for (uint32_t i = 0; i < count; i++)
        out[i] = (gf_rect_t){ bounds->x + (int)(i * bounds->width / count), bounds->y,
                              bounds->width / count, bounds->height };
}

static bool
interacting (gf_display_t display)
{
    (void)display;
    return dragging;
}
static bool
poll (gf_platform_t *platform, gf_resize_event_t *out)
{
    (void)platform;
    *out = gesture;
    return true;
}

int
main (void)
{
    gf_config_t config = { .enable_live_resize = true, .enable_borders = true };
    gf_layout_engine_t engine = { .apply_layout = layout };
    gf_platform_t platform = { .window_set_geometry = set_geometry,
                               .window_get_geometry = get_geometry,
                               .monitor_enumerate = monitors,
                               .monitor_from_window = monitor_from_window,
                               .screen_get_bounds_for_monitor = monitor_bounds,
                               .window_minimize = fake_minimize,
                               .window_unminimize = fake_unminimize,
                               .window_is_minimized = fake_is_minimized,
                               .border_add = fake_border_add,
                               .border_remove = fake_border_remove,
                               .window_is_interacting = interacting,
                               .resize_poll = poll };
    gf_wm_t m = { .platform = &platform, .layout = &engine, .config = &config };
    assert (gf_window_list_init (&m.state.windows, 4) == GF_SUCCESS);
    assert (gf_workspace_list_init (&m.state.workspaces, 4) == GF_SUCCESS);
    gf_ws_info_t ws = { .id = 1, .local_id = 1, .monitor_id = 0 };
    gf_workspace_list_add (&m.state.workspaces, &ws);
    gf_ws_info_t external_ws = { .id = 33, .local_id = 1, .monitor_id = 1 };
    gf_workspace_list_add (&m.state.workspaces, &external_ws);
    m.state.workspaces.active_workspace[0] = 1;
    m.state.workspaces.active_workspace[1] = 33;
    for (uintptr_t i = 1; i <= 3; i++)
    {
        gf_win_info_t w = { .id = (gf_handle_t)i,
                            .workspace_id = i == 3 ? 33 : 1,
                            .monitor_id = i == 3 ? 1 : 0,
                            .is_valid = true };
        gf_window_list_add (&m.state.windows, &w);
    }

    // Every tile is written, including the second monitor, and negative origins survive.
    assert (gf_wm_layout_apply (&m) == GF_SUCCESS);
    assert (writes[1] == 1 && writes[2] == 1 && writes[3] == 1);
    assert (actual[1].width == 960 && actual[2].x == 960);
    assert (actual[3].x == -1280 && actual[3].y == -200);

    // A monitor transfer invalidates both the source and destination layouts.
    gf_win_info_t moved = m.state.windows.items[1];
    moved.monitor_id = 1;
    moved.workspace_id = 33;
    gf_window_list_update (&m.state.windows, &moved);
    for (uint32_t i = 0; i < 3; i++)
        assert (m.state.windows.items[i].needs_update);
    dragging = true;
    gf_wm_layout_apply (&m);
    assert (writes[1] == 1 && writes[2] == 1 && writes[3] == 1);
    dragging = false;
    gf_wm_layout_apply (&m);
    assert (actual[1].width == 1920);
    assert (actual[2].width == 640 && actual[3].width == 640);

    // A failed write remains pending while successful writes are cleared individually.
    gf_window_list_mark_all_needs_update (&m.state.windows, NULL);
    failed = (gf_handle_t)(uintptr_t)2;
    gf_wm_layout_apply (&m);
    assert (!m.state.windows.items[0].needs_update);
    assert (m.state.windows.items[1].needs_update);
    assert (!m.state.windows.items[2].needs_update);
    failed = 0;

    // Title-bar moves release the interaction guard without committing a custom resize.
    for (int live = 0; live <= 1; live++)
    {
        config.enable_live_resize = live;
        gesture = (gf_resize_event_t){ .window = (gf_handle_t)(uintptr_t)1,
                                       .phase = GF_RESIZE_ACTIVE,
                                       .direction = GF_RESIZE_NONE };
        gf_wm_resize_event (&m);
        assert (m.state.resize_active);
        gesture.phase = GF_RESIZE_COMPLETE;
        gf_wm_resize_event (&m);
        assert (!m.state.resize_active);
        assert (!m.state.workspaces.items[0].is_custom_layout);
        assert (m.state.windows.items[1].needs_update);
    }

    // Releasing a title-bar move on another monitor retags the window before layout.
    gf_window_list_clear_update_flags (&m.state.windows, -1);
    m.state.windows.items[0].monitor_id = 0;
    m.state.workspaces.items[0].is_custom_layout = true;
    actual[1] = (gf_rect_t){ 0, 0, 1920, 1080 };
    gesture = (gf_resize_event_t){ .window = (gf_handle_t)(uintptr_t)1,
                                   .phase = GF_RESIZE_ACTIVE,
                                   .direction = GF_RESIZE_NONE,
                                   .initial_rect = actual[1],
                                   .current_rect = actual[1] };
    gf_wm_resize_event (&m);
    assert (m.state.resize_active);
    actual[1] = (gf_rect_t){ -1200, -200, 1920, 1080 };
    gesture.phase = GF_RESIZE_COMPLETE;
    gesture.current_rect = actual[1];
    gf_wm_resize_event (&m);
    assert (!m.state.resize_active);
    assert (m.state.windows.items[0].monitor_id == 1);
    assert (!m.state.workspaces.items[0].is_custom_layout);
    for (uint32_t i = 0; i < 3; i++)
        assert (m.state.windows.items[i].needs_update);
    gf_wm_layout_apply (&m);
    assert (actual[1].x == -1280);
    assert (actual[1].width == 426);

    // Committing a resize on monitor 0 must preserve pending work on monitor 1.
    config.enable_live_resize = true;
    m.state.windows.items[0].monitor_id = 0;
    m.state.windows.items[0].workspace_id = 1;
    m.state.windows.items[1].monitor_id = 1;
    m.state.windows.items[1].workspace_id = 33;
    actual[1] = (gf_rect_t){ 0, 0, 1920, 1080 };
    m.state.windows.items[1].needs_update = true;
    gesture = (gf_resize_event_t){ .window = (gf_handle_t)(uintptr_t)1,
                                   .phase = GF_RESIZE_COMPLETE,
                                   .direction = GF_RESIZE_RIGHT,
                                   .initial_rect = actual[1],
                                   .current_rect = actual[1] };
    gesture.current_rect.width += 10;
    gf_wm_resize_event (&m);
    assert (m.state.workspaces.items[0].is_custom_layout);
    assert (m.state.windows.items[1].needs_update);

    // An untracked window closing during a gesture cannot leave layout paused.
    m.state.resize_active = true;
    gesture.window = (gf_handle_t)(uintptr_t)4;
    gf_wm_resize_event (&m);
    assert (!m.state.resize_active);

    // Each display retains borders for its own live normal workspace.
    gf_ws_info_t max_ws = { .id = 65, .monitor_id = 0, .has_maximized_state = true };
    gf_workspace_list_add (&m.state.workspaces, &max_ws);
    gf_win_info_t *main_max = &m.state.windows.items[0];
    gf_win_info_t *main_normal = &m.state.windows.items[1];
    gf_win_info_t *external_normal = &m.state.windows.items[2];
    main_max->monitor_id = 0;
    main_max->workspace_id = 65;
    main_max->is_maximized = true;
    main_max->is_minimized = false;
    main_max->monitor_suspended = false;
    main_normal->monitor_id = 0;
    main_normal->workspace_id = 1;
    main_normal->is_maximized = false;
    main_normal->is_minimized = false;
    external_normal->monitor_id = 1;
    external_normal->workspace_id = 33;
    external_normal->is_maximized = false;
    external_normal->is_minimized = false;
    fake_minimized[1] = false;
    fake_bordered[1] = true;
    fake_bordered[2] = true;
    fake_bordered[3] = false;
    m.state.workspaces.active_workspace[0] = 65;
    m.state.workspaces.active_workspace[1] = 33;

    wm_sync_monitor_activity (&m, 1);
    assert (!fake_minimized[1] && !main_max->monitor_suspended);
    assert (!fake_bordered[1] && !fake_bordered[2] && fake_bordered[3]);

    wm_sync_monitor_activity (&m, 0);
    assert (!fake_minimized[1] && !main_max->monitor_suspended);
    assert (fake_bordered[3]);

    // A normal workspace on the same monitor also hides its maximized workspace.
    m.state.workspaces.active_workspace[0] = 1;
    wm_sync_monitor_activity (&m, 0);
    assert (fake_minimized[1] && main_max->monitor_suspended);

    m.state.workspaces.active_workspace[0] = 65;
    wm_sync_monitor_activity (&m, 0);
    assert (!fake_minimized[1] && !main_max->monitor_suspended);
    fake_minimized[1] = true;
    main_max->is_minimized = true;
    wm_sync_monitor_activity (&m, 0);
    assert (fake_minimized[1] && !main_max->monitor_suspended);

    gf_window_list_cleanup (&m.state.windows);
    gf_workspace_list_cleanup (&m.state.workspaces);
    puts ("Multi-monitor and move regressions passed");
    return 0;
}
