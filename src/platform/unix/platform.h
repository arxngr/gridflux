#ifndef GF_PLATFORM_LINUX_H
#define GF_PLATFORM_LINUX_H

#include "../platform.h"
#include "atoms.h"
#include <X11/Xlib.h>
#include <stdbool.h>

// Border structure
// Linux platform data
typedef struct
{
    gf_platform_atoms_t atoms;
    int screen;
    Window root_window;
    Display *display;
    bool use_kwin_backend;
#ifdef GF_KWIN_SUPPORT
    void *kwin_dbus_conn; // DBusConnection*
    char *kwin_script_name;
#endif
    gf_border_t **borders;
    int border_count;
} gf_linux_platform_data_t;

// Platform interface (Linux implementation)
gf_platform_interface_t *gf_platform_create (void);
void gf_platform_destroy (gf_platform_interface_t *platform);

// Internal platform functions (declared here for use in other linux files)
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
// Additional platform functions
gf_window_id_t gf_platform_active_window (gf_display_t dpy);
gf_error_code_t gf_platform_minimize_window (gf_display_t display,
                                             gf_native_window_t window);
gf_error_code_t gf_platform_unminimize_window (gf_display_t display,
                                               gf_window_id_t window);
void gf_platform_get_window_name (gf_display_t display, gf_native_window_t win,
                                  char *buffer, size_t bufsize);
static gf_error_code_t gf_platform_noop_get_screen_bounds (gf_display_t display,
                                                           gf_rect_t *bounds);
static gf_error_code_t gf_platform_noop_set_window_geometry (gf_display_t display,
                                                             gf_native_window_t window,
                                                             const gf_rect_t *geometry,
                                                             gf_geometry_flags_t flags,
                                                             gf_config_t *cfg);

gf_error_code_t gf_platform_get_screen_bounds (gf_display_t dpy, gf_rect_t *bounds);

gf_error_code_t gf_platform_set_window_geometry (gf_display_t display,
                                                 gf_native_window_t window,
                                                 const gf_rect_t *geometry,
                                                 gf_geometry_flags_t flags,
                                                 gf_config_t *cfg);

gf_error_code_t gf_platform_get_frame_extents (Display *dpy, Window win, int *left,
                                               int *right, int *top, int *bottom,
                                               bool *is_csd);
gf_error_code_t gf_platform_get_window_property (Display *display, Window window,
                                                 Atom property, Atom type,
                                                 unsigned char **data,
                                                 unsigned long *nitems);
gf_error_code_t gf_platform_send_client_message (Display *display, Window window,
                                                 Atom message_type, long *data,
                                                 int count);
bool gf_platform_window_has_state (Display *display, Window window, Atom state);
bool gf_platform_is_window_minimized (gf_display_t display, gf_native_window_t window);
void gf_platform_update_borders (gf_platform_interface_t *platform);
void gf_platform_add_border (gf_platform_interface_t *platform, gf_native_window_t window,
                             gf_color_t color, int thickness);
void gf_platform_set_border_color (gf_platform_interface_t *platform, gf_color_t color);
void gf_platform_cleanup_borders (gf_platform_interface_t *platform);
void gf_platform_remove_border (gf_platform_interface_t *platform,
                                gf_native_window_t window);
bool gf_platform_is_fullscreen (gf_display_t display, gf_native_window_t window);

#endif // GF_PLATFORM_LINUX_H
