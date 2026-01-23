#ifndef GF_PLATFORM_H
#define GF_PLATFORM_H

#include "../config.h"
#include "../types.h"

// Platform-agnostic types
#ifdef __linux__
#include <X11/Xlib.h>
#endif

typedef struct gf_platform_interface gf_platform_interface_t;

struct gf_platform_interface
{
    gf_error_code_t (*init) (gf_platform_interface_t *platform, gf_display_t *display);
    void (*cleanup) (gf_display_t display, gf_platform_interface_t *platform);
    gf_error_code_t (*get_windows) (gf_display_t display, gf_workspace_id_t *workspace_id,
                                    gf_window_info_t **windows, uint32_t *count);
    gf_error_code_t (*set_window_geometry) (gf_display_t display,
                                            gf_native_window_t window,
                                            const gf_rect_t *geometry,
                                            gf_geometry_flags_t flags, gf_config_t *cfg);
    gf_error_code_t (*unmaximize_window) (gf_display_t display,
                                          gf_native_window_t window);
    gf_error_code_t (*get_window_geometry) (gf_display_t display,
                                            gf_native_window_t window,
                                            gf_rect_t *geometry);
    gf_workspace_id_t (*get_current_workspace) (gf_display_t display);
    uint32_t (*get_workspace_count) (gf_display_t display);
    gf_error_code_t (*create_workspace) (gf_display_t display);
    gf_error_code_t (*get_screen_bounds) (gf_display_t display, gf_rect_t *bounds);
    bool (*is_window_valid) (gf_display_t display, gf_native_window_t window);
    bool (*is_window_excluded) (gf_display_t display, gf_native_window_t window);
    gf_error_code_t (*remove_workspace) (gf_display_t display,
                                         gf_workspace_id_t workspace_id);
    gf_window_id_t (*get_active_window) (gf_display_t display);
    gf_error_code_t (*minimize_window) (gf_display_t display, gf_native_window_t window);
    gf_error_code_t (*unminimize_window) (gf_display_t display,
                                          gf_native_window_t window);

    void (*window_name_info) (gf_display_t display, gf_native_window_t win, char *buffer,
                              size_t bufsize);
    bool (*is_window_minimized) (gf_display_t display, gf_native_window_t window);
    bool (*is_fullscreen) (gf_display_t display, gf_native_window_t window);

    void (*add_border) (gf_platform_interface_t *platform, gf_native_window_t window,
                        gf_color_t color, int thickness);
    void (*update_border) (gf_platform_interface_t *platform);
    void (*set_border_color) (struct gf_platform_interface *platform, gf_color_t color);

    // Event hooks
    void (*start_event_hook) (struct gf_platform_interface *platform,
                              gf_window_destroy_callback_t callback, void *user_data);
    void (*cleanup_borders) (gf_platform_interface_t *platform);
    bool (*is_window_hidden) (gf_display_t display, gf_native_window_t window);
    void (*remove_border) (gf_platform_interface_t *platform, gf_native_window_t window);

    void *platform_data;
};

gf_platform_interface_t *gf_platform_create (void);
void gf_platform_destroy (gf_platform_interface_t *platform);

#endif // GF_PLATFORM_H
