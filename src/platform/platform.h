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
    GF_KEY_NONE = 0,
    GF_KEY_WORKSPACE_PREV,
    GF_KEY_WORKSPACE_NEXT,
    GF_KEY_EXCLUDE_FOCUSED,
} gf_key_action_t;

struct gf_platform
{
    // --- Lifecycle & Core ---
    gf_err_t (*init) (gf_platform_t *platform, gf_display_t *display);
    void (*cleanup) (gf_display_t display, gf_platform_t *platform);

    // --- Window Enumeration & Info ---
    gf_err_t (*window_enumerate) (gf_display_t display, gf_ws_id_t *workspace_id,
                                  gf_win_info_t **windows, uint32_t *count);
    gf_handle_t (*window_get_focused) (gf_display_t display);
    void (*window_get_class) (gf_display_t display, gf_handle_t win, char *buffer,
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
    gf_err_t (*window_focus) (gf_display_t display, gf_handle_t window);
    gf_err_t (*window_set_maximized) (gf_display_t display, gf_handle_t window,
                                      bool maximized);
    bool window_maximize_async; // Native maximize requests can complete in later ticks
    gf_err_t (*window_fill_maximized) (gf_display_t display, gf_handle_t window,
                                       bool fill_monitor);

    // --- Workspace & Screen ---
    gf_err_t (*screen_get_bounds) (gf_display_t display, gf_rect_t *bounds);
    uint32_t (*workspace_get_count) (gf_display_t display);

    // --- Monitor Management ---
    uint32_t (*monitor_get_count) (gf_platform_t *platform);
    gf_err_t (*monitor_enumerate) (gf_platform_t *platform, gf_monitor_t *monitors,
                                   uint32_t *count);
    gf_monitor_id_t (*monitor_from_window) (gf_platform_t *platform, gf_handle_t window);
    // Suspend window reconciliation while the native display topology settles.
    bool (*monitor_poll) (gf_platform_t *platform);
    gf_err_t (*window_restore_monitor) (gf_platform_t *platform,
                                        const gf_win_info_t *window,
                                        const gf_rect_t *previous_bounds);
    gf_err_t (*screen_get_bounds_for_monitor) (gf_display_t display,
                                               gf_monitor_id_t monitor_id,
                                               gf_rect_t *bounds);

    // --- Border Management ---
    void (*border_add) (gf_platform_t *platform, gf_handle_t window, gf_color_t color,
                        int thickness);
    void (*border_cleanup) (gf_platform_t *platform);
    void (*border_remove) (gf_platform_t *platform, gf_handle_t window);
    void (*border_update) (gf_platform_t *platform, const gf_config_t *config);

    // --- Dock Management ---
    void (*dock_hide) (gf_platform_t *platform);
    void (*dock_restore) (gf_platform_t *platform);
    void (*dock_sync) (gf_platform_t *platform, const bool *hide_on_monitor,
                       uint32_t monitor_count);

    // --- Keymap Support ---
    gf_err_t (*keymap_init) (gf_platform_t *platform, gf_display_t display);
    void (*keymap_cleanup) (gf_platform_t *platform);
    gf_key_action_t (*keymap_poll) (gf_platform_t *platform, gf_display_t display);
    // Foreground window captured at the moment the last hotkey was pressed,
    // before arrangement can shift focus. 0 if none.
    gf_handle_t (*keymap_focused_window) (gf_platform_t *platform);

    // --- Resize Interaction ---
    bool (*window_is_interacting) (gf_display_t display);
    gf_err_t (*resize_hook_install) (gf_platform_t *platform);
    void (*resize_hook_uninstall) (gf_platform_t *platform);
    bool (*resize_poll) (gf_platform_t *platform, gf_resize_event_t *event);

    void *platform_data;
};

gf_platform_t *gf_platform_create (void);
void gf_platform_destroy (gf_platform_t *platform);

#endif // GF_PLATFORM_H
