#ifndef GF_PLATFORM_WINDOWS_INTERNAL_H
#define GF_PLATFORM_WINDOWS_INTERNAL_H
#define MAX_TITLE_LENGTH 256
#define MAX_CLASS_NAME_LENGTH 256
#include "../../core/types.h"
#include "platform.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <windows.h>

// --- Window Management ---
void _get_window_geometry (HWND hwnd, gf_rect_t *rect);
BOOL _is_app_window (HWND hwnd);
BOOL _validate_window (HWND hwnd);
bool _window_it_self (gf_display_t display, gf_handle_t window);
BOOL _is_excluded_class (const char *class_name, const char *title);
BOOL _is_excluded_style (HWND hwnd);
BOOL _is_fullscreen_window (HWND hwnd);
BOOL _is_cloaked_window (HWND hwnd);
BOOL _is_notification_center (HWND hwnd);
void _get_window_name (gf_display_t display, HWND window, char *buffer, size_t bufsize);

// --- Workspace & System ---
void _get_taskbar_dimensions (int *left, int *right, int *top, int *bottom);

// --- Border Rendering (Win32 Overlay) ---
LRESULT CALLBACK _border_wnd_proc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
HWND _create_border_overlay (HWND target);
void _update_border (gf_border_t *b, const RECT *gui_rects, int gui_count);
void gf_border_remove (gf_platform_t *platform, gf_handle_t window);
void gf_border_update (gf_platform_t *platform, const gf_config_t *config);
void gf_border_cleanup (gf_platform_t *platform);

#endif // GF_PLATFORM_WINDOWS_INTERNAL_H
