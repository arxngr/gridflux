#ifndef GF_PLATFORM_X11_WINDOW_MANAGER_H
#define GF_PLATFORM_X11_WINDOW_MANAGER_H

#include "core/types.h"
#ifdef GF_PLATFORM_X11

#include "../../core/geometry.h"
#include "../../core/interfaces.h"
#include "../../core/logger.h"
#include "../../utils/memory.h"
#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "x11_atoms.h"

// X11 platform data
typedef struct
{
    gf_x11_atoms_t atoms;
    int screen;
    Window root_window;
    bool use_kwin_backend;
#ifdef GF_KWIN_SUPPORT
    void *kwin_dbus_conn; // DBusConnection*
    char *kwin_script_name;
#endif
} gf_x11_platform_data_t;

// X11 platform interface
gf_platform_interface_t *gf_x11_platform_create (void);
void gf_x11_platform_destroy (gf_platform_interface_t *platform);

// X11 utility functions
gf_error_code_t gf_x11_get_window_property (Display *display, Window window,
                                            Atom property, Atom type,
                                            unsigned char **data, unsigned long *nitems);
gf_error_code_t gf_x11_send_client_message (Display *display, Window window,
                                            Atom message_type, long *data, int count);
bool gf_x11_window_has_state (Display *display, Window window, Atom state);
gf_error_code_t gf_x11_get_frame_extents (Display *display, Window window, int *left,
                                          int *right, int *top, int *bottom);

static gf_error_code_t gf_x11_platform_init (gf_platform_interface_t *platform,
                                             gf_display_t *display);
static void gf_x11_platform_cleanup (gf_display_t display,
                                     gf_platform_interface_t *platform);
static gf_error_code_t gf_x11_platform_get_windows (gf_display_t display,
                                                    gf_workspace_id_t *workspace_id,
                                                    gf_window_info_t **windows,
                                                    uint32_t *count);
static gf_error_code_t gf_x11_platform_set_window_geometry (gf_display_t display,
                                                            gf_native_window_t window,
                                                            const gf_rect_t *geometry,
                                                            gf_geometry_flags_t flags,
                                                            gf_config_t *cfg);
static gf_error_code_t gf_x11_platform_unmaximize_window (gf_display_t display,
                                                          gf_native_window_t window);
static gf_error_code_t gf_x11_platform_get_window_geometry (gf_display_t display,
                                                            gf_native_window_t window,
                                                            gf_rect_t *geometry);
static gf_workspace_id_t gf_x11_platform_get_current_workspace (gf_display_t display);
static uint32_t gf_x11_platform_get_workspace_count (gf_display_t display);
static gf_error_code_t gf_x11_platform_create_workspace (gf_display_t display);
static gf_error_code_t gf_x11_platform_get_screen_bounds (gf_display_t display,
                                                          gf_rect_t *bounds);
static bool gf_x11_platform_is_window_valid (gf_display_t display,
                                             gf_native_window_t window);
static bool gf_x11_platform_is_window_excluded (gf_display_t display,
                                                gf_native_window_t window);
const char *gf_x11_detect_desktop_environment (void);
static gf_error_code_t gf_x11_platform_is_window_drag (gf_display_t display,
                                                       gf_native_window_t window,
                                                       gf_rect_t *geometry);
static gf_error_code_t gf_x11_platform_remove_workspace (gf_display_t display,
                                                         gf_workspace_id_t workspace_id);
gf_window_id_t gf_x11_event_process (gf_display_t dpy);
gf_error_code_t gf_x11_minimize_window (gf_display_t display, gf_window_id_t window);
gf_error_code_t gf_x11_unminimize_window (gf_display_t display, gf_window_id_t window);
void gf_x11_get_window_name (gf_display_t display, gf_native_window_t win, char *buffer,
                             size_t bufsize);

#endif // GF_PLATFORM_X11

#endif // GF_PLATFORM_X11_WINDOW_MANAGER_H
