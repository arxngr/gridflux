#include "internal.h"
#include <minwindef.h>

BOOL
_is_app_window (HWND hwnd)
{
    if (!IsWindowVisible (hwnd))
        return FALSE;

    HWND owner = GetWindow (hwnd, GW_OWNER);
    LONG exStyle = GetWindowLong (hwnd, GWL_EXSTYLE);

    if ((exStyle & WS_EX_TOOLWINDOW) && !(exStyle & WS_EX_APPWINDOW))
        return FALSE;

    if (owner != NULL && !(exStyle & WS_EX_APPWINDOW))
        return FALSE;

    return TRUE;
}
void
_get_window_name (gf_display_t display, HWND window, char *buffer, size_t bufsize)
{
    (void)display;

    if (!window || !buffer || bufsize == 0)
        return;

    buffer[0] = '\0';

    if (!_validate_window (window))
        return;

    int len = GetWindowTextA (window, buffer, (int)bufsize - 1);
    if (len > 0)
        buffer[len] = '\0';
    else
        buffer[0] = '\0';
}

BOOL
_is_excluded_class (const char *class_name, const char *title)
{
    static const char *excluded_classes[]
        = { "Shell_TrayWnd",
            "Shell_SecondaryTrayWnd",
            "TrayNotifyWnd",
            "NotifyIconOverflowWindow",
            "Windows.UI.Core.CoreWindow",
            "Xaml_Windowed_Popup",
            "Progman",
            "WorkerW",
            "TopLevelWindowForOverflowXamlIsland",
            "Windows.Internal.Shell.NotificationCenter",
            "NativeHWNDHost",
            "Windows.UI.Composition.DesktopWindowContentBridge",
            "Xaml",
            "Overflow",
            "SDL_app",
            "ApplicationFrameWindow" };

    for (size_t i = 0; i < sizeof (excluded_classes) / sizeof (excluded_classes[0]); i++)
    {
        if (strcmp (class_name, excluded_classes[i]) == 0)
            return true;
    }

    return false;
}

BOOL
_is_fullscreen_window (HWND hwnd)
{
    if (!_validate_window (hwnd) || !IsWindowVisible (hwnd))
        return false;

    if (IsZoomed (hwnd))
        return false;

    if (GetWindow (hwnd, GW_OWNER))
        return false;

    LONG style = GetWindowLong (hwnd, GWL_STYLE);

    RECT win, screen;
    // Use GetWindowRect for screen occupancy check as it's more reliable
    if (!GetWindowRect (hwnd, &win))
        return false;

    HMONITOR mon = MonitorFromWindow (hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { .cbSize = sizeof (mi) };
    GetMonitorInfo (mon, &mi);
    screen = mi.rcMonitor;

    // Tolerance of 10 pixels for DPI scaling/rounding
    int t = 10;
    bool covers_screen
        = (win.left <= screen.left + t && win.top <= screen.top + t
           && win.right >= screen.right - t && win.bottom >= screen.bottom - t);

    if (covers_screen)
    {
        // Many games use WS_POPUP (no borders/title bar)
        if (style & WS_POPUP)
            return true;

        // Also check if it lacks a caption (standard for borderless fullscreen)
        if (!(style & WS_CAPTION))
            return true;

        // Final fallback: if it strictly covers the whole screen
        if (win.left <= screen.left && win.top <= screen.top && win.right >= screen.right
            && win.bottom >= screen.bottom)
            return true;
    }

    return false;
}

BOOL
_is_notification_center (HWND hwnd)
{
    char class_name[MAX_CLASS_NAME_LENGTH];
    if (!GetClassNameA (hwnd, class_name, sizeof (class_name)))
        return false;

    if (strstr (class_name, "Windows.Internal.Shell"))
        return true;

    if (strstr (class_name, "NotificationCenter"))
        return true;

    if (strcmp (class_name, "Windows.UI.Core.CoreWindow") == 0)
        return true;

    LONG exstyle = GetWindowLongA (hwnd, GWL_EXSTYLE);
    if (exstyle & WS_EX_NOACTIVATE)
    {
        char title[MAX_TITLE_LENGTH];
        GetWindowTextA (hwnd, title, sizeof (title));
        if (title[0] == '\0' || strstr (title, "Notification"))
            return true;
    }

    return false;
}

BOOL
_is_excluded_style (HWND hwnd)
{
    LONG exstyle = GetWindowLongA (hwnd, GWL_EXSTYLE);

    if (exstyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))
        return true;

    char title[MAX_TITLE_LENGTH];
    _get_window_name (NULL, hwnd, title, sizeof (title));

    if ((exstyle & WS_EX_TOPMOST) && title[0] == '\0')
        return true;

    return false;
}

BOOL
_is_cloaked_window (HWND hwnd)
{
    DWORD cloaked = 0;
    HRESULT hr = DwmGetWindowAttribute (hwnd, DWMWA_CLOAKED, &cloaked, sizeof (cloaked));
    return SUCCEEDED (hr) && cloaked;
}

BOOL
_validate_window (HWND hwnd)
{
    return hwnd != NULL && IsWindow (hwnd);
}

void
_get_window_geometry (HWND hwnd, gf_rect_t *rect)
{
    RECT r;
    if (GetWindowRect (hwnd, &r))
    {
        rect->x = r.left;
        rect->y = r.top;
        rect->width = (gf_dimension_t)(r.right - r.left);
        rect->height = (gf_dimension_t)(r.bottom - r.top);
    }
}

void
_get_taskbar_dimensions (int *left, int *right, int *top, int *bottom)
{
    *left = *right = *top = *bottom = 0;

    APPBARDATA abd = { .cbSize = sizeof (abd) };
    if (SHAppBarMessage (ABM_GETTASKBARPOS, &abd))
    {
        switch (abd.uEdge)
        {
        case ABE_LEFT:
            *left = abd.rc.right - abd.rc.left;
            break;
        case ABE_RIGHT:
            *right = abd.rc.right - abd.rc.left;
            break;
        case ABE_TOP:
            *top = abd.rc.bottom - abd.rc.top;
            break;
        case ABE_BOTTOM:
            *bottom = abd.rc.bottom - abd.rc.top;
            break;
        }
    }
}
