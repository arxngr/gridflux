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
    window_border_t *borders[GF_MAX_WINDOWS_PER_WORKSPACE * GF_MAX_WORKSPACES];
    int border_count;
} gf_windows_platform_data_t;

// Platform interface (Windows implementation)
gf_platform_interface_t *gf_platform_create (void);
void gf_platform_destroy (gf_platform_interface_t *platform);

// Internal platform functions
gf_error_code_t gf_platform_init (gf_platform_interface_t *platform,
                                  gf_display_t *display);
void gf_platform_cleanup (gf_display_t display, gf_platform_interface_t *platform);
gf_error_code_t gf_platform_get_windows (gf_display_t display,
                                         gf_workspace_id_t *workspace_id,
                                         gf_window_info_t **windows, uint32_t *count);
gf_error_code_t gf_platform_set_window_geometry (gf_display_t display,
                                                 gf_native_window_t window,
                                                 const gf_rect_t *geometry,
                                                 gf_geometry_flags_t flags,
                                                 gf_config_t *cfg);
gf_error_code_t gf_platform_unmaximize_window (gf_display_t display,
                                               gf_native_window_t window);
gf_error_code_t gf_platform_get_window_geometry (gf_display_t display,
                                                 gf_native_window_t window,
                                                 gf_rect_t *geometry);
gf_workspace_id_t gf_platform_get_current_workspace (gf_display_t display);
uint32_t gf_platform_get_workspace_count (gf_display_t display);
gf_error_code_t gf_platform_create_workspace (gf_display_t display);
gf_error_code_t gf_platform_get_screen_bounds (gf_display_t display, gf_rect_t *bounds);
bool gf_platform_is_window_valid (gf_display_t display, gf_native_window_t window);
bool gf_platform_is_window_excluded (gf_display_t display, gf_native_window_t window);
gf_error_code_t gf_platform_is_window_drag (gf_display_t display,
                                            gf_native_window_t window,
                                            gf_rect_t *geometry);
gf_window_id_t gf_platform_active_window (gf_display_t display);
gf_error_code_t gf_platform_minimize_window (gf_display_t display,
                                             gf_native_window_t window);
gf_error_code_t gf_platform_unminimize_window (gf_display_t display,
                                               gf_native_window_t window);
void gf_platform_get_window_name (gf_display_t display, gf_native_window_t win,
                                  char *buffer, size_t bufsize);
bool gf_platform_window_minimized (gf_display_t display, gf_native_window_t window);
void gf_platform_add_border (gf_platform_interface_t *platform, gf_native_window_t window,
                             gf_color_t color, int thickness);
void gf_platform_update_borders (gf_platform_interface_t *platform);
void gf_platform_cleanup_borders (gf_platform_interface_t *platform);

#endif // GF_PLATFORM_WINDOWS_H
