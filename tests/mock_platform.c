#include "mock_platform.h"
#include "../src/utils/memory.h"

#include <string.h>

// The active state under test. Set by mock_use before each scenario.
static mock_state_t *g_mock = NULL;

void
mock_use (mock_state_t *st)
{
    g_mock = st;
    if (st)
    {
        st->enumerate_calls = 0;
        st->op_counter = 0;
        st->dock_restore_seq = -1;
        st->first_show_seq = -1;
    }
}

static void
record_show (void)
{
    if (g_mock->first_show_seq < 0)
        g_mock->first_show_seq = ++g_mock->op_counter;
}

void
mock_add (mock_state_t *st, gf_handle_t id, unsigned long desktop, const char *cls)
{
    mock_window_t *w = &st->wins[st->count++];
    w->id = id;
    w->desktop = desktop;
    w->desktop_readable = true;
    w->maximized = false;
    w->excluded = false;
    w->cls = cls;
}

static mock_window_t *
find (gf_handle_t id)
{
    for (uint32_t i = 0; i < g_mock->count; i++)
        if (g_mock->wins[i].id == id)
            return &g_mock->wins[i];
    return NULL;
}

static bool
resolve (const mock_window_t *w, const gf_ws_id_t *ws_filter, gf_ws_id_t *out_ws)
{
    // Unreadable desktop is never trackable, in either mode.
    if (!w->desktop_readable)
        return false;

    bool sticky = (w->desktop == MOCK_STICKY_DESKTOP);

    if (ws_filter != NULL)
    {
        if (!sticky && (gf_ws_id_t)w->desktop != *ws_filter)
            return false;
        *out_ws = *ws_filter;
        return true;
    }

    *out_ws = sticky ? 0 : (gf_ws_id_t)w->desktop;
    return true;
}

static gf_err_t
mock_window_enumerate (gf_display_t display, gf_ws_id_t *workspace_id,
                       gf_win_info_t **windows, uint32_t *count)
{
    (void)display;
    g_mock->enumerate_calls++;

    gf_win_info_t *out = gf_malloc (sizeof (gf_win_info_t) * (g_mock->count + 1));
    uint32_t n = 0;

    for (uint32_t i = 0; i < g_mock->count; i++)
    {
        mock_window_t *w = &g_mock->wins[i];
        gf_ws_id_t ws = 0;
        if (!resolve (w, workspace_id, &ws))
            continue;

        out[n] = (gf_win_info_t){ .id = w->id,
                                  .workspace_id = ws,
                                  .monitor_id = 0,
                                  .geometry = { 0, 0, 100, 100 },
                                  .is_maximized = w->maximized,
                                  .is_minimized = false,
                                  .needs_update = false,
                                  .is_valid = !w->excluded,
                                  .last_modified = 0 };
        n++;
    }

    *windows = out;
    *count = n;
    return GF_SUCCESS;
}

static uint32_t
mock_workspace_get_count (gf_display_t display)
{
    (void)display;
    return g_mock->workspace_count;
}

static bool
mock_window_is_maximized (gf_display_t display, gf_handle_t window)
{
    (void)display;
    mock_window_t *w = find (window);
    return w && w->maximized;
}

static bool
mock_window_is_excluded (gf_display_t display, gf_handle_t window)
{
    (void)display;
    mock_window_t *w = find (window);
    return w && w->excluded;
}

static bool
mock_window_is_fullscreen (gf_display_t display, gf_handle_t window)
{
    (void)display;
    (void)window;
    return false;
}

static gf_handle_t
mock_window_get_focused (gf_display_t display)
{
    (void)display;
    return g_mock->focused;
}

static gf_monitor_id_t
mock_monitor_from_window (gf_platform_t *platform, gf_handle_t window)
{
    (void)platform;
    (void)window;
    return 0;
}

static gf_err_t
mock_window_unminimize (gf_display_t display, gf_handle_t window)
{
    (void)display;
    (void)window;
    g_mock->unminimize_calls++;
    record_show ();
    return GF_SUCCESS;
}

static void
mock_dock_hide (gf_platform_t *platform)
{
    (void)platform;
}

static void
mock_dock_restore (gf_platform_t *platform)
{
    (void)platform;
    g_mock->dock_restore_seq = ++g_mock->op_counter;
}

static gf_err_t
mock_window_minimize (gf_display_t display, gf_handle_t window)
{
    (void)display;
    (void)window;
    g_mock->minimize_calls++;
    return GF_SUCCESS;
}

static void
mock_window_get_class (gf_display_t display, gf_handle_t win, char *buffer,
                       size_t bufsize)
{
    (void)display;
    mock_window_t *w = find (win);
    const char *name = (w && w->cls) ? w->cls : "";
    strncpy (buffer, name, bufsize - 1);
    buffer[bufsize - 1] = '\0';
}

gf_platform_t *
mock_platform (void)
{
    static gf_platform_t p;
    memset (&p, 0, sizeof (p));
    p.window_enumerate = mock_window_enumerate;
    p.workspace_get_count = mock_workspace_get_count;
    p.window_is_maximized = mock_window_is_maximized;
    p.window_is_excluded = mock_window_is_excluded;
    p.window_is_fullscreen = mock_window_is_fullscreen;
    p.window_get_focused = mock_window_get_focused;
    p.monitor_from_window = mock_monitor_from_window;
    p.window_get_class = mock_window_get_class;
    p.window_unminimize = mock_window_unminimize;
    p.window_minimize = mock_window_minimize;
    p.dock_hide = mock_dock_hide;
    p.dock_restore = mock_dock_restore;
    return &p;
}
