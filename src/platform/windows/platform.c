#include "../../utils/logger.h"
#include "../../utils/memory.h"
#include "internal.h"
#include <string.h>

extern gf_err_t gf_keymap_init (gf_platform_t *platform, gf_display_t display);
extern void gf_keymap_cleanup (gf_platform_t *platform);
extern gf_key_action_t gf_keymap_poll (gf_platform_t *platform, gf_display_t display);
extern gf_err_t gf_resize_hook_install (gf_platform_t *platform);
extern void gf_resize_hook_uninstall (gf_platform_t *platform);
extern bool gf_resize_poll (gf_platform_t *platform, gf_resize_event_t *event);

// Bind the window enumeration, info, geometry and state operations.
static void
_platform_bind_window_ops (gf_platform_t *p)
{
    // --- Window Enumeration & Info ---
    p->window_enumerate = gf_platform_get_windows;
    p->window_get_focused = gf_window_get_focused;
    p->window_get_class = gf_window_get_class;

    // --- Window Geometry & State ---
    p->window_get_geometry = gf_window_get_geometry;
    p->window_is_excluded = gf_window_is_excluded;
    p->window_is_fullscreen = gf_window_is_fullscreen;
    p->window_is_hidden = gf_platform_window_hidden;
    p->window_is_maximized = gf_window_is_maximized;
    p->window_is_minimized = gf_platform_window_minimized;
    p->window_is_valid = gf_window_is_valid;
    p->window_minimize = gf_window_minimize;
    p->window_set_geometry = gf_window_set_geometry;
    p->window_unminimize = gf_window_unminimize;
}

// Bind lifecycle, screen, border, dock, monitor, keymap and resize operations.
static void
_platform_bind_system_ops (gf_platform_t *p)
{
    // --- Lifecycle & Core ---
    p->init = gf_platform_init;
    p->cleanup = gf_platform_cleanup;

    // --- Workspace & Screen ---
    p->screen_get_bounds = gf_screen_get_bounds;
    p->workspace_get_count = gf_workspace_get_count;

    // --- Border Management ---
    p->border_add = gf_border_add;
    p->border_cleanup = gf_border_cleanup;
    p->border_remove = gf_border_remove;
    p->border_update = gf_border_update;

    // --- Dock Management ---
    p->dock_hide = gf_dock_hide;
    p->dock_restore = gf_dock_restore;

    // --- Monitor Management ---
    p->monitor_get_count = gf_monitor_get_count;
    p->monitor_enumerate = gf_monitor_enumerate;
    p->monitor_from_window = gf_monitor_from_window;
    p->screen_get_bounds_for_monitor = gf_screen_get_bounds_for_monitor;

    // --- Keymap Support ---
    p->keymap_init = gf_keymap_init;
    p->keymap_cleanup = gf_keymap_cleanup;
    p->keymap_poll = gf_keymap_poll;

    // --- Resize Interaction ---
    p->resize_hook_install = gf_resize_hook_install;
    p->resize_hook_uninstall = gf_resize_hook_uninstall;
    p->resize_poll = gf_resize_poll;
}

gf_platform_t *
gf_platform_create (void)
{
    gf_platform_t *platform = gf_malloc (sizeof (gf_platform_t));
    if (!platform)
        return NULL;

    gf_windows_platform_data_t *data = gf_malloc (sizeof (gf_windows_platform_data_t));
    if (!data)
    {
        gf_free (platform);
        return NULL;
    }

    memset (platform, 0, sizeof (gf_platform_t));
    memset (data, 0, sizeof (gf_windows_platform_data_t));

    _platform_bind_window_ops (platform);
    _platform_bind_system_ops (platform);

    platform->platform_data = data;

    return platform;
}

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT) - 4)
#endif

// Request Per-Monitor-V2 DPI awareness so GetWindowRect stays in the same
// (physical) coordinate space as the DWM extended-frame bounds we subtract from
// it -- without this, tiling and borders drift on mixed-DPI multi-monitor
// setups. The app manifest declares the same awareness; this covers hosts where
// the embedded manifest is not honoured. Falls back to system DPI on older OSes.
static void
_ensure_dpi_awareness (void)
{
    HMODULE user32 = GetModuleHandleW (L"user32.dll");
    BOOL (WINAPI * set_ctx) (DPI_AWARENESS_CONTEXT) = NULL;
    if (user32)
        set_ctx = (BOOL (WINAPI *) (DPI_AWARENESS_CONTEXT))GetProcAddress (
            user32, "SetProcessDpiAwarenessContext");
    if (!set_ctx || !set_ctx (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDPIAware ();
}

gf_err_t
gf_platform_init (gf_platform_t *platform, gf_display_t *display)
{
    GF_LOG_INFO ("Initialize Windows platform...");

    if (!display)
        return GF_ERROR_INVALID_PARAMETER;

    *display = NULL;

    _ensure_dpi_awareness ();

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    // Capture original dock (taskbar) state before we start managing it
    APPBARDATA abd = { .cbSize = sizeof (abd) };
    abd.hWnd = FindWindowA ("Shell_TrayWnd", NULL);
    if (abd.hWnd)
    {
        data->original_dock_state = (UINT)SHAppBarMessage (ABM_GETSTATE, &abd);
        data->dock_state_saved = true;
        GF_LOG_INFO ("Captured original dock state: %u", data->original_dock_state);
    }

    data->monitor_count = GetSystemMetrics (SM_CMONITORS);

    // Enumerate monitors and cache their bounds
    uint32_t mon_count = GF_MAX_MONITORS;
    gf_monitor_enumerate (platform, data->monitors, &mon_count);

    if (platform->resize_hook_install)
        platform->resize_hook_install (platform);

    GF_LOG_INFO ("Platform initialized successfully (monitors: %d)", data->monitor_count);
    return GF_SUCCESS;
}

void
gf_platform_cleanup (gf_display_t display, gf_platform_t *platform)
{
    (void)display;

    if (!platform || !platform->platform_data)
        return;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    // Restore original dock state on exit
    if (data->dock_state_saved)
    {
        APPBARDATA abd = { .cbSize = sizeof (abd) };
        abd.hWnd = FindWindowA ("Shell_TrayWnd", NULL);
        if (abd.hWnd)
        {
            abd.lParam = data->original_dock_state;
            SHAppBarMessage (ABM_SETSTATE, &abd);
            GF_LOG_INFO ("Restored original dock state: %u", data->original_dock_state);
        }
    }

    if (platform->resize_hook_uninstall)
        platform->resize_hook_uninstall (platform);

    gf_border_cleanup (platform);

    GF_LOG_INFO ("Platform cleaned up");
}

void
gf_platform_destroy (gf_platform_t *platform)
{
    if (!platform)
        return;

    gf_free (platform->platform_data);
    gf_free (platform);
}
