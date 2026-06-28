#include "../../utils/logger.h"
#include "../../utils/memory.h"
#include "internal.h"
#include <ctype.h>
#include <stdio.h>
#include <time.h>

#define MAX_WINDOWS 1024

static const char *
_strcasestr (const char *haystack, const char *needle)
{
    if (!haystack || !needle)
        return NULL;
    if (*needle == '\0')
        return haystack;

    for (; *haystack != '\0'; haystack++)
    {
        if (tolower ((unsigned char)*haystack) == tolower ((unsigned char)*needle))
        {
            const char *h = haystack;
            const char *n = needle;
            while (*h != '\0' && *n != '\0'
                   && tolower ((unsigned char)*h) == tolower ((unsigned char)*n))
            {
                h++;
                n++;
            }
            if (*n == '\0')
                return haystack;
        }
    }
    return NULL;
}

// Resolve the executable file name (e.g. "code.exe") for a process id.
// Writes an empty string on failure.
static void
_pid_get_exe_name (DWORD pid, char *out, size_t out_size)
{
    out[0] = '\0';

    HANDLE proc = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc)
        return;

    char path[MAX_PATH] = { 0 };
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameA (proc, 0, path, &size))
    {
        char *filename = strrchr (path, '\\');
        filename = filename ? filename + 1 : path;
        strncpy (out, filename, out_size - 1);
        out[out_size - 1] = '\0';
    }
    CloseHandle (proc);
}

// True if any of the needles occurs (case-insensitively) in haystack.
static bool
_str_contains_any (const char *haystack, const char *const *needles, int count)
{
    for (int i = 0; i < count; i++)
        if (_strcasestr (haystack, needles[i]) != NULL)
            return true;
    return false;
}

// Case-insensitive check that `s` ends with `suffix`.
static bool
_str_ends_with_ci (const char *s, const char *suffix)
{
    size_t ls = strlen (s), lf = strlen (suffix);
    if (lf > ls)
        return false;

    const char *tail = s + (ls - lf);
    for (size_t i = 0; i < lf; i++)
        if (tolower ((unsigned char)tail[i]) != tolower ((unsigned char)suffix[i]))
            return false;
    return true;
}

static bool
_window_class_is_installer (const char *class_name)
{
    static const char *const exact[] = {
        "MsiDialogCloseClass", // Windows Installer / MSI / WiX 
        "TWizardForm",         // Inno Setup wizard
        "TSetupLdrWindow",     // Inno Setup loader
    };
    for (int i = 0; i < 3; i++)
        if (strcmp (class_name, exact[i]) == 0)
            return true;

    return _strcasestr (class_name, "InstallShield") != NULL // InstallShield
           || _strcasestr (class_name, "Nullsoft") != NULL;  // NSIS
}

static bool
_window_title_is_installer (const char *title)
{
    if (_str_ends_with_ci (title, " setup") || _str_ends_with_ci (title, " installer"))
        return true;

    static const char *const phrases[] = { "setup wizard", "install wizard",
                                           "installshield", "uninstall" };
    return _str_contains_any (title, phrases, 4);
}

bool
window_is_installer (HWND window)
{
    char class_name[256] = { 0 };
    GetClassNameA (window, class_name, sizeof (class_name));

    char title[256] = { 0 };
    GetWindowTextA (window, title, sizeof (title) - 1);

    if (_window_class_is_installer (class_name))
        return true;

    DWORD pid = 0;
    GetWindowThreadProcessId (window, &pid);
    char exe_name[MAX_PATH] = { 0 };
    _pid_get_exe_name (pid, exe_name, sizeof (exe_name));

    static const char *const exe_kw[] = { "setup", "install", "uninst", "msiexec" };
    if (exe_name[0] != '\0' && _str_contains_any (exe_name, exe_kw, 4))
        return true;

    return _window_title_is_installer (title);
}

static bool
_window_fill_info (HWND hwnd, gf_win_info_t *info)
{
    RECT rect;
    if (FAILED (DwmGetWindowAttribute (hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect,
                                       sizeof (rect)))
        && !GetWindowRect (hwnd, &rect))
        return false;

    if (GetWindowTextA (hwnd, info->name, sizeof (info->name)))
        info->name[sizeof (info->name) - 1] = '\0';
    else
        info->name[0] = '\0';

    info->id = (gf_handle_t)hwnd;
    info->workspace_id = GF_FIRST_WORKSPACE_ID;
    info->geometry.x = rect.left;
    info->geometry.y = rect.top;
    info->geometry.width = (gf_dimension_t)(rect.right - rect.left);
    info->geometry.height = (gf_dimension_t)(rect.bottom - rect.top);
    info->is_maximized = IsZoomed (hwnd);
    info->is_valid = true;
    info->last_modified = time (NULL);
    info->monitor_id = 0;
    return true;
}

gf_err_t
gf_platform_get_windows (gf_display_t display, gf_ws_id_t *workspace_id,
                         gf_win_info_t **windows, uint32_t *count)
{
    (void)display;
    (void)workspace_id;

    if (!windows || !count)
        return GF_ERROR_INVALID_PARAMETER;

    gf_win_info_t *window_list = gf_malloc (MAX_WINDOWS * sizeof (gf_win_info_t));
    if (!window_list)
        return GF_ERROR_MEMORY_ALLOCATION;

    uint32_t found_count = 0;
    HWND hwnd = GetTopWindow (NULL);

    while (hwnd && found_count < MAX_WINDOWS)
    {
        // NOTE: always advance hwnd. The previous version did `continue` on a
        // geometry-lookup failure before advancing, which could spin forever.
        if (window_is_app (hwnd) && _window_fill_info (hwnd, &window_list[found_count]))
            found_count++;
        hwnd = GetNextWindow (hwnd, GW_HWNDNEXT);
    }

    *windows = window_list;
    *count = found_count;

    return GF_SUCCESS;
}

gf_err_t
gf_window_get_geometry (gf_display_t display, gf_handle_t window, gf_rect_t *geometry)
{
    (void)display;

    if (!window_validate (window) || !geometry)
        return GF_ERROR_INVALID_PARAMETER;

    RECT rect;
    if (SUCCEEDED (DwmGetWindowAttribute (window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect,
                                          sizeof (rect)))
        || GetWindowRect (window, &rect))
    {
        geometry->x = rect.left;
        geometry->y = rect.top;
        geometry->width = (gf_dimension_t)(rect.right - rect.left);
        geometry->height = (gf_dimension_t)(rect.bottom - rect.top);
        return GF_SUCCESS;
    }

    return GF_ERROR_PLATFORM_ERROR;
}

gf_err_t
gf_window_set_geometry (gf_display_t display, gf_handle_t window,
                        const gf_rect_t *geometry, gf_geom_flags_t flags,
                        gf_config_t *cfg)
{
    (void)display;
    (void)flags;
    (void)cfg;

    if (!window_validate (window) || !geometry)
        return GF_ERROR_INVALID_PARAMETER;

    int new_x = geometry->x;
    int new_y = geometry->y;
    int new_w = geometry->width;
    int new_h = geometry->height;

    // Compensate for the invisible DWM shadow/border that shifts the window rect
    RECT d_rect, w_rect;
    if (SUCCEEDED (DwmGetWindowAttribute (window, DWMWA_EXTENDED_FRAME_BOUNDS, &d_rect,
                                          sizeof (d_rect)))
        && GetWindowRect (window, &w_rect))
    {
        int left_border = d_rect.left - w_rect.left;
        int top_border = d_rect.top - w_rect.top;
        int right_border = w_rect.right - d_rect.right;
        int bottom_border = w_rect.bottom - d_rect.bottom;

        new_x -= left_border;
        new_y -= top_border;
        new_w += left_border + right_border;
        new_h += top_border + bottom_border;
    }

    // Use SetWindowPos with SWP_NOSENDCHANGING so that apps like Discord
    // (CEF/Electron) cannot intercept the resize via WM_WINDOWPOSCHANGING
    // and silently enforce their own minimum size.
    UINT swp_flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING;
    if (SetWindowPos (window, NULL, new_x, new_y, new_w, new_h, swp_flags) == 0)
        return GF_ERROR_PLATFORM_ERROR;

    return GF_SUCCESS;
}

bool
gf_window_is_valid (gf_display_t display, gf_handle_t window)
{
    (void)display;
    return window_validate (window);
}

// True if the window's title marks it as a system/utility window to ignore.
static bool
_window_title_excluded (HWND window)
{
    char title[MAX_TITLE_LENGTH];
    int len = GetWindowTextA (window, title, sizeof (title) - 1);
    if (len <= 0)
        return false;
    title[len] = '\0';

    static const char *const exact[] = { "DWM Notification Window", "GridFlux" };
    for (int i = 0; i < 2; i++)
        if (strcmp (title, exact[i]) == 0)
            return true;

    static const char *const substr[] = { "Snipping Tool", "Game Bar", "Screen Sketch" };
    for (int i = 0; i < 3; i++)
        if (strstr (title, substr[i]) != NULL)
            return true;

    return false;
}

bool
gf_window_is_excluded (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!window_validate (window))
        return true;

    if (GetWindow (window, GW_OWNER) != NULL)
        return true;

    if (window_is_excluded_style (window))
        return true;

    if (window_is_fullscreen (window))
        return true;

    if (window_is_cloaked (window))
        return true;

    if (window_is_notification_center (window))
        return true;

    if (window_is_self (display, window))
        return true;

    if (window_is_installer ((HWND)window))
        return true;

    if (_window_title_excluded ((HWND)window))
        return true;

    char class_name[MAX_CLASS_NAME_LENGTH];
    if (GetClassNameA (window, class_name, sizeof (class_name)))
    {
        if (window_is_excluded_class (class_name))
            return true;

        char dbg_title[128] = { 0 };
        GetWindowTextA ((HWND)window, dbg_title, sizeof (dbg_title) - 1);
        GF_LOG_DEBUG ("Managing window: class='%s' title='%s'", class_name, dbg_title);
    }

    return false;
}

bool
gf_window_is_fullscreen (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!window_validate (window))
        return false;

    RECT rect;
    GetWindowRect (window, &rect);

    int screenWidth = GetSystemMetrics (SM_CXSCREEN);
    int screenHeight = GetSystemMetrics (SM_CYSCREEN);

    return (rect.left <= 0 && rect.top <= 0 && rect.right >= screenWidth
            && rect.bottom >= screenHeight);
}

gf_handle_t
gf_window_get_focused (gf_display_t display)
{
    (void)display;

    HWND hwnd = GetForegroundWindow ();
    if (window_validate (hwnd) && window_is_app (hwnd))
        return hwnd;

    return 0;
}

gf_err_t
gf_window_minimize (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!window_validate (window))
        return GF_ERROR_INVALID_PARAMETER;

    if (ShowWindow (window, SW_SHOWMINNOACTIVE) == 0)
        return GF_ERROR_PLATFORM_ERROR;

    return GF_SUCCESS;
}

gf_err_t
gf_window_unminimize (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!window_validate (window))
        return GF_ERROR_INVALID_PARAMETER;

    if (IsIconic ((HWND)window))
    {
        if (ShowWindow ((HWND)window, SW_RESTORE) == 0)
            return GF_ERROR_PLATFORM_ERROR;
    }
    else
    {
        if (ShowWindow ((HWND)window, SW_SHOW) == 0)
            return GF_ERROR_PLATFORM_ERROR;
    }

    SetForegroundWindow (window);
    return GF_SUCCESS;
}

// Resolve the owning process id for a window. UWP host windows
// (ApplicationFrameWindow) proxy a child process, so dig into the child.
static DWORD
_window_resolve_pid (HWND window, const char *class_name)
{
    DWORD pid = 0;
    GetWindowThreadProcessId (window, &pid);

    if (strcmp (class_name, "ApplicationFrameWindow") != 0)
        return pid;

    HWND child = FindWindowExA (window, NULL, "Windows.UI.Core.CoreWindow", NULL);
    if (child)
    {
        GetWindowThreadProcessId (child, &pid);
        return pid;
    }

    // Fallback: first child window with a different PID
    for (HWND c = GetWindow (window, GW_CHILD); c; c = GetWindow (c, GW_HWNDNEXT))
    {
        DWORD child_pid = 0;
        GetWindowThreadProcessId (c, &child_pid);
        if (child_pid != 0 && child_pid != pid)
            return child_pid;
    }
    return pid;
}

void
gf_window_get_class (gf_display_t display, gf_handle_t window, char *buffer,
                     size_t bufsize)
{
    (void)display;

    if (!window || !buffer || bufsize == 0)
        return;

    buffer[0] = '\0';

    if (!window_validate (window))
        return;

    char class_name[128] = { 0 };
    if (!GetClassNameA ((HWND)window, class_name, sizeof (class_name)))
        return;

    // Append the executable name so rules can match against the .exe
    DWORD pid = _window_resolve_pid ((HWND)window, class_name);
    char exe_name[MAX_PATH] = { 0 };
    _pid_get_exe_name (pid, exe_name, sizeof (exe_name));

    if (exe_name[0] != '\0')
    {
        snprintf (buffer, bufsize, "%s|%s", class_name, exe_name);
    }
    else
    {
        strncpy (buffer, class_name, bufsize - 1);
        buffer[bufsize - 1] = '\0';
    }
}

bool
gf_platform_window_minimized (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!window_validate (window))
        return false;

    return IsIconic ((HWND)window);
}

bool
gf_platform_window_hidden (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!window_validate (window))
        return false;

    // Window is hidden if it's not visible AND not minimized to taskbar
    // This catches windows that are closed to system tray
    return !IsWindowVisible ((HWND)window) && !IsIconic ((HWND)window);
}

bool
gf_window_is_maximized (gf_display_t display, gf_handle_t window)
{
    (void)display;
    if (!window_validate ((HWND)window))
        return false;
    return IsZoomed ((HWND)window);
}
