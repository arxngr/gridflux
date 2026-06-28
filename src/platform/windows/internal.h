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
BOOL window_is_app (HWND hwnd);
BOOL window_validate (HWND hwnd);
bool window_is_self (gf_display_t display, gf_handle_t window);
BOOL window_is_excluded_class (const char *class_name);
BOOL window_is_border_excluded (HWND hwnd);
BOOL window_is_excluded_style (HWND hwnd);
BOOL window_is_fullscreen (HWND hwnd);
BOOL window_is_cloaked (HWND hwnd);
BOOL window_is_notification_center (HWND hwnd);
void gf_window_get_class (gf_display_t display, gf_handle_t window, char *buffer,
                          size_t bufsize);

// --- Border Rendering (Win32 Overlay) ---
HWND create_border_overlay (HWND target);
void gf_border_remove (gf_platform_t *platform, gf_handle_t window);
void gf_border_update (gf_platform_t *platform, const gf_config_t *config);
void gf_border_cleanup (gf_platform_t *platform);

// --- Resize Interaction (WinEventHook) ---
gf_err_t gf_resize_hook_install (gf_platform_t *platform);
void gf_resize_hook_uninstall (gf_platform_t *platform);
bool gf_resize_poll (gf_platform_t *platform, gf_resize_event_t *event);

#endif // GF_PLATFORM_WINDOWS_INTERNAL_H
