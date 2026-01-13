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

#define MAX_WINDOWS 1024
#define MAX_TITLE_LENGTH 256
#define MAX_CLASS_NAME_LENGTH 256

static bool
_validate_window(HWND hwnd)
{
    return hwnd && IsWindow(hwnd);
}

static bool
_is_fullscreen_window(HWND hwnd)
{
    if (!_validate_window(hwnd) || !IsWindowVisible(hwnd))
        return false;

    if (GetWindow(hwnd, GW_OWNER))
        return false;

    RECT win, screen;
    GetWindowRect(hwnd, &win);

    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { .cbSize = sizeof(mi) };
    GetMonitorInfo(mon, &mi);
    screen = mi.rcMonitor;

    return (win.left   <= screen.left  &&
            win.top    <= screen.top   &&
            win.right  >= screen.right &&
            win.bottom >= screen.bottom);
}

static bool
_is_cloaked_window(HWND hwnd)
{
    DWORD cloaked = 0;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return SUCCEEDED(hr) && cloaked;
}

static bool
_is_app_window(HWND hwnd)
{
    if (!_validate_window(hwnd))
        return false;

    bool minimized = IsIconic(hwnd);
    if (!IsWindowVisible(hwnd) && !minimized)
        return false;

    if (_is_cloaked_window(hwnd))
        return false;

    if (GetParent(hwnd) != NULL || hwnd == GetShellWindow())
        return false;

    char title[MAX_TITLE_LENGTH];
    return GetWindowTextA(hwnd, title, sizeof(title)) > 0;
}

static bool
_is_excluded_class(const char *class_name, const char *title)
{
    static const char *excluded_classes[] = {
        "Shell_TrayWnd",
        "TrayNotifyWnd",
        "NotifyIconOverflowWindow",
        "Windows.UI.Core.CoreWindow",
        "Xaml_Windowed_Popup",
        "TopLevelWindowForOverflowXamlIsland"
    };

    for (size_t i = 0; i < sizeof(excluded_classes) / sizeof(excluded_classes[0]); i++) {
        if (strcmp(class_name, excluded_classes[i]) == 0)
            return true;
    }

    if (strstr(class_name, "Xaml") || strstr(class_name, "Overflow"))
        return true;

    if (strcmp(class_name, "ApplicationFrameWindow") == 0 && title[0] == '\0')
        return true;

    return false;
}

static bool
_is_excluded_style(HWND hwnd)
{
    LONG exstyle = GetWindowLongA(hwnd, GWL_EXSTYLE);

    if (exstyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))
        return true;

    char title[MAX_TITLE_LENGTH];
    gf_platform_get_window_name(NULL, hwnd, title, sizeof(title));

    if ((exstyle & WS_EX_TOPMOST) && title[0] == '\0')
        return true;

    return false;
}

static void
_get_taskbar_dimensions(int *left, int *right, int *top, int *bottom)
{
    *left = *right = *top = *bottom = 0;

    APPBARDATA abd = { .cbSize = sizeof(abd) };
    if (!SHAppBarMessage(ABM_GETTASKBARPOS, &abd))
        return;

    switch (abd.uEdge) {
        case ABE_LEFT:   *left   = abd.rc.right - abd.rc.left; break;
        case ABE_RIGHT:  *right  = abd.rc.right - abd.rc.left; break;
        case ABE_TOP:    *top    = abd.rc.bottom - abd.rc.top; break;
        case ABE_BOTTOM: *bottom = abd.rc.bottom - abd.rc.top; break;
    }
}

gf_platform_interface_t *
gf_platform_create(void)
{
    gf_platform_interface_t *platform = gf_malloc(sizeof(gf_platform_interface_t));
    if (!platform)
        return NULL;

    gf_windows_platform_data_t *data = gf_malloc(sizeof(gf_windows_platform_data_t));
    if (!data) {
        gf_free(platform);
        return NULL;
    }

    memset(platform, 0, sizeof(gf_platform_interface_t));
    memset(data, 0, sizeof(gf_windows_platform_data_t));

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
    platform->platform_data = data;

    return platform;
}

void
gf_platform_destroy(gf_platform_interface_t *platform)
{
    if (!platform)
        return;

    gf_free(platform->platform_data);
    gf_free(platform);
}

gf_error_code_t
gf_platform_init(gf_platform_interface_t *platform, gf_display_t *display)
{
    GF_LOG_INFO("Initialize Windows platform...");
    
    if (!display)
        return GF_ERROR_INVALID_PARAMETER;

    *display = NULL;

    gf_windows_platform_data_t *data = (gf_windows_platform_data_t *)platform->platform_data;
    data->monitor_count = GetSystemMetrics(SM_CMONITORS);

    GF_LOG_INFO("Platform initialized successfully (monitors: %d)", data->monitor_count);
    return GF_SUCCESS;
}

void
gf_platform_cleanup(gf_display_t display, gf_platform_interface_t *platform)
{
    (void)display;

    if (!platform || !platform->platform_data)
        return;

    GF_LOG_INFO("Platform cleaned up");
    gf_free(platform->platform_data);
}

gf_error_code_t
gf_platform_get_windows(gf_display_t display, gf_workspace_id_t *workspace_id,
                        gf_window_info_t **windows, uint32_t *count)
{
    (void)display;
    (void)workspace_id;

    if (!windows || !count)
        return GF_ERROR_INVALID_PARAMETER;

    gf_window_info_t *window_list = gf_malloc(MAX_WINDOWS * sizeof(gf_window_info_t));
    if (!window_list)
        return GF_ERROR_MEMORY_ALLOCATION;

    uint32_t found_count = 0;
    HWND hwnd = GetTopWindow(NULL);

    while (hwnd && found_count < MAX_WINDOWS) {
        if (_is_app_window(hwnd)) {
            RECT rect;
            if (GetWindowRect(hwnd, &rect)) {
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

                gf_platform_get_window_name(display, hwnd, window_list[found_count].name,
                                           sizeof(window_list[found_count].name));
                found_count++;
            }
        }
        hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
    }

    if (found_count == 0) {
        gf_free(window_list);
        *windows = NULL;
        *count = 0;
        return GF_SUCCESS;
    }

    gf_window_info_t *resized = gf_realloc(window_list, found_count * sizeof(gf_window_info_t));
    if (!resized) {
        gf_free(window_list);
        return GF_ERROR_MEMORY_ALLOCATION;
    }

    *windows = resized;
    *count = found_count;
    return GF_SUCCESS;
}

gf_error_code_t
gf_platform_get_window_geometry(gf_display_t display, gf_native_window_t window,
                                gf_rect_t *geometry)
{
    (void)display;

    if (!geometry || !_validate_window(window))
        return GF_ERROR_INVALID_PARAMETER;

    RECT rect;
    if (!GetWindowRect(window, &rect))
        return GF_ERROR_PLATFORM_ERROR;

    geometry->x = rect.left;
    geometry->y = rect.top;
    geometry->width = (gf_dimension_t)(rect.right - rect.left);
    geometry->height = (gf_dimension_t)(rect.bottom - rect.top);

    return GF_SUCCESS;
}

gf_error_code_t
gf_platform_set_window_geometry(gf_display_t display, gf_native_window_t window,
                                const gf_rect_t *geometry, gf_geometry_flags_t flags,
                                gf_config_t *cfg)
{
    (void)display;

    if (!geometry || !_validate_window(window))
        return GF_ERROR_INVALID_PARAMETER;

    if (IsIconic(window))
        return GF_SUCCESS;

    gf_rect_t rect = *geometry;

    if (flags & GF_GEOMETRY_APPLY_PADDING)
        gf_rect_apply_padding(&rect, cfg->default_padding);

    if (IsZoomed(window))
        ShowWindow(window, SW_RESTORE);

    if (!MoveWindow(window, rect.x, rect.y, (int)rect.width, (int)rect.height, TRUE))
        return GF_ERROR_PLATFORM_ERROR;

    return GF_SUCCESS;
}

gf_error_code_t
gf_platform_unmaximize_window(gf_display_t display, gf_native_window_t window)
{
    (void)display;

    if (!_validate_window(window))
        return GF_ERROR_INVALID_PARAMETER;

    if (!IsZoomed(window))
        return GF_SUCCESS;

    if (ShowWindow(window, SW_RESTORE) == 0)
        return GF_ERROR_PLATFORM_ERROR;

    return GF_SUCCESS;
}

gf_workspace_id_t
gf_platform_get_current_workspace(gf_display_t display)
{
    (void)display;
    return 0;
}

uint32_t
gf_platform_get_workspace_count(gf_display_t display)
{
    (void)display;
    return 1;
}

gf_error_code_t
gf_platform_create_workspace(gf_display_t display)
{
    (void)display;
    GF_LOG_WARN("Cannot create workspace on Windows - use Windows+Ctrl+D instead");
    return GF_ERROR_PLATFORM_ERROR;
}

bool
gf_platform_is_window_valid(gf_display_t display, gf_native_window_t window)
{
    (void)display;
    return _validate_window(window);
}

bool
gf_platform_is_window_excluded(gf_display_t display, gf_native_window_t window)
{
    (void)display;

    HWND hwnd = (HWND)window;

    if (!_validate_window(hwnd))
        return true;

    if (GetWindow(hwnd, GW_OWNER) != NULL)
        return true;

    if (_is_excluded_style(hwnd))
        return true;

    if (_is_fullscreen_window(hwnd))
        return true;

    if (_is_cloaked_window(hwnd))
        return true;

    char title[MAX_TITLE_LENGTH];
    gf_platform_get_window_name(display, hwnd, title, sizeof(title));

    static const char *excluded_titles[] = {
        "GridFlux",
        "DWM Notification Window"
    };

    for (size_t i = 0; i < sizeof(excluded_titles) / sizeof(excluded_titles[0]); i++) {
        if (strcmp(title, excluded_titles[i]) == 0)
            return true;
    }

    char class_name[MAX_CLASS_NAME_LENGTH];
    if (GetClassNameA(hwnd, class_name, sizeof(class_name))) {
        if (_is_excluded_class(class_name, title))
            return true;
    }

    return false;
}

gf_error_code_t
gf_platform_is_window_drag(gf_display_t display, gf_native_window_t window,
                           gf_rect_t *geometry)
{
    (void)display;
    (void)window;

    memset(geometry, 0, sizeof(*geometry));
    return GF_SUCCESS;
}

gf_error_code_t
gf_platform_get_screen_bounds(gf_display_t display, gf_rect_t *bounds)
{
    (void)display;

    if (!bounds)
        return GF_ERROR_INVALID_PARAMETER;

    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    int panel_left, panel_right, panel_top, panel_bottom;
    _get_taskbar_dimensions(&panel_left, &panel_right, &panel_top, &panel_bottom);

    bounds->x = x + panel_left;
    bounds->y = y + panel_top;
    bounds->width = (gf_dimension_t)(width - panel_left - panel_right);
    bounds->height = (gf_dimension_t)(height - panel_top - panel_bottom);

    return GF_SUCCESS;
}

gf_window_id_t
gf_platform_active_window(gf_display_t display)
{
    (void)display;

    HWND hwnd = GetForegroundWindow();
    if (_validate_window(hwnd) && _is_app_window(hwnd))
        return (gf_window_id_t)hwnd;

    return 0;
}

gf_error_code_t
gf_platform_minimize_window(gf_display_t display, gf_native_window_t window)
{
    (void)display;

    if (!_validate_window(window))
        return GF_ERROR_INVALID_PARAMETER;

    if (ShowWindow(window, SW_MINIMIZE) == 0)
        return GF_ERROR_PLATFORM_ERROR;

    return GF_SUCCESS;
}

gf_error_code_t
gf_platform_unminimize_window(gf_display_t display, gf_native_window_t window)
{
    (void)display;

    if (!_validate_window(window))
        return GF_ERROR_INVALID_PARAMETER;

    if (ShowWindow(window, SW_RESTORE) == 0)
        return GF_ERROR_PLATFORM_ERROR;

    SetForegroundWindow(window);
    return GF_SUCCESS;
}

void
gf_platform_get_window_name(gf_display_t display, gf_native_window_t window,
                            char *buffer, size_t bufsize)
{
    (void)display;

    if (!window || !buffer || bufsize == 0)
        return;

    buffer[0] = '\0';

    if (!_validate_window(window))
        return;

    int len = GetWindowTextA(window, buffer, (int)bufsize - 1);
    if (len > 0)
        buffer[len] = '\0';
    else
        buffer[0] = '\0';
}