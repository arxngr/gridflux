#include "platform.h"
#include "../../layout.h"
#include "../../logger.h"
#include "../../memory.h"
#include "../../types.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#ifndef SWP_NOSENDCHANGING
#define SWP_NOSENDCHANGING 0x0400
#endif

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "gdi32.lib")

#define MAX_WINDOWS 1024
#define MAX_TITLE_LENGTH 256
#define MAX_CLASS_NAME_LENGTH 256

static gf_border_t *borders[GF_MAX_WINDOWS_PER_WORKSPACE * GF_MAX_WORKSPACES] = { 0 };
static int border_count = 0;

static bool
_validate_window (HWND hwnd)
{
    return hwnd && IsWindow (hwnd);
}

static bool
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

static bool
_is_cloaked_window (HWND hwnd)
{
    DWORD cloaked = 0;
    HRESULT hr = DwmGetWindowAttribute (hwnd, DWMWA_CLOAKED, &cloaked, sizeof (cloaked));
    return SUCCEEDED (hr) && cloaked;
}

static bool
_is_app_window (HWND hwnd)
{
    if (!_validate_window (hwnd))
        return false;

    bool minimized = IsIconic (hwnd);
    if (!IsWindowVisible (hwnd) && !minimized)
        return false;

    if (_is_cloaked_window (hwnd))
        return false;

    if (GetParent (hwnd) != NULL || hwnd == GetShellWindow ())
        return false;

    char title[MAX_TITLE_LENGTH];
    return GetWindowTextA (hwnd, title, sizeof (title)) > 0;
}

static bool
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

static bool
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

static bool
_is_excluded_style (HWND hwnd)
{
    LONG exstyle = GetWindowLongA (hwnd, GWL_EXSTYLE);

    if (exstyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))
        return true;

    char title[MAX_TITLE_LENGTH];
    gf_platform_get_window_name (NULL, hwnd, title, sizeof (title));

    if ((exstyle & WS_EX_TOPMOST) && title[0] == '\0')
        return true;

    return false;
}

static void
_get_taskbar_dimensions (int *left, int *right, int *top, int *bottom)
{
    *left = *right = *top = *bottom = 0;

    APPBARDATA abd = { .cbSize = sizeof (abd) };
    if (!SHAppBarMessage (ABM_GETTASKBARPOS, &abd))
        return;

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

static HWND
_create_border_overlay (HWND target)
{
    RECT rect;
    if (!GetWindowRect (target, &rect))
        return NULL;

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    HWND overlay = CreateWindowExA (WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
                                    "STATIC", NULL, WS_POPUP, rect.left, rect.top, width,
                                    height, NULL, NULL, GetModuleHandle (NULL), NULL);
    if (!overlay)
        return NULL;

    ShowWindow (overlay, SW_SHOW);
    return overlay;
}

static void
_update_border (gf_border_t *border)
{
    if (!border || !border->target || !border->overlay)
        return;

    if (!IsWindow (border->target) || !IsWindow (border->overlay))
        return;

    RECT rect = { 0 };
    if (!SUCCEEDED (DwmGetWindowAttribute (border->target, DWMWA_EXTENDED_FRAME_BOUNDS,
                                           &rect, sizeof (rect))))
    {
        if (!GetWindowRect (border->target, &rect))
            return;
    }

    if (memcmp (&rect, &border->last_rect, sizeof (RECT)) == 0)
        return;

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    if (width <= 0 || height <= 0)
        return;

    HDC hdcScreen = NULL;
    HDC hdcMem = NULL;
    HBITMAP hBmp = NULL;
    HGDIOBJ oldBmp = NULL;
    HPEN hPen = NULL;
    HGDIOBJ oldPen = NULL;
    HBRUSH hTransparent = NULL;

    // Allocate all resources with proper cleanup on failure
    hdcScreen = GetDC (NULL);
    if (!hdcScreen)
        goto cleanup;

    hdcMem = CreateCompatibleDC (hdcScreen);
    if (!hdcMem)
        goto cleanup;

    hBmp = CreateCompatibleBitmap (hdcScreen, width, height);
    if (!hBmp)
        goto cleanup;

    oldBmp = SelectObject (hdcMem, hBmp);

    // Fill transparent background
    RECT full_rect = { 0, 0, width, height };
    hTransparent = CreateSolidBrush (RGB (0, 0, 0));
    if (hTransparent)
    {
        FillRect (hdcMem, &full_rect, hTransparent);
        DeleteObject (hTransparent);
        hTransparent = NULL;
    }

    // Draw border
    COLORREF gdi_color = ((border->color & 0xFF) << 16) | (border->color & 0x00FF00)
                         | ((border->color & 0xFF0000) >> 16);

    hPen = CreatePen (PS_INSIDEFRAME, border->thickness, gdi_color);
    if (hPen)
    {
        oldPen = SelectObject (hdcMem, hPen);
        HGDIOBJ oldBrush = SelectObject (hdcMem, GetStockObject (NULL_BRUSH));

        Rectangle (hdcMem, 0, 0, width, height);

        SelectObject (hdcMem, oldBrush);
        SelectObject (hdcMem, oldPen);
        DeleteObject (hPen);
        hPen = NULL;
    }

    BLENDFUNCTION blend = { 0 };
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    POINT ptZero = { 0, 0 };
    SIZE size = { width, height };
    POINT ptDest = { rect.left, rect.top };

    UpdateLayeredWindow (border->overlay, hdcScreen, &ptDest, &size, hdcMem, &ptZero, 0,
                         &blend, ULW_ALPHA);

    // Visibility and Z-order logic
    if (IsIconic (border->target))
    {
        ShowWindow (border->overlay, SW_HIDE);
    }
    else
    {
        ShowWindow (border->overlay, SW_SHOWNA);

        // NEVER use HWND_TOPMOST - always relative positioning
        SetWindowPos (border->overlay, border->target, rect.left, rect.top, width, height,
                      SWP_NOACTIVATE | SWP_NOSENDCHANGING);
    }

    border->last_rect = rect;

cleanup:
    if (oldBmp && hBmp)
        SelectObject (hdcMem, oldBmp);
    if (hBmp)
        DeleteObject (hBmp);
    if (hdcMem)
        DeleteDC (hdcMem);
    if (hdcScreen)
        ReleaseDC (NULL, hdcScreen);
    if (hTransparent)
        DeleteObject (hTransparent);
    if (hPen)
        DeleteObject (hPen);
}

void
gf_platform_set_border_color (gf_platform_interface_t *platform, gf_color_t color)
{
    if (!platform || !platform->platform_data)
        return;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    for (int i = 0; i < data->border_count; i++)
    {
        if (data->borders[i])
        {
            data->borders[i]->color = color;
            // Force update by invalidating last_rect
            memset (&data->borders[i]->last_rect, 0, sizeof (RECT));
            _update_border (data->borders[i]);
        }
    }
}

void
gf_platform_cleanup_borders (gf_platform_interface_t *platform)
{
    if (!platform)
        return;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;
    if (!data)
        return;

    for (int i = 0; i < data->border_count;)
    {
        gf_border_t *b = data->borders[i];

        // Sanity check
        if (!b || (uintptr_t)b < 0x1000)
        {
            // Shift remaining
            for (int j = i; j < data->border_count - 1; j++)
                data->borders[j] = data->borders[j + 1];
            data->border_count--;
            continue;
        }

        bool should_remove = false;

        if (!b->target || !IsWindow (b->target))
        {
            should_remove = true;
        }
        else if (!b->overlay || !IsWindow (b->overlay))
        {
            should_remove = true;
        }

        if (should_remove)
        {
            if (b->overlay && IsWindow (b->overlay))
            {
                DestroyWindow (b->overlay);
            }
            free (b);

            // Shift remaining
            for (int j = i; j < data->border_count - 1; j++)
                data->borders[j] = data->borders[j + 1];
            data->border_count--;
        }
        else
        {
            i++;
        }
    }
}

bool
gf_platform_is_window_fullscreen (gf_display_t display, gf_native_window_t window)
{
    (void)display;
    return _is_fullscreen_window ((HWND)window);
}

gf_platform_interface_t *
gf_platform_create (void)
{
    gf_platform_interface_t *platform = gf_malloc (sizeof (gf_platform_interface_t));
    if (!platform)
        return NULL;

    gf_windows_platform_data_t *data = gf_malloc (sizeof (gf_windows_platform_data_t));
    if (!data)
    {
        gf_free (platform);
        return NULL;
    }

    memset (platform, 0, sizeof (gf_platform_interface_t));
    memset (data, 0, sizeof (gf_windows_platform_data_t));

    // Assign function pointers
    platform->init = gf_platform_init;
    platform->cleanup = gf_platform_cleanup;
    platform->get_windows = gf_platform_get_windows;
    platform->set_unmaximize_window = gf_platform_unmaximize_window;
    platform->get_window_name_info = gf_platform_get_window_name;
    platform->set_minimize_window = gf_platform_minimize_window;
    platform->set_unminimize_window = gf_platform_unminimize_window;
    platform->get_window_geometry = gf_platform_get_window_geometry;
    platform->get_current_workspace = gf_platform_get_current_workspace;
    platform->get_workspace_count = gf_platform_get_workspace_count;
    platform->create_workspace = gf_platform_create_workspace;
    platform->is_window_valid = gf_platform_is_window_valid;
    platform->is_window_excluded = gf_platform_is_window_excluded;
    platform->get_active_window = gf_platform_active_window;
    platform->get_screen_bounds = gf_platform_get_screen_bounds;
    platform->set_window_geometry = gf_platform_set_window_geometry;
    platform->is_window_minimized = gf_platform_window_minimized;
    platform->is_window_fullscreen = gf_platform_is_window_fullscreen;
    platform->is_window_maximized = gf_platform_is_window_maximized;
    platform->create_border = gf_platform_add_border;
    platform->update_border = gf_platform_update_borders;
    platform->cleanup_borders = gf_platform_cleanup_borders;
    platform->is_window_hidden = gf_platform_window_hidden;
    platform->remove_border = gf_platform_remove_border;
    platform->set_dock_autohide = gf_platform_set_dock_autohide;
    platform->restore_dock = gf_platform_restore_dock;
    platform->platform_data = data;

    return platform;
}

void
gf_platform_destroy (gf_platform_interface_t *platform)
{
    if (!platform)
        return;

    gf_free (platform->platform_data);
    gf_free (platform);
}

gf_error_code_t
gf_platform_init (gf_platform_interface_t *platform, gf_display_t *display)
{
    GF_LOG_INFO ("Initialize Windows platform...");

    if (!display)
        return GF_ERROR_INVALID_PARAMETER;

    *display = NULL;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;
    data->monitor_count = GetSystemMetrics (SM_CMONITORS);

    GF_LOG_INFO ("Platform initialized successfully (monitors: %d)", data->monitor_count);
    return GF_SUCCESS;
}

void
gf_platform_cleanup (gf_display_t display, gf_platform_interface_t *platform)
{
    (void)display;

    if (!platform || !platform->platform_data)
        return;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    for (int i = 0; i < data->border_count; i++)
    {
        if (data->borders[i])
        {
            if (data->borders[i]->overlay && IsWindow (data->borders[i]->overlay))
            {
                DestroyWindow (data->borders[i]->overlay);
            }
            free (data->borders[i]);
        }
    }
    data->border_count = 0;

    GF_LOG_INFO ("Platform cleaned up");
}

gf_error_code_t
gf_platform_get_windows (gf_display_t display, gf_workspace_id_t *workspace_id,
                         gf_window_info_t **windows, uint32_t *count)
{
    (void)display;
    (void)workspace_id;

    if (!windows || !count)
        return GF_ERROR_INVALID_PARAMETER;

    gf_window_info_t *window_list = gf_malloc (MAX_WINDOWS * sizeof (gf_window_info_t));
    if (!window_list)
        return GF_ERROR_MEMORY_ALLOCATION;

    uint32_t found_count = 0;
    HWND hwnd = GetTopWindow (NULL);

    while (hwnd && found_count < MAX_WINDOWS)
    {
        if (_is_app_window (hwnd))
        {
            RECT rect;
            if (GetWindowRect (hwnd, &rect))
            {
                window_list[found_count] = (gf_window_info_t){
                    .id = hwnd,
                    .workspace_id = GF_FIRST_WORKSPACE_ID,
                    .geometry = {
                        .x = rect.left,
                        .y = rect.top,
                        .width = (gf_dimension_t)(rect.right - rect.left),
                        .height = (gf_dimension_t)(rect.bottom - rect.top),
                    },
                    .is_maximized = IsZoomed(hwnd),
                    .is_minimized = IsIconic(hwnd),
                    .needs_update = false,
                    .is_valid = true,
                    .last_modified = time(NULL),
                };

                gf_platform_get_window_name (display, hwnd, window_list[found_count].name,
                                             sizeof (window_list[found_count].name));
                found_count++;
            }
        }
        hwnd = GetNextWindow (hwnd, GW_HWNDNEXT);
    }

    if (found_count == 0)
    {
        gf_free (window_list);
        *windows = NULL;
        *count = 0;
        return GF_SUCCESS;
    }

    gf_window_info_t *resized
        = gf_realloc (window_list, found_count * sizeof (gf_window_info_t));
    if (!resized)
    {
        gf_free (window_list);
        return GF_ERROR_MEMORY_ALLOCATION;
    }

    *windows = resized;
    *count = found_count;
    return GF_SUCCESS;
}

gf_error_code_t
gf_platform_get_window_geometry (gf_display_t display, gf_native_window_t window,
                                 gf_rect_t *geometry)
{
    (void)display;

    if (!geometry || !_validate_window (window))
        return GF_ERROR_INVALID_PARAMETER;

    RECT rect;
    if (!GetWindowRect (window, &rect))
        return GF_ERROR_PLATFORM_ERROR;

    geometry->x = rect.left;
    geometry->y = rect.top;
    geometry->width = (gf_dimension_t)(rect.right - rect.left);
    geometry->height = (gf_dimension_t)(rect.bottom - rect.top);

    return GF_SUCCESS;
}

gf_error_code_t
gf_platform_set_window_geometry (gf_display_t display, gf_native_window_t window,
                                 const gf_rect_t *geometry, gf_geometry_flags_t flags,
                                 gf_config_t *cfg)
{
    (void)display;

    if (!geometry || !_validate_window (window))
        return GF_ERROR_INVALID_PARAMETER;

    /* Skip minimized windows */
    if (IsIconic (window))
        return GF_SUCCESS;

    /* Skip fullscreen / maximized windows */
    if (IsZoomed (window))
    {
        WINDOWPLACEMENT wp = { .length = sizeof (WINDOWPLACEMENT) };
        if (GetWindowPlacement (window, &wp) && wp.showCmd == SW_SHOWMAXIMIZED)
        {
            return GF_SUCCESS;
        }
    }

    gf_rect_t rect = *geometry;

    /* Apply padding */
    if (flags & GF_GEOMETRY_APPLY_PADDING)
        gf_rect_apply_padding (&rect, cfg->default_padding);

    /* Validate geometry */
    if (rect.width <= 0 || rect.height <= 0)
        return GF_SUCCESS;

    /* Restore BEFORE moving if currently maximized */
    if (IsZoomed (window))
    {
        ShowWindow (window, SW_RESTORE);
        Sleep (50);
    }

    /*
     * Compute the offset between GetWindowRect (includes invisible borders/shadows)
     * and DwmGetWindowAttribute DWMWA_EXTENDED_FRAME_BOUNDS (visible portion only).
     */
    RECT dwm_rect = { 0 };
    RECT win_rect = { 0 };
    int shadow_left = 0, shadow_top = 0, shadow_right = 0, shadow_bottom = 0;

    if (GetWindowRect (window, &win_rect)
        && SUCCEEDED (DwmGetWindowAttribute (window, DWMWA_EXTENDED_FRAME_BOUNDS,
                                             &dwm_rect, sizeof (dwm_rect))))
    {
        shadow_left = win_rect.left - dwm_rect.left;
        shadow_top = win_rect.top - dwm_rect.top;
        shadow_right = win_rect.right - dwm_rect.right;
        shadow_bottom = win_rect.bottom - dwm_rect.bottom;
    }

    /* Apply shadow offsets to target geometry */
    int final_x = rect.x + shadow_left;
    int final_y = rect.y + shadow_top;
    int final_w = (int)rect.width + shadow_right - shadow_left;
    int final_h = (int)rect.height + shadow_bottom - shadow_top;

    /*
     * Use SetWindowPos with SWP_NOSENDCHANGING (0x0400) to bypass minimum size
     * constraints. This is crucial for apps like Steam that enforce a large minimum size.
     */
    UINT swp_flags = SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED
                     | SWP_NOSENDCHANGING | SWP_NOCOPYBITS;

    if (!SetWindowPos (window, NULL, final_x, final_y, final_w, final_h, swp_flags))
    {
        GF_LOG_ERROR ("SetWindowPos failed for window %p (error: %lu)", (void *)window,
                      GetLastError ());
        return GF_ERROR_PLATFORM_ERROR;
    }

    return GF_SUCCESS;
}

gf_error_code_t
gf_platform_unmaximize_window (gf_display_t display, gf_native_window_t window)
{
    (void)display;

    if (!_validate_window (window))
        return GF_ERROR_INVALID_PARAMETER;

    if (!IsZoomed (window))
        return GF_SUCCESS;

    if (ShowWindow (window, SW_RESTORE) == 0)
        return GF_ERROR_PLATFORM_ERROR;

    return GF_SUCCESS;
}

gf_workspace_id_t
gf_platform_get_current_workspace (gf_display_t display)
{
    (void)display;
    return GF_FIRST_WORKSPACE_ID;
}

uint32_t
gf_platform_get_workspace_count (gf_display_t display)
{
    (void)display;
    return 1;
}

gf_error_code_t
gf_platform_create_workspace (gf_display_t display)
{
    (void)display;
    GF_LOG_WARN ("Cannot create workspace on Windows - use Windows+Ctrl+D instead");
    return GF_ERROR_PLATFORM_ERROR;
}

bool
gf_platform_is_window_valid (gf_display_t display, gf_native_window_t window)
{
    (void)display;
    return _validate_window (window);
}

bool
gf_platform_is_window_excluded (gf_display_t display, gf_native_window_t window)
{
    (void)display;

    HWND hwnd = (HWND)window;

    if (!_validate_window (hwnd))
        return true;

    if (GetWindow (hwnd, GW_OWNER) != NULL)
        return true;

    if (_is_excluded_style (hwnd))
        return true;

    if (_is_fullscreen_window (hwnd))
        return true;

    if (_is_cloaked_window (hwnd))
        return true;

    if (_is_notification_center (hwnd))
        return true;

    char title[MAX_TITLE_LENGTH];
    gf_platform_get_window_name (display, hwnd, title, sizeof (title));

    static const char *excluded_titles[] = { "GridFlux", "DWM Notification Window" };

    for (size_t i = 0; i < sizeof (excluded_titles) / sizeof (excluded_titles[0]); i++)
    {
        if (strcmp (title, excluded_titles[i]) == 0)
            return true;
    }

    char class_name[MAX_CLASS_NAME_LENGTH];
    if (GetClassNameA (hwnd, class_name, sizeof (class_name)))
    {
        if (_is_excluded_class (class_name, title))
            return true;
    }

    return false;
}

gf_error_code_t
gf_platform_get_screen_bounds (gf_display_t display, gf_rect_t *bounds)
{
    (void)display;

    if (!bounds)
        return GF_ERROR_INVALID_PARAMETER;

    int x = GetSystemMetrics (SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics (SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics (SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics (SM_CYVIRTUALSCREEN);

    int panel_left, panel_right, panel_top, panel_bottom;
    _get_taskbar_dimensions (&panel_left, &panel_right, &panel_top, &panel_bottom);

    bounds->x = x + panel_left;
    bounds->y = y + panel_top;
    bounds->width = (gf_dimension_t)(width - panel_left - panel_right);
    bounds->height = (gf_dimension_t)(height - panel_top - panel_bottom);

    return GF_SUCCESS;
}

gf_native_window_t
gf_platform_active_window (gf_display_t display)
{
    (void)display;

    HWND hwnd = GetForegroundWindow ();
    if (_validate_window (hwnd) && _is_app_window (hwnd))
        return hwnd;

    return 0;
}

gf_error_code_t
gf_platform_minimize_window (gf_display_t display, gf_native_window_t window)
{
    (void)display;

    if (!_validate_window (window))
        return GF_ERROR_INVALID_PARAMETER;

    if (ShowWindow (window, SW_MINIMIZE) == 0)
        return GF_ERROR_PLATFORM_ERROR;

    return GF_SUCCESS;
}

gf_error_code_t
gf_platform_unminimize_window (gf_display_t display, gf_native_window_t window)
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
gf_platform_get_window_name (gf_display_t display, gf_native_window_t window,
                             char *buffer, size_t bufsize)
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
gf_platform_window_minimized (gf_display_t display, gf_native_window_t window)
{
    (void)display;

    if (!_validate_window (window))
        return false;

    return IsIconic ((HWND)window);
}

void
gf_platform_add_border (gf_platform_interface_t *platform, gf_native_window_t window,
                        gf_color_t color, int thickness)
{
    if (!platform || !platform->platform_data || !window)
        return;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    // Check if already exists
    for (int i = 0; i < data->border_count; i++)
    {
        if (data->borders[i] && data->borders[i]->target == window)
        {
            GF_LOG_DEBUG ("Border already exists for window %p", window);
            return;
        }
    }

    RECT rect;
    if (!GetWindowRect (window, &rect))
    {
        GF_LOG_WARN ("Failed to get window rect for border");
        return;
    }

    HWND overlay = _create_border_overlay (window);
    if (!overlay)
    {
        GF_LOG_WARN ("Failed to create border overlay");
        return;
    }

    gf_border_t *b = malloc (sizeof (gf_border_t));
    if (!b)
    {
        DestroyWindow (overlay);
        GF_LOG_ERROR ("Failed to allocate border structure");
        return;
    }

    b->target = window;
    b->overlay = overlay;
    b->color = color;
    b->thickness = thickness;
    b->last_rect = rect;

    data->borders[data->border_count++] = b;

    GF_LOG_INFO ("Added border for window %p (color=0x%08X, thickness=%d, count=%d)",
                 window, color, thickness, data->border_count);

    _update_border (b);
}

void
gf_platform_update_borders (gf_platform_interface_t *platform)
{
    if (!platform || !platform->platform_data)
        return;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    // Update all borders
    for (int i = 0; i < data->border_count; i++)
    {
        _update_border (data->borders[i]);
    }
}

bool
gf_platform_window_hidden (gf_display_t display, gf_native_window_t window)
{
    (void)display;

    if (!_validate_window (window))
        return false;

    // Window is hidden if it's not visible AND not minimized to taskbar
    // This catches windows that are closed to system tray
    return !IsWindowVisible ((HWND)window) && !IsIconic ((HWND)window);
}

void
gf_platform_remove_border (gf_platform_interface_t *platform, gf_native_window_t window)
{
    if (!platform || !platform->platform_data || !window)
        return;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    for (int i = 0; i < data->border_count; i++)
    {
        if (data->borders[i] && data->borders[i]->target == window)
        {
            gf_border_t *b = data->borders[i];

            // Destroy overlay window
            if (b->overlay && IsWindow (b->overlay))
            {
                DestroyWindow (b->overlay);
            }

            gf_free (b);

            // Shift remaining borders
            for (int j = i; j < data->border_count - 1; j++)
                data->borders[j] = data->borders[j + 1];

            data->border_count--;

            GF_LOG_DEBUG ("Removed border for window %p", window);
            return;
        }
    }
}

bool
gf_platform_is_window_maximized (gf_display_t display, gf_native_window_t window)
{
    (void)display;
    if (!_validate_window ((HWND)window))
        return false;
    return IsZoomed ((HWND)window);
}

void
gf_platform_set_dock_autohide (gf_platform_interface_t *platform)
{
    (void)platform;
    APPBARDATA abd = { .cbSize = sizeof (abd) };
    abd.hWnd = FindWindowA ("Shell_TrayWnd", NULL);
    if (abd.hWnd)
    {
        abd.lParam = ABS_AUTOHIDE;
        SHAppBarMessage (ABM_SETSTATE, &abd);
    }
}

void
gf_platform_restore_dock (gf_platform_interface_t *platform)
{
    (void)platform;
    APPBARDATA abd = { .cbSize = sizeof (abd) };
    abd.hWnd = FindWindowA ("Shell_TrayWnd", NULL);
    if (abd.hWnd)
    {
        abd.lParam = ABS_ALWAYSONTOP;
        SHAppBarMessage (ABM_SETSTATE, &abd);
    }
}
