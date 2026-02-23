#ifndef GF_PLATFORM_H
#define GF_PLATFORM_H

#include "../config/config.h"
#include "../core/types.h"

// Platform-agnostic types
#ifdef __linux__
#include <X11/Xlib.h>
#endif

typedef struct gf_platform gf_platform_t;

typedef enum
{
    GF_GESTURE_NONE = 0,
    GF_GESTURE_SWIPE_BEGIN,
    GF_GESTURE_SWIPE_UPDATE,
    GF_GESTURE_SWIPE_END,
    GF_GESTURE_SWIPE_CANCEL,
} gf_gesture_type_t;

typedef struct
{
    gf_gesture_type_t type;
    double dx;
    double dy;
    double total_dx;
    double total_dy;
    int fingers;
} gf_gesture_event_t;

struct gf_platform
{
    // --- Lifecycle & Core ---
    gf_err_t (*init) (gf_platform_t *platform, gf_display_t *display);
    void (*cleanup) (gf_display_t display, gf_platform_t *platform);

    // --- Window Enumeration & Info ---
    gf_err_t (*window_enumerate) (gf_display_t display, gf_ws_id_t *workspace_id,
                                  gf_win_info_t **windows, uint32_t *count);
    gf_handle_t (*window_get_focused) (gf_display_t display);
    void (*window_get_name) (gf_display_t display, gf_handle_t win, char *buffer,
                             size_t bufsize);

    // --- Window Geometry & State ---
    gf_err_t (*window_get_geometry) (gf_display_t display, gf_handle_t window,
                                     gf_rect_t *geometry);
    bool (*window_is_excluded) (gf_display_t display, gf_handle_t window);
    bool (*window_is_fullscreen) (gf_display_t display, gf_handle_t window);
    bool (*window_is_hidden) (gf_display_t display, gf_handle_t window);
    bool (*window_is_maximized) (gf_display_t display, gf_handle_t window);
    bool (*window_is_minimized) (gf_display_t display, gf_handle_t window);
    bool (*window_is_valid) (gf_display_t display, gf_handle_t window);
    gf_err_t (*window_minimize) (gf_display_t display, gf_handle_t window);
    gf_err_t (*window_set_geometry) (gf_display_t display, gf_handle_t window,
                                     const gf_rect_t *geometry, gf_geom_flags_t flags,
                                     gf_config_t *cfg);
    gf_err_t (*window_unminimize) (gf_display_t display, gf_handle_t window);

    // --- Workspace & Screen ---
    gf_err_t (*screen_get_bounds) (gf_display_t display, gf_rect_t *bounds);
    uint32_t (*workspace_get_count) (gf_display_t display);

    // --- Border Management ---
    void (*border_add) (gf_platform_t *platform, gf_handle_t window, gf_color_t color,
                        int thickness);
    void (*border_cleanup) (gf_platform_t *platform);
    void (*border_remove) (gf_platform_t *platform, gf_handle_t window);
    void (*border_update) (gf_platform_t *platform, const gf_config_t *config);

    // --- Dock Management ---
    void (*dock_hide) (gf_platform_t *platform);
    void (*dock_restore) (gf_platform_t *platform);

    // --- Gesture Support ---
    void (*gesture_cleanup) (gf_platform_t *platform);
    gf_err_t (*gesture_init) (gf_platform_t *platform, gf_display_t display);
    bool (*gesture_poll) (gf_platform_t *platform, gf_display_t display,
                          gf_gesture_event_t *event_out);

    void *platform_data;
};

gf_platform_t *gf_platform_create (void);
void gf_platform_destroy (gf_platform_t *platform);

#endif // GF_PLATFORM_H
