#ifndef GF_PLATFORM_H
#define GF_PLATFORM_H

#include "../core/config.h"
#include "../core/types.h"

// Platform-agnostic types
#ifdef __linux__
#include <X11/Xlib.h>
#endif

typedef struct gf_platform_interface gf_platform_interface_t;
typedef struct gf_geometry_calculator gf_geometry_calculator_t;
typedef struct gf_window_filter gf_window_filter_t;

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
    gf_error_code_t (*is_window_drag) (gf_display_t display, gf_native_window_t window,
                                       gf_rect_t *geometry);
    gf_error_code_t (*remove_workspace) (gf_display_t display,
                                         gf_workspace_id_t workspace_id);
    gf_window_id_t (*get_active_window) (gf_display_t display);
    gf_error_code_t (*minimize_window) (gf_display_t display, gf_native_window_t window);
    gf_error_code_t (*unminimize_window) (gf_display_t display, gf_window_id_t window);

    void (*window_name_info) (gf_display_t display, gf_native_window_t win, char *buffer,
                              size_t bufsize);

    void *platform_data;
};

struct gf_geometry_calculator
{
    void (*calculate_layout) (const struct gf_geometry_calculator *calc,
                              const gf_window_info_t *windows, uint32_t count,
                              const gf_rect_t *workspace_bounds, gf_rect_t *results);
    void (*set_padding) (struct gf_geometry_calculator *calc, uint32_t padding);
    void (*set_min_size) (struct gf_geometry_calculator *calc, uint32_t min_size);
    const gf_config_t *config;
    void *calculator_data;
};

struct gf_window_filter
{
    bool (*should_manage) (const gf_window_info_t *window);
    void *filter_data;
};

gf_platform_interface_t *gf_platform_create (void);
void gf_platform_destroy (gf_platform_interface_t *platform);

#endif // GF_PLATFORM_H
