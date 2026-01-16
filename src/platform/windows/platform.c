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

    if (GetWindow (hwnd, GW_OWNER))
        return false;

    RECT win, screen;
    GetWindowRect (hwnd, &win);

    HMONITOR mon = MonitorFromWindow (hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { .cbSize = sizeof (mi) };
    GetMonitorInfo (mon, &mi);
    screen = mi.rcMonitor;

    return (win.left <= screen.left && win.top <= screen.top && win.right >= screen.right
            && win.bottom >= screen.bottom);
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
            "TrayNotifyWnd",
            "NotifyIconOverflowWindow",
            "Windows.UI.Core.CoreWindow",
            "Xaml_Windowed_Popup",
            "TopLevelWindowForOverflowXamlIsland",
            "Windows.Internal.Shell.NotificationCenter",
            "NativeHWNDHost",
            "Windows.UI.Composition.DesktopWindowContentBridge" };

    for (size_t i = 0; i < sizeof (excluded_classes) / sizeof (excluded_classes[0]); i++)
    {
        if (strcmp (class_name, excluded_classes[i]) == 0)
            return true;
    }

    if (strstr (class_name, "Xaml") || strstr (class_name, "Overflow"))
        return true;

    if (strcmp (class_name, "ApplicationFrameWindow") == 0 && title[0] == '\0')
        return true;

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
        if (title[0] == '\0' || strstr (title, "otification"))
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
    if (!border)
        return;

    if (!border->target || !border->overlay)
        return;

    if (!IsWindow (border->target) || !IsWindow (border->overlay))
        return;

    // Use various methods to get the correct visual bounds
    RECT rect = { 0 };
    if (SUCCEEDED (DwmGetWindowAttribute (border->target, DWMWA_EXTENDED_FRAME_BOUNDS,
                                          &rect, sizeof (rect))))
    {
        // DWM frame bounds are usually correct for visible windows
    }
    else if (!GetWindowRect (border->target, &rect))
    {
        return;
    }

    // Skip if unchanged (and strictly checking rect, not forced)
    if (memcmp (&rect, &border->last_rect, sizeof (RECT)) == 0)
        return;

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    // Expand width/height slightly to draw OUTSIDE the window content
    // But since this is an overlay, we usually want it *on top* of the border area.
    // Given the user report of "gap", using exact DWM bounds is the best starting point.

    if (width <= 0 || height <= 0)
        return;

    // ... (rest of drawing logic stays same, but we need to ensure the overlay window
    // MOVES to the new rect)

    HDC hdcScreen = GetDC (NULL);
    if (!hdcScreen)
        return;

    HDC hdcMem = CreateCompatibleDC (hdcScreen);
    if (!hdcMem)
    {
        ReleaseDC (NULL, hdcScreen);
        return;
    }

    HBITMAP hBmp = CreateCompatibleBitmap (hdcScreen, width, height);
    if (!hBmp)
    {
        DeleteDC (hdcMem);
        ReleaseDC (NULL, hdcScreen);
        return;
    }

    HGDIOBJ oldBmp = SelectObject (hdcMem, hBmp);

    // Fill transparent
    // Note: We use 0,0,0 as key for 'transparent' usually, or alpha channel.
    // Since we use ULW_ALPHA, we need pre-multiplied alpha or just 0 alpha.
    // Creating a 32-bit bitmap might be needed for per-pixel alpha if we wanted fancy
    // effects, but here we just want a simple border. For simple borders, a mask color or
    // regions is often used, but UpdateLayeredWindow is fine.

    // Clear background
    RECT full_rect = { 0, 0, width, height };
    HBRUSH hTransparent
        = CreateSolidBrush (RGB (0, 0, 0)); // Black is usually the color key
    FillRect (hdcMem, &full_rect, hTransparent);
    DeleteObject (hTransparent);

    // Draw border
    // Draw hollow rectangle
    // Convert RGB (0xRRGGBB) to Windows BGR (0x00BBGGRR)
    COLORREF gdi_color = ((border->color & 0xFF) << 16) | (border->color & 0x00FF00)
                         | ((border->color & 0xFF0000) >> 16);
    HPEN hPen = CreatePen (PS_INSIDEFRAME, border->thickness, gdi_color);
    HGDIOBJ oldPen = SelectObject (hdcMem, hPen);
    HGDIOBJ oldBrush = SelectObject (hdcMem, GetStockObject (NULL_BRUSH));

    Rectangle (hdcMem, 0, 0, width, height);

    SelectObject (hdcMem, oldBrush);
    SelectObject (hdcMem, oldPen);
    DeleteObject (hPen);

    BLENDFUNCTION blend = { 0 };
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA; // Only if using 32-bit bitmap with alpha channel

    // If we are NOT using per-pixel alpha (just color keying), we should use LWA_COLORKEY
    // method but we started with ULW_ALPHA. Let's assume standard GDI drawing creates 0
    // alpha for background and non-zero for content if we are lucky, OR we simply use
    // color key. Actually, UpdateLayeredWindow with ULW_ALPHA expects 32bpp bitmap.
    // CreateCompatibleBitmap might return screen compatible (usually 32bpp now).
    // Let's stick to previous working logic but ensure rect is correct.

    POINT ptZero = { 0, 0 };
    SIZE size = { width, height };
    POINT ptDest = { rect.left, rect.top };

    // Important: Use LWA_COLORKEY if we want simple transparency without alpha headaches
    // But since we are using UpdateLayeredWindow, let's keep it.
    // However, we should explicitly set using ULW_COLORKEY if we want black to be
    // transparent. The previous code used ULW_ALPHA.

    UpdateLayeredWindow (border->overlay, hdcScreen, &ptDest, &size, hdcMem, &ptZero, 0,
                         &blend, ULW_ALPHA);

    // Also update window pos to ensure z-order (topmost)
    SetWindowPos (border->overlay, HWND_TOPMOST, rect.left, rect.top, width, height,
                  SWP_NOACTIVATE | SWP_NOSENDCHANGING);

    border->last_rect = rect;

    SelectObject (hdcMem, oldBmp);
    DeleteObject (hBmp);
    DeleteDC (hdcMem);
    ReleaseDC (NULL, hdcScreen);
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
    platform->unmaximize_window = gf_platform_unmaximize_window;
    platform->window_name_info = gf_platform_get_window_name;
    platform->minimize_window = gf_platform_minimize_window;
    platform->unminimize_window = gf_platform_unminimize_window;
    platform->get_window_geometry = gf_platform_get_window_geometry;
    platform->get_current_workspace = gf_platform_get_current_workspace;
    platform->get_workspace_count = gf_platform_get_workspace_count;
    platform->create_workspace = gf_platform_create_workspace;
    platform->is_window_valid = gf_platform_is_window_valid;
    platform->is_window_excluded = gf_platform_is_window_excluded;
    platform->is_window_drag = gf_platform_is_window_drag;
    platform->get_active_window = gf_platform_active_window;
    platform->get_screen_bounds = gf_platform_get_screen_bounds;
    platform->set_window_geometry = gf_platform_set_window_geometry;
    platform->is_window_minimized = gf_platform_window_minimized;
    platform->add_border = gf_platform_add_border;
    platform->update_border = gf_platform_update_borders;
    platform->cleanup_borders = gf_platform_cleanup_borders;
    platform->is_window_hidden = gf_platform_window_hidden;
    platform->remove_border = gf_platform_remove_border;
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
                    .id = (gf_window_id_t)hwnd,
                    .native_handle = hwnd,
                    .workspace_id = 0,
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
    return 0;
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
gf_platform_is_window_drag (gf_display_t display, gf_native_window_t window,
                            gf_rect_t *geometry)
{
    (void)display;
    (void)window;

    memset (geometry, 0, sizeof (*geometry));
    return GF_SUCCESS;
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

gf_window_id_t
gf_platform_active_window (gf_display_t display)
{
    (void)display;

    HWND hwnd = GetForegroundWindow ();
    if (_validate_window (hwnd) && _is_app_window (hwnd))
        return (gf_window_id_t)hwnd;

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

    if (ShowWindow (window, SW_RESTORE) == 0)
        return GF_ERROR_PLATFORM_ERROR;

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
