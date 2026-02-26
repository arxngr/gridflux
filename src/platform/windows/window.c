#include "../../utils/logger.h"
#include "../../utils/memory.h"
#include "internal.h"
#include <stdio.h>
#include <time.h>


#define MAX_WINDOWS 1024

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
        if (_is_app_window (hwnd))
        {
            RECT rect;
            if (FAILED (DwmGetWindowAttribute (hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect,
                                               sizeof (rect)))
                && !GetWindowRect (hwnd, &rect))
                continue;

            if (GetWindowTextA (hwnd, window_list[found_count].name,
                                sizeof (window_list[found_count].name)))
            {
                window_list[found_count].name[sizeof (window_list[found_count].name) - 1]
                    = '\0';
            }
            else
            {
                window_list[found_count].name[0] = '\0';
            }
            window_list[found_count].id = (gf_handle_t)hwnd;
            window_list[found_count].workspace_id = GF_FIRST_WORKSPACE_ID;
            window_list[found_count].geometry.x = rect.left;
            window_list[found_count].geometry.y = rect.top;
            window_list[found_count].geometry.width
                = (gf_dimension_t)(rect.right - rect.left);
            window_list[found_count].geometry.height
                = (gf_dimension_t)(rect.bottom - rect.top);
            window_list[found_count].is_maximized = IsZoomed (hwnd);
            window_list[found_count].is_valid = true;
            window_list[found_count].last_modified = time (NULL);
            found_count++;
        }
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

    if (!_validate_window (window) || !geometry)
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

    if (!_validate_window (window) || !geometry)
        return GF_ERROR_INVALID_PARAMETER;

    int new_x = geometry->x;
    int new_y = geometry->y;
    int new_w = geometry->width;
    int new_h = geometry->height;

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

    if (MoveWindow (window, new_x, new_y, new_w, new_h, TRUE) == 0)
        return GF_ERROR_PLATFORM_ERROR;

    return GF_SUCCESS;
}

bool
gf_window_is_valid (gf_display_t display, gf_handle_t window)
{
    (void)display;
    return _validate_window (window);
}

bool
gf_window_is_excluded (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!_validate_window (window))
        return true;

    if (GetWindow (window, GW_OWNER) != NULL)
        return true;

    if (_is_excluded_style (window))
        return true;

    if (_is_fullscreen_window (window))
        return true;

    if (_is_cloaked_window (window))
        return true;

    if (_is_notification_center (window))
        return true;

    if (_window_it_self (display, window))
        return true;

    char title[MAX_TITLE_LENGTH];
    int len = GetWindowTextA (window, title, sizeof (title) - 1);
    if (len > 0)
    {
        title[len] = '\0';
        if (strcmp (title, "DWM Notification Window") == 0
            || strcmp (title, "GridFlux") == 0)
            return true;
    }

    char class_name[MAX_CLASS_NAME_LENGTH];
    if (GetClassNameA (window, class_name, sizeof (class_name)))
    {
        if (_is_excluded_class (class_name))
            return true;
    }

    return false;
}

bool
gf_window_is_fullscreen (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!_validate_window (window))
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
    if (_validate_window (hwnd) && _is_app_window (hwnd))
        return hwnd;

    return 0;
}

gf_err_t
gf_window_minimize (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!_validate_window (window))
        return GF_ERROR_INVALID_PARAMETER;

    if (ShowWindow (window, SW_SHOWMINNOACTIVE) == 0)
        return GF_ERROR_PLATFORM_ERROR;

    return GF_SUCCESS;
}

gf_err_t
gf_window_unminimize (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!_validate_window (window))
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

void
gf_window_get_class (gf_display_t display, gf_handle_t window, char *buffer,
                     size_t bufsize)
{
    (void)display;

    if (!window || !buffer || bufsize == 0)
        return;

    buffer[0] = '\0';

    if (!_validate_window (window))
        return;

    // Use GetClassNameA to get the window class
    char class_name[128] = { 0 };
    if (GetClassNameA ((HWND)window, class_name, sizeof (class_name)))
    {
        // Get the executable name so rules can match against the .exe
        char exe_name[MAX_PATH] = { 0 };
        DWORD pid = 0;
        GetWindowThreadProcessId ((HWND)window, &pid);
        HANDLE hProcess = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProcess)
        {
            char process_path[MAX_PATH] = { 0 };
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameA (hProcess, 0, process_path, &size))
            {
                char *filename = strrchr (process_path, '\\');
                if (filename)
                    filename++;
                else
                    filename = process_path;
                strncpy (exe_name, filename, sizeof (exe_name) - 1);
            }
            CloseHandle (hProcess);
        }

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
    else
    {
        buffer[0] = '\0';
    }
}

bool
gf_platform_window_minimized (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!_validate_window (window))
        return false;

    return IsIconic ((HWND)window);
}

bool
gf_platform_window_hidden (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!_validate_window (window))
        return false;

    // Window is hidden if it's not visible AND not minimized to taskbar
    // This catches windows that are closed to system tray
    return !IsWindowVisible ((HWND)window) && !IsIconic ((HWND)window);
}

bool
gf_window_is_maximized (gf_display_t display, gf_handle_t window)
{
    (void)display;
    if (!_validate_window ((HWND)window))
        return false;
    return IsZoomed ((HWND)window);
}
