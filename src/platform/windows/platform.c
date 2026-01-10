#include "platform.h"
#include "../../layout.h"
#include "../../logger.h"
#include "../../memory.h"
#include "../../types.h"
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "gdi32.lib")

// Forward declarations
static bool gf_platform_is_app_window (HWND hwnd);
static bool gf_platform_is_cloaked_window (HWND hwnd);

// Platform implementation
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

    // On Windows, display is NULL (we use window handles directly)
    *display = NULL;

    gf_windows_platform_data_t *data = (gf_windows_platform_data_t *)platform->platform_data;
    data->monitor_count = GetSystemMetrics (SM_CMONITORS);

    GF_LOG_INFO ("Platform initialized successfully (monitors: %d)", data->monitor_count);
    return GF_SUCCESS;
}

void
gf_platform_cleanup (gf_display_t display, gf_platform_interface_t *platform)
{
    if (!platform || !platform->platform_data)
        return;

    (void)display;

    GF_LOG_INFO ("Platform cleaned up");
    gf_free (platform->platform_data);
}

static bool
gf_platform_is_app_window (HWND hwnd)
{
    if (!IsWindow (hwnd))
        return false;

    // Skip invisible windows
    if (!IsWindowVisible (hwnd))
        return false;

    // Skip cloaked windows (like Alt+Tab filtered windows)
    if (gf_platform_is_cloaked_window (hwnd))
        return false;

    // Skip windows with no title
    char title[256];
    if (GetWindowTextA (hwnd, title, sizeof (title)) == 0)
        return false;

    // Skip windows that are children
    if (GetParent (hwnd) != NULL)
        return false;

    // Skip shell windows
    HWND shell_hwnd = GetShellWindow ();
    if (hwnd == shell_hwnd)
        return false;

    return true;
}

static bool
gf_platform_is_cloaked_window (HWND hwnd)
{
    // Try to detect cloaked windows using DWM
    DWORD cloaked = 0;
    HRESULT hr = DwmGetWindowAttribute (hwnd, DWMWA_CLOAKED, &cloaked, sizeof (cloaked));
    
    if (SUCCEEDED (hr) && cloaked)
        return true;

    return false;
}

gf_error_code_t
gf_platform_get_windows (gf_display_t display, gf_workspace_id_t *workspace_id,
                         gf_window_info_t **windows, uint32_t *count)
{
    if (!windows || !count)
        return GF_ERROR_INVALID_PARAMETER;

    (void)display;
    (void)workspace_id;

    gf_window_info_t *window_list = gf_malloc (1024 * sizeof (gf_window_info_t));
    if (!window_list)
        return GF_ERROR_MEMORY_ALLOCATION;

    uint32_t found_count = 0;
    HWND hwnd = GetTopWindow (NULL);

    while (hwnd && found_count < 1024)
    {
        if (gf_platform_is_app_window (hwnd))
        {
            RECT rect;
            if (GetWindowRect (hwnd, &rect))
            {
                window_list[found_count] = (gf_window_info_t){
                    .id = (gf_window_id_t)hwnd,
                    .native_handle = hwnd,
                    .workspace_id = 0, // Windows 10/11 virtual desktops not directly supported
                    .geometry = {
                        .x = rect.left,
                        .y = rect.top,
                        .width = (gf_dimension_t)(rect.right - rect.left),
                        .height = (gf_dimension_t)(rect.bottom - rect.top),
                    },
                    .is_maximized = IsZoomed (hwnd),
                    .is_minimized = IsIconic (hwnd),
                    .needs_update = false,
                    .is_valid = true,
                    .last_modified = time (NULL),
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

    // Resize to actual count
    gf_window_info_t *resized = gf_realloc (window_list, found_count * sizeof (gf_window_info_t));
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

    if (!geometry || !IsWindow (window))
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

    if (!geometry || !IsWindow (window))
        return GF_ERROR_INVALID_PARAMETER;

    gf_rect_t rect = *geometry;

    if (flags & GF_GEOMETRY_APPLY_PADDING)
        gf_rect_apply_padding (&rect, cfg->default_padding);

    // Unmaximize window first if needed
    if (IsZoomed (window))
        ShowWindow (window, SW_RESTORE);

    if (!MoveWindow (window, rect.x, rect.y, (int)rect.width, (int)rect.height, TRUE))
        return GF_ERROR_PLATFORM_ERROR;

    return GF_SUCCESS;
}

gf_error_code_t
gf_platform_unmaximize_window (gf_display_t display, gf_native_window_t window)
{
    (void)display;

    if (!IsWindow (window))
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
    // Windows 10/11 virtual desktops are not directly accessible via public API
    // We'll default to workspace 0
    return 0;
}

uint32_t
gf_platform_get_workspace_count (gf_display_t display)
{
    (void)display;
    // Windows 10/11 has virtual desktops but no public API to query them
    // Default to 1 for compatibility
    return 1;
}

gf_error_code_t
gf_platform_create_workspace (gf_display_t display)
{
    (void)display;
    // Virtual desktops in Windows 10/11 cannot be created via public API
    GF_LOG_WARN ("Cannot create workspace on Windows - use Windows+Ctrl+D instead");
    return GF_ERROR_PLATFORM_ERROR;
}

bool
gf_platform_is_window_valid (gf_display_t display, gf_native_window_t window)
{
    (void)display;
    return IsWindow (window) != 0;
}

bool
gf_platform_is_window_excluded (gf_display_t display, gf_native_window_t window)
{
    (void)display;

    if (!IsWindow (window))
        return true;

    char name[256];
    gf_platform_get_window_name (display, window, name, sizeof (name));
    if (strcmp (name, "GridFlux") == 0)
        return true;

    // Check for UWP apps and system windows
    DWORD exstyle = GetWindowLongA (window, GWL_EXSTYLE);
    
    // Skip windows with app bar style
    if (exstyle & WS_EX_TOOLWINDOW)
        return true;

    // Skip cloaked/background windows
    if (gf_platform_is_cloaked_window (window))
        return true;

    // Check if it's a UWP app (has specific class name patterns)
    char class_name[256];
    if (GetClassNameA (window, class_name, sizeof (class_name)) > 0)
    {
        // Skip shell windows and COM objects
        if (strstr (class_name, "Shell_TrayWnd") || 
            strstr (class_name, "Shell_SecondaryTrayWnd") ||
            strstr (class_name, "Windows.UI.Core") ||
            strcmp (class_name, "ApplicationFrameWindow") == 0)
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

    // Windows drag detection would require WH_MOUSE hook
    // For now, return success without tracking
    return GF_SUCCESS;
}

gf_error_code_t
gf_platform_get_screen_bounds (gf_display_t display, gf_rect_t *bounds)
{
    (void)display;

    if (!bounds)
        return GF_ERROR_INVALID_PARAMETER;

    // Get primary monitor bounds
    int x = GetSystemMetrics (SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics (SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics (SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics (SM_CYVIRTUALSCREEN);

    // Account for taskbar
    APPBARDATA abd;
    memset (&abd, 0, sizeof (abd));
    abd.cbSize = sizeof (abd);
    
    LONG taskbar_size = SHAppBarMessage (ABM_GETTASKBARPOS, &abd);
    
    int panel_left = 0, panel_right = 0, panel_top = 0, panel_bottom = 0;
    
    if (taskbar_size)
    {
        switch (abd.uEdge)
        {
        case ABE_LEFT:
            panel_left = abd.rc.right - abd.rc.left;
            break;
        case ABE_RIGHT:
            panel_right = (abd.rc.right - abd.rc.left);
            break;
        case ABE_TOP:
            panel_top = abd.rc.bottom - abd.rc.top;
            break;
        case ABE_BOTTOM:
            panel_bottom = abd.rc.bottom - abd.rc.top;
            break;
        }
    }

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
    if (hwnd && IsWindow (hwnd) && gf_platform_is_app_window (hwnd))
        return (gf_window_id_t)hwnd;

    return 0;
}

gf_error_code_t
gf_platform_minimize_window (gf_display_t display, gf_native_window_t window)
{
    (void)display;

    if (!IsWindow (window))
        return GF_ERROR_INVALID_PARAMETER;

    if (ShowWindow (window, SW_MINIMIZE) == 0)
        return GF_ERROR_PLATFORM_ERROR;

    return GF_SUCCESS;
}

gf_error_code_t
gf_platform_unminimize_window (gf_display_t display, gf_native_window_t window)
{
    (void)display;

    if (!IsWindow (window))
        return GF_ERROR_INVALID_PARAMETER;

    if (ShowWindow (window, SW_RESTORE) == 0)
        return GF_ERROR_PLATFORM_ERROR;

    SetForegroundWindow (window);
    return GF_SUCCESS;
}

void
gf_platform_get_window_name (gf_display_t display, gf_native_window_t window, char *buffer,
                             size_t bufsize)
{
    (void)display;

    if (!window || !buffer || bufsize == 0)
        return;

    buffer[0] = '\0';

    if (!IsWindow (window))
        return;

    int len = GetWindowTextA (window, buffer, (int)bufsize - 1);
    if (len > 0)
        buffer[len] = '\0';
    else
        buffer[0] = '\0';
}
