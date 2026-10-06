#include "config/excludes.h"
#include "platform/windows/internal.h"
#include "utils/logger.h"
#include <assert.h>
#include <stdio.h>

// Exercise real Win32 overlay geometry, regions, visibility, and recovery.
// The test target is a nonactivating tool window outside the visible desktop.
static HWND clip_window;

bool
gf_exclude_list_contains (const gf_exclude_list_t *list, const char *name)
{
    (void)list;
    (void)name;
    return false;
}
gf_monitor_id_t
gf_monitor_from_window (gf_platform_t *platform, gf_handle_t window)
{
    (void)platform;
    (void)window;
    return 1;
}
static void
check_overlay (gf_windows_platform_data_t *data)
{
    assert (data->border_count == 1);
    gf_border_t *border = data->borders[0];
    assert (border->monitor_id == 1 && IsWindowVisible (border->overlay));
    RECT rect;
    assert (GetWindowRect (border->overlay, &rect));
    assert (rect.right > rect.left && rect.bottom > rect.top);
    HRGN region = CreateRectRgn (0, 0, 0, 0);
    assert (GetWindowRgn (border->overlay, region) == COMPLEXREGION);
    DeleteObject (region);
}
int
main (void)
{
    char desktop_name[64];
    snprintf (desktop_name, sizeof (desktop_name), "GridFluxBorderTest%lu",
              GetCurrentProcessId ());
    HDESK previous = GetThreadDesktop (GetCurrentThreadId ());
    HDESK desktop = CreateDesktopA (desktop_name, NULL, NULL, 0, GENERIC_ALL, NULL);
    assert (desktop && SetThreadDesktop (desktop));
    gf_log_init (GF_LOG_ERROR);
    WNDCLASSA cls = { .lpfnWndProc = DefWindowProcA,
                      .hInstance = GetModuleHandle (NULL),
                      .lpszClassName = "GridFluxBorderTest" };
    assert (RegisterClassA (&cls));
    // A hidden console or another window sharing the application title is not
    // the tray GUI. It must not make this process's border overlays occluders.
    HWND same_title
        = CreateWindowExA (WS_EX_TOOLWINDOW, cls.lpszClassName, "GridFlux", WS_POPUP, 0,
                           0, 1, 1, NULL, NULL, cls.hInstance, NULL);
    assert (same_title && !IsWindowVisible (same_title));
    HWND target = CreateWindowExA (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName,
                                   "GridFlux border regression", WS_POPUP, -30000, -30000,
                                   320, 240, NULL, NULL, cls.hInstance, NULL);
    assert (target);
    ShowWindow (target, SW_SHOWNOACTIVATE);
    DwmFlush ();
    gf_windows_platform_data_t data = { 0 };
    gf_platform_t platform = { .platform_data = &data };
    gf_config_t config = { .enable_borders = true, .border_color = 0x00ff00 };
    gf_border_add (&platform, target, config.border_color, 3);
    check_overlay (&data);
    gf_border_update (&platform, &config);
    check_overlay (&data);

    // Dedicated tray identity still identifies GUI popups, and destroying the
    // tray must immediately stop treating that process as a GUI owner.
    WNDCLASSA tray_cls = { .lpfnWndProc = DefWindowProcA,
                           .hInstance = cls.hInstance,
                           .lpszClassName = "GridFluxTrayClass" };
    assert (RegisterClassA (&tray_cls));
    HWND tray = CreateWindowExA (0, tray_cls.lpszClassName, "GridFluxTray", WS_POPUP, 0,
                                 0, 1, 1, NULL, NULL, cls.hInstance, NULL);
    assert (tray && gf_window_is_border_excluded (target));
    assert (DestroyWindow (tray));
    assert (!gf_window_is_border_excluded (target));
    assert (UnregisterClassA (tray_cls.lpszClassName, cls.hInstance));
    gf_border_update (&platform, &config);
    check_overlay (&data);

    // Native window state can invalidate an overlay region without changing
    // the target frame. The geometry cache must not keep an empty border.
    HRGN empty = CreateRectRgn (0, 0, 0, 0);
    assert (empty && SetWindowRgn (data.borders[0]->overlay, empty, TRUE));
    gf_border_update (&platform, &config);
    check_overlay (&data);

    // A real covering GUI window may intentionally clip the entire border.
    // Keep that region empty until the window is hidden, minimized, or cloaked.
    clip_window = CreateWindowExA (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "#32770",
                                   "GridFlux clipping regression", WS_POPUP, -30010,
                                   -30010, 350, 270, NULL, NULL, cls.hInstance, NULL);
    assert (clip_window);
    ShowWindow (clip_window, SW_SHOWNOACTIVATE);
    DwmFlush ();
    RECT region_rect;
    for (int i = 0; i < 2; i++)
    {
        gf_border_update (&platform, &config);
        assert (GetWindowRgnBox (data.borders[0]->overlay, &region_rect) == NULLREGION);
    }
    // Layered helper windows can keep WS_VISIBLE while painting no pixels.
    // Their full frame must not erase the border when the tray GUI starts.
    SetWindowLongPtr (clip_window, GWL_EXSTYLE,
                      GetWindowLongPtr (clip_window, GWL_EXSTYLE) | WS_EX_LAYERED);
    assert (SetLayeredWindowAttributes (clip_window, 0, 0, LWA_ALPHA));
    DwmFlush ();
    gf_border_update (&platform, &config);
    check_overlay (&data);
    assert (SetLayeredWindowAttributes (clip_window, 0, 255, LWA_ALPHA));
    gf_border_update (&platform, &config);
    assert (GetWindowRgnBox (data.borders[0]->overlay, &region_rect) == NULLREGION);
    empty = CreateRectRgn (0, 0, 0, 0);
    assert (empty && SetWindowRgn (clip_window, empty, TRUE));
    gf_border_update (&platform, &config);
    check_overlay (&data);
    assert (SetWindowRgn (clip_window, NULL, TRUE));
    ShowWindow (clip_window, SW_SHOWMINNOACTIVE);
    assert (IsWindowVisible (clip_window) && IsIconic (clip_window));
    gf_border_update (&platform, &config);
    check_overlay (&data);
    ShowWindow (clip_window, SW_SHOWNOACTIVATE);
    BOOL cloaked = TRUE;
    assert (SUCCEEDED (
        DwmSetWindowAttribute (clip_window, DWMWA_CLOAK, &cloaked, sizeof (cloaked))));
    DwmFlush ();
    gf_border_update (&platform, &config);
    check_overlay (&data);
    DestroyWindow (clip_window);
    clip_window = NULL;

    ShowWindow (target, SW_HIDE);
    gf_border_update (&platform, &config);
    assert (!IsWindowVisible (data.borders[0]->overlay));
    ShowWindow (target, SW_SHOWNOACTIVATE);
    gf_border_update (&platform, &config);
    check_overlay (&data);

    // A destroyed native overlay must be recreated rather than kept as an
    // existing border with an invalid HWND.
    assert (DestroyWindow (data.borders[0]->overlay));
    gf_border_add (&platform, target, config.border_color, 3);
    check_overlay (&data);
    gf_border_cleanup (&platform);
    assert (data.border_count == 0);
    DestroyWindow (target);
    DestroyWindow (same_title);
    UnregisterClassA (cls.lpszClassName, cls.hInstance);
    assert (SetThreadDesktop (previous));
    assert (CloseDesktop (desktop));
    puts ("Windows border initialization and recovery regressions passed");
    return 0;
}
