#include "config/excludes.h"
#include "platform/windows/internal.h"
#include "utils/logger.h"
#include <assert.h>
#include <stdio.h>

// Exercise real Win32 overlay geometry, regions, visibility, and recovery.
// The test target is a nonactivating tool window outside the visible desktop.
void
gf_log (gf_log_level_t level, const char *format, ...)
{
    (void)level;
    (void)format;
}
bool
gf_exclude_list_contains (const gf_exclude_list_t *list, const char *name)
{
    (void)list;
    (void)name;
    return false;
}
BOOL
window_is_border_excluded (HWND hwnd)
{
    (void)hwnd;
    return FALSE;
}
void
gf_window_get_class (gf_display_t display, gf_handle_t window, char *out, size_t size)
{
    (void)display;
    (void)window;
    if (size)
        out[0] = '\0';
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
    WNDCLASSA cls = { .lpfnWndProc = DefWindowProcA,
                      .hInstance = GetModuleHandle (NULL),
                      .lpszClassName = "GridFluxBorderTest" };
    assert (RegisterClassA (&cls));
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
    UnregisterClassA (cls.lpszClassName, cls.hInstance);
    puts ("Windows border initialization and recovery regressions passed");
    return 0;
}
