#ifndef GF_PLATFORM_WINDOWS_H
#define GF_PLATFORM_WINDOWS_H

#include "../platform.h"
#include <stdbool.h>
#include <windows.h>

// Windows platform data
typedef struct
{
    HANDLE event_hook;
    HANDLE shell_hook;
    HMONITOR monitor;
    int monitor_count;
    gf_border_t *borders[GF_MAX_WINDOWS_PER_WORKSPACE * GF_MAX_WORKSPACES];
    int border_count;
} gf_windows_platform_data_t;

// Platform interface (Windows implementation)
gf_platform_t *gf_platform_create (void);
void gf_platform_destroy (gf_platform_t *platform);

// Internal platform functions
gf_err_t gf_platform_init (gf_platform_t *platform, gf_display_t *display);
void gf_platform_cleanup (gf_display_t display, gf_platform_t *platform);
gf_err_t gf_platform_get_windows (gf_display_t display, gf_ws_id_t *workspace_id,
                                  gf_win_info_t **windows, uint32_t *count);
gf_err_t gf_window_set_geometry (gf_display_t display, gf_handle_t window,
                                 const gf_rect_t *geometry, gf_geom_flags_t flags,
                                 gf_config_t *cfg);
gf_err_t gf_window_unmaximize (gf_display_t display, gf_handle_t window);
gf_err_t gf_window_get_geometry (gf_display_t display, gf_handle_t window,
                                 gf_rect_t *geometry);
gf_ws_id_t gf_workspace_get_current (gf_display_t display);
uint32_t gf_workspace_get_count (gf_display_t display);
gf_err_t gf_workspace_create_native (gf_display_t display);
gf_err_t gf_screen_get_bounds (gf_display_t display, gf_rect_t *bounds);
bool gf_window_is_valid (gf_display_t display, gf_handle_t window);
bool gf_window_is_excluded (gf_display_t display, gf_handle_t window);
gf_err_t gf_platform_is_window_drag (gf_display_t display, gf_handle_t window,
                                     gf_rect_t *geometry);
gf_handle_t gf_window_get_focused (gf_display_t display);
gf_err_t gf_window_minimize (gf_display_t display, gf_handle_t window);
gf_err_t gf_window_unminimize (gf_display_t display, gf_handle_t window);
void gf_window_get_name (gf_display_t display, gf_handle_t win, char *buffer,
                         size_t bufsize);
bool gf_platform_window_minimized (gf_display_t display, gf_handle_t window);
void gf_border_add (gf_platform_t *platform, gf_handle_t window, gf_color_t color,
                    int thickness);
void gf_border_update (gf_platform_t *platform, const gf_config_t *config);
void gf_border_cleanup (gf_platform_t *platform);
bool gf_platform_window_hidden (gf_display_t display, gf_handle_t window);
void gf_border_remove (gf_platform_t *platform, gf_handle_t window);
bool gf_window_is_maximized (gf_display_t display, gf_handle_t window);
bool gf_window_is_fullscreen (gf_display_t display, gf_handle_t window);
void gf_dock_hide (gf_platform_t *platform);
void gf_dock_restore (gf_platform_t *platform);
#endif // GF_PLATFORM_WINDOWS_H
