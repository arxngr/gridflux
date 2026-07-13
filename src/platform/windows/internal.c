#include "internal.h"
#include <minwindef.h>

void
_window_get_name (gf_display_t display, HWND window, char *buffer, size_t bufsize)
{
    (void)display;

    if (!window || !buffer || bufsize == 0)
        return;

    buffer[0] = '\0';

    if (!window_validate (window))
        return;

    int len = GetWindowTextA (window, buffer, (int)bufsize - 1);
    if (len > 0)
        buffer[len] = '\0';
    else
        buffer[0] = '\0';
}

BOOL
window_is_app (HWND hwnd)
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
BOOL
window_is_excluded_class (const char *class_name)
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
            "vgui_test_shell",
            "tooltips_class32",
            "Valve001",
            "Steam",
            "TaskManagerWindow",
            "#32770",
            "ThumbnailDeviceHelperWnd",
            "RecordingAreaIndicatorWindow",
            "XamlExplorerHostIslandWindow_WASDK",
            "TWizardForm",
            "#32769"
            "XamlWindow" };

    for (size_t i = 0; i < sizeof (excluded_classes) / sizeof (excluded_classes[0]); i++)
    {
        if (strcmp (class_name, excluded_classes[i]) == 0)
            return true;
    }

    if (strstr (class_name, "SnippingTool") != NULL)
        return true;
    if (strstr (class_name, "ScreenClipping") != NULL)
        return true;
    if (strstr (class_name, "BcastDVR") != NULL)
        return true;
    if (strstr (class_name, "GameBar") != NULL)
        return true;
    if (strstr (class_name, "bcastdvr") != NULL)
        return true;

    return false;
}

BOOL
window_is_fullscreen (HWND hwnd)
{
    if (!window_validate (hwnd) || !IsWindowVisible (hwnd))
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
window_is_notification_center (HWND hwnd)
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
        return true;
    }

    return false;
}

BOOL
window_is_excluded_style (HWND hwnd)
{
    LONG exstyle = GetWindowLongA (hwnd, GWL_EXSTYLE);

    if (exstyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))
        return true;

    char class_name[MAX_CLASS_NAME_LENGTH] = { 0 };
    GetClassNameA (hwnd, class_name, sizeof (class_name));

    if ((exstyle & WS_EX_TOPMOST) && class_name[0] == '\0')
        return true;

    return false;
}

BOOL
window_is_cloaked (HWND hwnd)
{
    DWORD cloaked = 0;
    HRESULT hr = DwmGetWindowAttribute (hwnd, DWMWA_CLOAKED, &cloaked, sizeof (cloaked));
    return SUCCEEDED (hr) && cloaked;
}

BOOL
window_validate (HWND hwnd)
{
    return hwnd != NULL && IsWindow (hwnd);
}

bool
window_is_self (gf_display_t display, gf_handle_t window)
{
    (void)display;
    if (!window_validate (window))
        return false;

    char title[MAX_TITLE_LENGTH];
    _window_get_name (display, window, title, sizeof (title));

    // EXACT match for GridFlux GUI
    if (strcmp (title, "GridFlux") == 0)
        return true;

    char class_name[MAX_CLASS_NAME_LENGTH];
    if (GetClassNameA (window, class_name, sizeof (class_name)))
    {
        // GridFlux GUI class
        if (strcmp (class_name, "GridFluxGUI") == 0)
        {
            return true;
        }
    }

    return false;
}

// Resolve (and cache) the GridFlux GUI's process id from its own window,
// identified by window class/title rather than a spoofable executable name.
// Re-resolves if the cached process has exited.
static DWORD
gui_process_id (void)
{
    static DWORD cached = 0;
    if (cached)
    {
        HANDLE h = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, cached);
        if (h)
        {
            CloseHandle (h);
            return cached;
        }
        cached = 0;
    }

    HWND gui = FindWindowA ("GridFluxGUI", NULL);
    if (!gui)
        gui = FindWindowA (NULL, "GridFlux");
    if (gui)
        GetWindowThreadProcessId (gui, &cached);
    return cached;
}

// True if hwnd is owned by the GUI process. Its transient popups (colour
// palette, dropdowns) share that PID but not its title/class.
static bool
window_belongs_to_gui (HWND hwnd)
{
    DWORD gui_pid = gui_process_id ();
    if (!gui_pid)
        return false;

    DWORD pid = 0;
    GetWindowThreadProcessId (hwnd, &pid);
    return pid == gui_pid;
}

BOOL
window_is_border_excluded (HWND hwnd)
{
    if (!window_validate (hwnd))
        return true;

    if (window_is_self (NULL, hwnd))
        return true;

    // Clip managed borders around the GUI's own popups (rules search dropdown,
    // colour palette) instead of drawing over them.
    if (window_belongs_to_gui (hwnd))
        return true;

    // Treat installers like the GridFlux GUI: clip managed windows' borders
    // around them instead of drawing over them.
    if (window_is_installer (hwnd))
        return true;

    char class_name[MAX_CLASS_NAME_LENGTH];
    if (GetClassNameA (hwnd, class_name, sizeof (class_name)))
    {
        static const char *excluded_classes[] = { "#32770",
                                                  "TaskManagerWindow",
                                                  "NotifyIconOverflowWindow",
                                                  "ThumbnailDeviceHelperWnd",
                                                  "RecordingAreaIndicatorWindow",
                                                  "XamlExplorerHostIslandWindow_WASDK",
                                                  "XamlWindow",
                                                  "TWizardForm",
                                                  "#32769" };

        for (size_t i = 0; i < sizeof (excluded_classes) / sizeof (excluded_classes[0]);
             i++)
        {
            if (strcmp (class_name, excluded_classes[i]) == 0)
                return true;
        }
    }
    if (strstr (class_name, "SnippingTool") != NULL)
        return true;
    if (strstr (class_name, "GameBar") != NULL)
        return true;
    if (strstr (class_name, "ScreenSketch") != NULL)
        return true;

    return false;
}
