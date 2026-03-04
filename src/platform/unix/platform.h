#ifndef GF_PLATFORM_LINUX_H
#define GF_PLATFORM_LINUX_H

#include "../platform.h"
#include "atoms.h"
#include <X11/Xlib.h>
#include <stdbool.h>

// Border structure
// Linux platform data
#define GF_MAX_DOCK_WINDOWS 8

typedef struct
{
    gf_platform_atoms_t atoms;
    int screen;
    Window root_window;
    Display *display;

    gf_border_t **borders;
    int border_count;

    // Dock auto-hide state
    Window saved_dock_windows[GF_MAX_DOCK_WINDOWS];
    int saved_dock_count;
    bool dock_hidden;

    // Keymap state
    bool keymap_initialized;
    int xi_opcode;
} gf_linux_platform_data_t;

// Platform interface (Linux implementation)
gf_platform_t *gf_platform_create (void);
void gf_platform_destroy (gf_platform_t *platform);

// Internal platform functions (declared here for use in other linux files)
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
// Additional platform functions
gf_handle_t gf_window_get_focused (gf_display_t dpy);
gf_err_t gf_window_minimize (gf_display_t display, gf_handle_t window);
gf_err_t gf_window_unminimize (gf_display_t display, gf_handle_t window);
gf_err_t gf_screen_get_bounds (gf_display_t dpy, gf_rect_t *bounds);

gf_err_t gf_window_set_geometry (gf_display_t display, gf_handle_t window,
                                 const gf_rect_t *geometry, gf_geom_flags_t flags,
                                 gf_config_t *cfg);

gf_err_t gf_platform_get_frame_extents (Display *dpy, Window win, int *left, int *right,
                                        int *top, int *bottom, bool *is_csd);
gf_err_t gf_platform_get_window_property (Display *display, Window window, Atom property,
                                          Atom type, unsigned char **data,
                                          unsigned long *nitems);
gf_err_t gf_platform_send_client_message (Display *display, Window window,
                                          Atom message_type, long *data, int count);
bool gf_platform_window_has_state (Display *display, Window window, Atom state);
bool gf_window_is_minimized (gf_display_t display, gf_handle_t window);
void gf_border_update (gf_platform_t *platform, const gf_config_t *config);
void gf_border_add (gf_platform_t *platform, gf_handle_t window, gf_color_t color,
                    int thickness);
void gf_border_set_color (gf_platform_t *platform, gf_color_t color);
void gf_border_cleanup (gf_platform_t *platform);
void gf_border_remove (gf_platform_t *platform, gf_handle_t window);
bool gf_window_is_fullscreen (gf_display_t display, gf_handle_t window);
bool gf_window_is_maximized (gf_display_t display, gf_handle_t window);

void gf_dock_hide (gf_platform_t *platform);
void gf_dock_restore (gf_platform_t *platform);

// --- Keymap Support ---
gf_err_t gf_keymap_init (gf_platform_t *platform, gf_display_t display);
void gf_keymap_cleanup (gf_platform_t *platform);
gf_key_action_t gf_keymap_poll (gf_platform_t *platform, gf_display_t display);

#endif // GF_PLATFORM_LINUX_H
