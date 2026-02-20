#include "../../utils/logger.h"
#include "../../utils/memory.h"
#include "internal.h"
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

    char title[MAX_TITLE_LENGTH];
    _get_window_name (display, window, title, sizeof (title));

    static const char *excluded_titles[] = { "GridFlux", "DWM Notification Window" };

    for (size_t i = 0; i < sizeof (excluded_titles) / sizeof (excluded_titles[0]); i++)
    {
        if (strcmp (title, excluded_titles[i]) == 0)
            return true;
    }

    char class_name[MAX_CLASS_NAME_LENGTH];
    if (GetClassNameA (window, class_name, sizeof (class_name)))
    {
        if (_is_excluded_class (class_name, title))
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
gf_window_get_name (gf_display_t display, gf_handle_t window, char *buffer,
                    size_t bufsize)
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
