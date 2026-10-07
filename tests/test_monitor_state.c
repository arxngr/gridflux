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
static bool dragging, missing_external, fourth_window;
static gf_key_action_t key_action;
static bool displays_ready = true;
static unsigned recovery_requests[5];
static gf_handle_t rejected_recovery;
static bool manually_moved[5];

static bool
was_moved (gf_display_t display, gf_handle_t window)
{
    (void)display;
    unsigned i = (unsigned)(uintptr_t)window;
    bool moved = manually_moved[i];
    manually_moved[i] = false;
    return moved;
}

static bool
poll_monitors (gf_platform_t *platform)
{
    (void)platform;
    return displays_ready;
}
static gf_err_t
recover_monitor (gf_platform_t *platform, const gf_win_info_t *window,
                 const gf_rect_t *previous_bounds)
{
    (void)platform;
    unsigned i = (unsigned)(uintptr_t)window->id;
    recovery_requests[i]++;
    if (window->id == rejected_recovery)
        return GF_ERROR_PLATFORM_ERROR;
    physical[i] = window->monitor_id;
    actual[i] = window->geometry;
    actual[i].x += (window->monitor_id == 1 ? 1920 : 0) - previous_bounds->x;
    return GF_SUCCESS;
}

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
const char *
gf_config_get_path (void)
{
    return NULL;
}
void
gf_config_save (const char *path, const gf_config_t *cfg)
{
    (void)path;
    (void)cfg;
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
static void
get_class (gf_display_t display, gf_handle_t window, char *name, size_t size)
{
    (void)display;
    snprintf (name, size, "test-app-%u", index_of (window));
}

static gf_handle_t
get_focused (gf_display_t display)
{
    (void)display;
    return focused;
}
static gf_err_t
focus_window (gf_display_t display, gf_handle_t window)
{
    (void)display;
    if (minimized[index_of (window)])
        return GF_ERROR_PLATFORM_ERROR;
    focused = window;
    return GF_SUCCESS;
}
static gf_key_action_t
poll_keymap (gf_platform_t *platform, gf_display_t display)
{
    (void)platform;
    (void)display;
    gf_key_action_t action = key_action;
    key_action = GF_KEY_NONE;
    return action;
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
    *count = fourth_window ? 4 : 3;
    *out = gf_calloc (*count, sizeof (**out));
    for (unsigned i = 1; i <= *count; i++)
    {
        (*out)[i - 1]
            = (gf_win_info_t){ .id = handle (i),
                               .workspace_id = 1,
                               .monitor_id = physical[i],
                               .geometry = actual[i],
                               .is_valid = true,
                               .is_minimized = minimized[i],
                               .is_maximized = is_maximized (NULL, handle (i)) };
        get_class (display, handle (i), (*out)[i - 1].name, sizeof ((*out)[i - 1].name));
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
    if (m->state.monitors_paused)
        return;
    gf_wm_event (m);
    wm_sync_dock_visibility (m);
    gf_wm_layout_rebalance (m);
    gf_wm_layout_apply (m);
    for (unsigned i = 0; i < m->state.windows.count; i++)
    {
        gf_win_info_t *w = &m->state.windows.items[i];
        gf_ws_info_t *ws
            = gf_workspace_list_find_by_id (&m->state.workspaces, w->workspace_id);
        assert (w->monitor_id < m->state.monitor_count);
        assert (gf_workspace_has_monitor (ws, w->monitor_id));
        if (w->restore_workspace_id)
        {
            ws = gf_workspace_list_find_by_id (&m->state.workspaces,
                                               w->restore_workspace_id);
            assert (ws && ws->monitor_id == w->monitor_id && !ws->has_maximized_state);
        }
    }
}
static void
test_sleep_resume (gf_wm_t *template)
{
    gf_wm_t m = { .platform = template->platform,
                  .layout = template->layout,
                  .config = template->config };
    gf_platform_t *ops = m.platform;
    gf_platform_t platform = *ops;
    m.platform = &platform;
    assert (gf_window_list_init (&m.state.windows, 16) == GF_SUCCESS);
    assert (gf_workspace_list_init (&m.state.workspaces, 16) == GF_SUCCESS);
    tick (&m);
    // Sleep/wake temporarily moves windows in both directions. Preserve the
    // original normal/maximized/minimized workspace IDs until recovery runs.
    platform.monitor_poll = poll_monitors;
    platform.window_restore_monitor = recover_monitor;
    maximized[1] = true;
    tick (&m);
    gf_ws_id_t saved_workspace[4], saved_restore[4], saved_active[2];
    bool saved_minimized[4];
    for (unsigned i = 1; i <= 3; i++)
    {
        saved_workspace[i] = win (&m, i)->workspace_id;
        saved_restore[i] = win (&m, i)->restore_workspace_id;
        saved_minimized[i] = minimized[i];
    }
    memcpy (saved_active, m.state.workspaces.active_workspace, sizeof (saved_active));
    unsigned saved_writes = writes[1] + writes[2] + writes[3];
    unsigned saved_minimizes = minimizes[1] + minimizes[2] + minimizes[3];
    displays_ready = false;
    m.state.resize_active = true;
    physical[1] = 1;
    physical[3] = 0;
    for (unsigned step = 0; step < 12; step++)
    {
        tick (&m);
        gf_wm_event (&m);
        gf_wm_keymap_event (&m);
        wm_sync_dock_visibility (&m);
        gf_wm_layout_apply (&m);
        gf_wm_layout_rebalance (&m);
        assert (m.state.monitors_paused);
        assert (!m.state.resize_active);
        assert (writes[1] + writes[2] + writes[3] == saved_writes);
        assert (minimizes[1] + minimizes[2] + minimizes[3] == saved_minimizes);
        for (unsigned i = 1; i <= 3; i++)
        {
            assert (win (&m, i)->workspace_id == saved_workspace[i]);
            assert (win (&m, i)->restore_workspace_id == saved_restore[i]);
            assert (win (&m, i)->monitor_id == (i == 3 ? 1u : 0u));
        }
    }
    displays_ready = true;
    tick (&m);
    assert (!m.state.monitors_paused && !m.state.monitors_recovering);
    assert (physical[1] == 0 && physical[3] == 1 && maximized[1]);
    for (unsigned i = 1; i <= 3; i++)
    {
        assert (recovery_requests[i] == 1);
        assert (win (&m, i)->workspace_id == saved_workspace[i]);
        assert (win (&m, i)->restore_workspace_id == saved_restore[i]);
        assert (minimized[i] == saved_minimized[i]);
    }
    assert (
        memcmp (saved_active, m.state.workspaces.active_workspace, sizeof (saved_active))
        == 0);
    // Recovery failures are bounded; they must never cause another arrange loop.
    displays_ready = false;
    tick (&m);
    displays_ready = true;
    rejected_recovery = handle (3);
    for (unsigned step = 0; step < 3; step++)
        tick (&m);
    assert (!m.state.monitors_paused && recovery_requests[3] == 4);
    rejected_recovery = 0;
    // A real move after recovery still transfers to the destination workspace.
    physical[1] = 1;
    tick (&m);
    assert (win (&m, 1)->monitor_id == 1 && win (&m, 1)->is_maximized);
    physical[1] = 0;
    tick (&m);
    maximized[1] = false;
    tick (&m);
    // The shared excluded workspace keeps the same ID on both monitors after
    // wake; recovery preserves native maximize without adding a managed mode.
    assert (gf_excludes_add (m.config, "test-app-2") == GF_SUCCESS);
    assert (gf_excludes_add (m.config, "test-app-3") == GF_SUCCESS);
    focused = handle (2);
    minimized[2] = false;
    tick (&m);
    focused = handle (3);
    minimized[3] = false;
    maximized[3] = true;
    tick (&m);
    gf_ws_id_t shared = win (&m, 2)->workspace_id;
    assert (shared == win (&m, 3)->workspace_id);
    displays_ready = false;
    physical[2] = 1;
    physical[3] = 0;
    tick (&m);
    displays_ready = true;
    tick (&m);
    assert (physical[2] == 0 && physical[3] == 1);
    assert (win (&m, 2)->workspace_id == shared && win (&m, 3)->workspace_id == shared);
    assert (maximized[3] && !win (&m, 3)->is_maximized);
    assert (!bordered[2] && !bordered[3] && !dock_hidden[0] && !dock_hidden[1]);
    assert (gf_excludes_remove (m.config, "test-app-2") == GF_SUCCESS);
    assert (gf_excludes_remove (m.config, "test-app-3") == GF_SUCCESS);
    focused = handle (1);
    platform.monitor_poll = NULL;
    platform.window_restore_monitor = NULL;

    gf_window_list_cleanup (&m.state.windows);
    gf_workspace_list_cleanup (&m.state.workspaces);
    memset (minimized, 0, sizeof (minimized));
    memset (maximized, 0, sizeof (maximized));
    memset (writes, 0, sizeof (writes));
    memset (minimizes, 0, sizeof (minimizes));
    memset (restores, 0, sizeof (restores));
    memset (mode_requests, 0, sizeof (mode_requests));
    memset (recovery_requests, 0, sizeof (recovery_requests));
    for (unsigned i = 1; i <= 3; i++)
        bounds (NULL, physical[i], &actual[i]);
}
static void
reset_desktop (void)
{
    memset (minimized, 0, sizeof (minimized));
    memset (maximized, 0, sizeof (maximized));
    memset (bordered, 0, sizeof (bordered));
    memset (filled, 0, sizeof (filled));
    memset (writes, 0, sizeof (writes));
    memset (minimizes, 0, sizeof (minimizes));
    memset (restores, 0, sizeof (restores));
    memset (mode_requests, 0, sizeof (mode_requests));
    memset (recovery_requests, 0, sizeof (recovery_requests));
    memset (manually_moved, 0, sizeof (manually_moved));
    missing_external = dragging = fourth_window = false;
    displays_ready = true;
    focused = handle (1);
    physical[1] = physical[2] = 0;
    physical[3] = 1;
    for (unsigned i = 1; i <= 3; i++)
        bounds (NULL, physical[i], &actual[i]);
}

static void
test_monitor_reconnect (gf_wm_t *template)
{
    for (unsigned scenario = 0; scenario < 7; scenario++)
    {
        reset_desktop ();
        gf_config_t cfg = *template->config;
        gf_platform_t platform = *template->platform;
        platform.monitor_poll = poll_monitors;
        platform.window_restore_monitor = recover_monitor;
        platform.window_was_moved = was_moved;
        gf_wm_t m = { .platform = &platform, .layout = template->layout, .config = &cfg };
        assert (gf_window_list_init (&m.state.windows, 16) == GF_SUCCESS);
        assert (gf_workspace_list_init (&m.state.workspaces, 16) == GF_SUCCESS);
        focused = handle (3);
        if (scenario == 1)
            maximized[3] = true;
        if (scenario == 3)
            assert (gf_excludes_add (&cfg, "test-app-3") == GF_SUCCESS);
        tick (&m);
        gf_ws_id_t original = win (&m, 3)->workspace_id;
        gf_ws_id_t restore_id = win (&m, 3)->restore_workspace_id;
        if (scenario == 2)
        {
            minimized[3] = true;
            focused = handle (1);
            tick (&m);
        }
        // Accept a real unplug, after Windows has relocated visible apps.
        displays_ready = false;
        physical[3] = 0;
        tick (&m);
        missing_external = true;
        displays_ready = true;
        tick (&m);
        assert (win (&m, 3)->monitor_return.pending);
        assert (win (&m, 3)->monitor_return.monitor_id == 1);
        assert (win (&m, 3)->monitor_return.workspace_id == original);
        assert (win (&m, 3)->monitor_id == 0 && physical[3] == 0);
        for (unsigned step = 0; step < 20; step++)
            tick (&m);
        assert (win (&m, 3)->monitor_return.pending);
        if (scenario == 4)
        {
            // A title-bar move within the temporary monitor cancels return.
            actual[3].x += 30;
            manually_moved[3] = true;
            tick (&m);
            assert (!win (&m, 3)->monitor_return.pending);
        }
        if (scenario == 5)
        {
            // A manual move during the unsettled reconnect must win as well.
            displays_ready = false;
            manually_moved[3] = true;
            tick (&m);
            assert (!win (&m, 3)->monitor_return.pending);
        }
        missing_external = false;
        displays_ready = true;
        if (scenario == 6)
            rejected_recovery = handle (3);
        tick (&m);
        for (unsigned step = 0; step < 10; step++)
            tick (&m);
        if (scenario == 6)
        {
            assert (win (&m, 3)->monitor_return.pending);
            assert (win (&m, 3)->monitor_return.failures == 3);
            assert (physical[3] == 0 && recovery_requests[3] == 4);
            rejected_recovery = 0;
            // A later topology transition starts a fresh, bounded attempt.
            missing_external = true;
            tick (&m);
            missing_external = false;
            tick (&m);
            assert (physical[3] == 1 && !win (&m, 3)->monitor_return.pending);
        }
        assert (!win (&m, 3)->monitor_return.pending);
        if (scenario == 4 || scenario == 5)
            assert (physical[3] == 0 && win (&m, 3)->monitor_id == 0);
        else
        {
            assert (physical[3] == 1 && win (&m, 3)->monitor_id == 1);
            assert (win (&m, 3)->workspace_id == original);
            assert (win (&m, 3)->restore_workspace_id == restore_id);
            assert (maximized[3] == (scenario == 1));
            if (scenario == 2)
                assert (minimized[3]);
            if (scenario == 3)
                assert (!bordered[3] && !dock_hidden[1]);
        }
        if (scenario == 3)
            assert (gf_excludes_remove (&cfg, "test-app-3") == GF_SUCCESS);
        gf_window_list_cleanup (&m.state.windows);
        gf_workspace_list_cleanup (&m.state.workspaces);
    }
    reset_desktop ();
}

static void
test_monitor_rules (gf_wm_t *template)
{
    reset_desktop ();
    gf_config_t cfg = *template->config;
    gf_platform_t platform = *template->platform;
    platform.window_restore_monitor = recover_monitor;
    gf_wm_t m = { .platform = &platform, .layout = template->layout, .config = &cfg };
    assert (gf_window_list_init (&m.state.windows, 16) == GF_SUCCESS);
    assert (gf_workspace_list_init (&m.state.workspaces, 16) == GF_SUCCESS);
    tick (&m);
    assert (gf_rules_add (&cfg, "test-app-1", 3, 1) == GF_SUCCESS);
    assert (gf_rules_add (&cfg, "test-app-3", 3, -1) == GF_SUCCESS);
    tick (&m);
    assert (win (&m, 1)->monitor_id == 1 && physical[1] == 1);
    assert (win (&m, 1)->workspace_id == 35);
    assert (win (&m, 3)->monitor_id == 1 && win (&m, 3)->workspace_id == 35);
    unsigned saved_requests = recovery_requests[1];
    for (unsigned step = 0; step < 20; step++)
        tick (&m);
    assert (recovery_requests[1] == saved_requests);
    assert (wm_workspace_monitor_window_count (&m, 35, 1) == 2);
    // Editing a rule moves an existing app and uses the monitor-local number.
    assert (gf_rules_add (&cfg, "test-app-1", 2, 0) == GF_SUCCESS);
    focused = handle (1);
    minimized[1] = false;
    tick (&m);
    assert (win (&m, 1)->monitor_id == 0 && win (&m, 1)->workspace_id == 2);
    maximized[1] = true;
    tick (&m);
    bool main_minimized = minimized[2];
    assert (gf_rules_add (&cfg, "test-app-1", 3, 1) == GF_SUCCESS);
    tick (&m);
    assert (maximized[1] && win (&m, 1)->is_maximized);
    assert (win (&m, 1)->monitor_id == 1 && win (&m, 1)->restore_workspace_id == 35);
    assert (minimized[2] == main_minimized);
    // A disconnected rule target is deferred, then enforced on reconnect.
    missing_external = true;
    tick (&m);
    assert (win (&m, 1)->monitor_id == 0 && win (&m, 1)->monitor_return.pending);
    unsigned requests = recovery_requests[1];
    for (unsigned step = 0; step < 20; step++)
        tick (&m);
    assert (recovery_requests[1] == requests);
    missing_external = false;
    tick (&m);
    assert (win (&m, 1)->monitor_id == 1 && maximized[1]);
    assert (win (&m, 1)->restore_workspace_id == 35);
    // Exclusion continues to own the shared workspace, ahead of pinning rules.
    assert (gf_excludes_add (&cfg, "test-app-1") == GF_SUCCESS);
    assert (gf_rules_add (&cfg, "test-app-1", 2, 0) == GF_SUCCESS);
    tick (&m);
    assert (win (&m, 1)->monitor_id == 1);
    gf_ws_info_t *ws
        = gf_workspace_list_find_by_id (&m.state.workspaces, win (&m, 1)->workspace_id);
    assert (ws && ws->is_excluded_ws && !bordered[1]);
    assert (gf_excludes_remove (&cfg, "test-app-1") == GF_SUCCESS);
    gf_window_list_cleanup (&m.state.windows);
    gf_workspace_list_cleanup (&m.state.workspaces);
    reset_desktop ();
}

static void
test_reconnect_retile (gf_wm_t *template)
{
    reset_desktop ();
    fourth_window = true;
    physical[4] = 1;
    bounds (NULL, 1, &actual[4]);
    gf_config_t cfg = *template->config;
    gf_platform_t platform = *template->platform;
    gf_wm_t m = { .platform = &platform, .config = &cfg, .layout = template->layout };
    assert (gf_window_list_init (&m.state.windows, 16) == GF_SUCCESS);
    assert (gf_workspace_list_init (&m.state.workspaces, 16) == GF_SUCCESS);
    tick (&m);
    gf_workspace_list_ensure (&m.state.workspaces, 35, 10, 1, 3);
    wm_move_window_to_workspace (&m, win (&m, 4), 35);
    wm_switch_workspace (&m, 35, 1);
    wm_place_window_on_monitor (&m, win (&m, 3), 0);
    physical[3] = 0;
    gf_ws_info_t *original = gf_workspace_list_find_by_id (&m.state.workspaces, 35);
    original->is_locked = original->is_custom_layout = true;
    win (&m, 3)->monitor_return
        = (gf_monitor_return_t){ .pending = true, .monitor_id = 1, .workspace_id = 35 };
    // A resident window is already settled; a later return must retile it too.
    for (unsigned i = 1; i <= 4; i++)
        win (&m, i)->needs_update = false;
    actual[4].height = 30;
    physical[3] = 1;
    unsigned resident_writes = writes[4];
    wm_return_window_to_monitor (&m, win (&m, 3));
    assert (m.state.workspaces.active_workspace[1] == 35);
    assert (win (&m, 3)->workspace_id == 35 && win (&m, 4)->workspace_id == 35);
    assert (win (&m, 4)->needs_update && !original->is_custom_layout);
    assert (gf_wm_layout_apply (&m) == GF_SUCCESS);
    assert (writes[4] > resident_writes && actual[4].height == 1080);
    assert (actual[3].width == 640 && actual[4].width == 640);
    assert (!minimized[1] && !minimized[2]);
    gf_window_list_cleanup (&m.state.windows);
    gf_workspace_list_cleanup (&m.state.workspaces);
    reset_desktop ();
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
                               .window_get_class = get_class,
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
    test_sleep_resume (&m);
    test_monitor_reconnect (&m);
    test_monitor_rules (&m);
    test_reconnect_retile (&m);
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
    // Excluding an already maximized app releases GridFlux's mode without
    // changing the app's native maximize state or the other monitor.
    missing_external = false;
    rejected = rejected_visibility = NULL;
    platform.window_maximize_async = false;
    focused = handle (1);
    maximized[1] = true;
    minimized[1] = false;
    tick (&m);
    assert (dock_hidden[0] && filled[1]);
    unsigned mode_before = mode_requests[1], writes_before = writes[1];
    unsigned min_before = minimizes[1], restore_before = restores[1];
    bool external_min = minimized[2], external_dock = dock_hidden[1];
    assert (gf_excludes_add (&cfg, "test-app-1") == GF_SUCCESS);
    tick (&m);
    gf_ws_info_t *excluded_ws
        = gf_workspace_list_find_by_id (&m.state.workspaces, win (&m, 1)->workspace_id);
    assert (excluded_ws && excluded_ws->is_excluded_ws
            && excluded_ws->monitor_id == GF_MONITOR_SHARED);
    gf_ws_id_t shared_excluded_id = excluded_ws->id;
    assert (!excluded_ws->has_maximized_state && excluded_ws->window_count == 1);
    assert (m.state.workspaces.active_workspace[0] == excluded_ws->id);
    assert (!bordered[1] && !win (&m, 1)->is_maximized && maximized[1]);
    assert (!dock_hidden[0] && !filled[1]);
    assert (minimized[2] == external_min && dock_hidden[1] == external_dock);
    for (unsigned i = 0; i < 10; i++)
        tick (&m);
    assert (mode_requests[1] == mode_before && writes[1] == writes_before);
    assert (minimizes[1] == min_before && restores[1] == restore_before);

    // Native mode and geometry remain the app's choice. A transfer selects
    // its own destination workspace and restores the vacated source monitor.
    maximized[1] = false;
    physical[1] = 1;
    actual[1].x = 2050;
    tick (&m);
    excluded_ws
        = gf_workspace_list_find_by_id (&m.state.workspaces, win (&m, 1)->workspace_id);
    assert (win (&m, 1)->monitor_id == 1 && excluded_ws->monitor_id == GF_MONITOR_SHARED);
    assert (excluded_ws->id == shared_excluded_id);
    assert (m.state.workspaces.active_workspace[1] == excluded_ws->id);
    assert (m.state.workspaces.active_workspace[0] == 1 && !minimized[3]);
    assert (minimized[2] && !minimized[1]);
    assert (!bordered[1] && !dock_hidden[1] && writes[1] == writes_before);
    minimized[1] = true;
    for (unsigned i = 0; i < 3; i++)
        tick (&m);
    assert (minimized[1] && restores[1] == restore_before);
    minimized[1] = false;
    maximized[1] = true;
    tick (&m);
    assert (maximized[1] && !dock_hidden[1] && !win (&m, 1)->is_maximized);

    // A second excluded app reuses the populated shared workspace. Opening it
    // on a monitor already viewing that workspace does not hide its siblings.
    assert (gf_excludes_add (&cfg, "test-app-4") == GF_SUCCESS);
    fourth_window = true;
    physical[4] = 1;
    minimized[4] = false;
    maximized[4] = true;
    bounds (NULL, 1, &actual[4]);
    min_before = minimizes[4];
    tick (&m);
    for (unsigned i = 0; i < 3; i++)
        tick (&m);
    assert (!win (&m, 4)->is_maximized && maximized[4] && !minimized[4]);
    assert (win (&m, 4)->workspace_id == shared_excluded_id);
    gf_ws_info_t *second_excluded_ws
        = gf_workspace_list_find_by_id (&m.state.workspaces, win (&m, 4)->workspace_id);
    assert (second_excluded_ws->is_excluded_ws
            && second_excluded_ws->monitor_id == GF_MONITOR_SHARED);
    assert (!second_excluded_ws->has_maximized_state
            && second_excluded_ws->window_count == 2);
    assert (!bordered[4] && minimizes[4] == min_before && !filled[4]);

    // A maximized workspace on monitor 0 remains live while monitor 1 switches
    // between excluded and normal workspaces.
    focused = handle (3);
    maximized[3] = true;
    minimized[3] = false;
    tick (&m);
    assert (dock_hidden[0] && !dock_hidden[1]);
    gf_ws_id_t behind = m.state.workspaces.active_workspace[0];
    min_before = minimizes[3];
    focused = handle (4);
    minimized[4] = false;
    tick (&m);
    assert (dock_hidden[0] && !dock_hidden[1]);
    assert (m.state.workspaces.active_workspace[0] == behind);
    assert (m.state.workspaces.active_workspace[1] == win (&m, 4)->workspace_id);
    assert (!minimized[1] && minimized[2]);
    assert (win (&m, 3)->is_maximized && !minimized[3] && !minimized[4]);
    assert (minimizes[3] == min_before && !bordered[4] && !filled[4]);
    focused = handle (2);
    minimized[2] = false;
    tick (&m);
    assert (minimized[1] && minimized[4] && !minimized[2] && bordered[2]);
    assert (m.state.workspaces.active_workspace[1] == win (&m, 2)->workspace_id);
    assert (dock_hidden[0] && !dock_hidden[1]);

    // Selecting an excluded app immediately selects the shared workspace and
    // restores its native mode, with no geometry, border, or maximize writes.
    focused = handle (1);
    minimized[1] = false;
    tick (&m);
    assert (m.state.workspaces.active_workspace[1] == win (&m, 1)->workspace_id);
    assert (minimized[2] && !minimized[4] && !minimized[1] && maximized[1]);
    assert (!win (&m, 1)->is_maximized && !bordered[1] && !dock_hidden[1]);
    assert (mode_requests[1] == mode_before && writes[1] == writes_before);

    // A delayed workspace minimize cannot defeat the first selection of an
    // excluded app. A late acknowledgement is repaired on its own monitor.
    focused = handle (2);
    minimized[2] = false;
    delay_visibility[1] = true;
    tick (&m);
    assert (win (&m, 1)->visibility_request == 1 && !minimized[1]);
    focused = handle (1);
    tick (&m);
    assert (m.state.workspaces.active_workspace[1] == win (&m, 1)->workspace_id);
    assert (win (&m, 1)->visibility_request == 2 && !minimized[1]);
    minimized[1] = true;
    delay_visibility[1] = false;
    tick (&m);
    assert (!minimized[1] && win (&m, 1)->monitor_id == 1 && maximized[1]);
    assert (dock_hidden[0] && !dock_hidden[1] && !minimized[3]);

    // Removing an exclusion adopts the current native mode once. It must
    // neither draw a border on a maximized app nor require another selection.
    assert (gf_excludes_remove (&cfg, "test-app-1") == GF_SUCCESS);
    focused = handle (1);
    tick (&m);
    assert (win (&m, 1)->is_maximized && !bordered[1] && dock_hidden[1]);
    maximized[1] = false;
    tick (&m);
    assert (!win (&m, 1)->is_maximized && bordered[1] && !dock_hidden[1]);
    assert (dock_hidden[0] && minimized[4] && !bordered[4]);

    // Excluding a background tile moves it to the shared inactive workspace.
    // A single user selection restores it and hides the previous workspace.
    restore_before = restores[2];
    assert (wm_request_visibility (&m, win (&m, 2), true) == GF_SUCCESS);
    assert (minimized[2] && win (&m, 2)->monitor_suspended);
    assert (gf_excludes_add (&cfg, "test-app-2") == GF_SUCCESS);
    tick (&m);
    assert (minimized[2] && !bordered[2] && restores[2] == restore_before);
    assert (win (&m, 2)->workspace_id != win (&m, 1)->workspace_id);
    assert (win (&m, 2)->workspace_id == win (&m, 4)->workspace_id);
    focused = handle (2);
    minimized[2] = false;
    tick (&m);
    assert (m.state.workspaces.active_workspace[1] == win (&m, 2)->workspace_id);
    assert (!minimized[2] && minimized[1] && !bordered[2]);
    for (unsigned i = 0; i < 2; i++)
        tick (&m);
    assert (restores[2] == restore_before && !win (&m, 2)->monitor_suspended);

    // Keyboard navigation visits the shared workspace once, restores its
    // siblings, and keeps the other monitor's maximized state live.
    platform.window_focus = focus_window;
    platform.keymap_poll = poll_keymap;
    m.state.keymap_initialized = true;
    unsigned visited = 0;
    for (unsigned i = 0; i < 2; i++)
    {
        key_action = GF_KEY_WORKSPACE_NEXT;
        gf_wm_keymap_event (&m);
        tick (&m);
        unsigned selected = index_of (focused);
        assert ((selected == 1 || selected == 2) && !(visited & (1u << selected)));
        visited |= 1u << selected;
        assert (m.state.workspaces.active_workspace[1]
                == win (&m, index_of (focused))->workspace_id);
        assert (!minimized[index_of (focused)] && !minimized[3] && dock_hidden[0]);
        if (focused != handle (1))
            assert (!bordered[index_of (focused)] && !dock_hidden[1] && !minimized[4]);
    }
    assert (visited == ((1u << 1) | (1u << 2)));

    // The same excluded workspace can be active on both monitors at once.
    // Moving one sibling leaves the other's source workspace active.
    physical[4] = 0;
    actual[4].x = 100;
    focused = handle (4);
    minimized[4] = false;
    tick (&m);
    assert (win (&m, 4)->workspace_id == shared_excluded_id);
    assert (m.state.workspaces.active_workspace[0] == shared_excluded_id);
    assert (m.state.workspaces.active_workspace[1] == shared_excluded_id);
    assert (!minimized[2] && !minimized[4] && minimized[1] && minimized[3]);
    assert (!dock_hidden[0] && !dock_hidden[1] && !bordered[2] && !bordered[4]);
    assert (gf_workspace_list_get_current (&m.state.workspaces, 0)
            == gf_workspace_list_get_current (&m.state.workspaces, 1));
    assert (wm_workspace_monitor_window_count (&m, shared_excluded_id, 0) == 1);
    assert (wm_workspace_monitor_window_count (&m, shared_excluded_id, 1) == 1);

    // Switching one monitor to normal hides only its excluded windows.
    focused = handle (1);
    minimized[1] = false;
    tick (&m);
    assert (minimized[2] && !minimized[4] && !minimized[1] && minimized[3]);
    assert (m.state.workspaces.active_workspace[0] == shared_excluded_id);
    focused = handle (3);
    minimized[3] = false;
    tick (&m);
    assert (minimized[4] && minimized[2] && !minimized[1] && !minimized[3]);
    assert (dock_hidden[0] && !dock_hidden[1]);
    focused = handle (2);
    minimized[2] = false;
    tick (&m);
    assert (!minimized[2] && minimized[4] && !minimized[3] && minimized[1]);

    // Repeated transfers reuse the same global ID and never create another
    // excluded workspace, even with a tile capacity below the excluded count.
    for (unsigned i = 0; i < 6; i++)
    {
        physical[4] = i % 2;
        actual[4].x = physical[4] ? 2100 : 100;
        focused = handle (4);
        minimized[4] = false;
        tick (&m);
        assert (win (&m, 4)->workspace_id == shared_excluded_id);
        assert (win (&m, 2)->workspace_id == shared_excluded_id);
        unsigned excluded_count = 0;
        for (unsigned j = 0; j < m.state.workspaces.count; j++)
            excluded_count += m.state.workspaces.items[j].is_excluded_ws;
        assert (excluded_count == 1);
    }
    cfg.max_windows_per_workspace = 1;
    tick (&m);
    excluded_ws = gf_workspace_list_find_by_id (&m.state.workspaces, shared_excluded_id);
    assert (excluded_ws->window_count == 2 && excluded_ws->available_space > 0);
    assert (gf_workspace_has_monitor (excluded_ws, 0)
            && gf_workspace_has_monitor (excluded_ws, 1));
    assert (!gf_workspace_has_monitor (excluded_ws, GF_MONITOR_SHARED));
    gf_exclude_list_free (&cfg.excluded_apps);
    gf_window_list_cleanup (&m.state.windows);
    gf_workspace_list_cleanup (&m.state.workspaces);
    puts ("Monitor isolation and maximize transition regressions passed");
    return 0;
}
