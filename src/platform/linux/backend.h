#ifndef GF_PLATFORM_LINUX_BACKEND_H
#define GF_PLATFORM_LINUX_BACKEND_H

#include "../../core/config.h"
#include "../../core/types.h"
#include <X11/Xlib.h>
#include <stdbool.h>

typedef enum gf_desktop_env_t
{
    GF_DE_UNKNOWN = 0,
    GF_DE_KDE,
    GF_DE_GNOME,
    GF_DE_XFCE,
    GF_DE_LXDE,
    GF_DE_LXQT
} gf_desktop_env_t;

typedef enum
{
    GF_BACKEND_NATIVE,
    GF_BACKEND_KWIN_QML,
    GF_BACKEND_AUTO
} gf_backend_type_t;

typedef struct
{
    gf_error_code_t (*get_screen_bounds) (gf_display_t, gf_rect_t *);
    gf_error_code_t (*set_window_geometry) (gf_display_t, gf_native_window_t,
                                            const gf_rect_t *, gf_geometry_flags_t,
                                            gf_config_t *);
} gf_platform_backend_t;

gf_desktop_env_t gf_detect_desktop_env (void);

gf_error_code_t gf_platform_get_screen_bounds (gf_display_t dpy, gf_rect_t *bounds);

gf_error_code_t gf_platform_set_window_geometry (gf_display_t display,
                                                 gf_native_window_t window,
                                                 const gf_rect_t *geometry,
                                                 gf_geometry_flags_t flags,
                                                 gf_config_t *cfg);

gf_error_code_t gf_platform_kde_create_workspace (gf_display_t display);

gf_backend_type_t gf_detect_backend (void);

gf_error_code_t gf_platform_get_frame_extents (Display *dpy, Window win, int *left,
                                               int *right, int *top, int *bottom);
gf_error_code_t gf_platform_get_window_property (Display *display, Window window,
                                                 Atom property, Atom type,
                                                 unsigned char **data,
                                                 unsigned long *nitems);
gf_error_code_t gf_platform_send_client_message (Display *display, Window window,
                                                 Atom message_type, long *data,
                                                 int count);
bool gf_platform_window_has_state (Display *display, Window window, Atom state);

// KWin backend support
#ifdef GF_KWIN_SUPPORT
#include "../../platform/platform.h"

#define KWIN_DBUS_SERVICE "org.kde.KWin"
#define KWIN_DBUS_OBJECT_PATH "/Scripting"
#define KWIN_DBUS_INTERFACE "org.kde.kwin.Scripting"
#define KWIN_SCRIPT_NAME "gridflux-tiler"
#define KWIN_DBUS_TIMEOUT_MS 5000
#define KWIN_UNLOAD_SLEEP_SEC 1
#define KWIN_TEMP_TEMPLATE "/tmp/kwin_script_XXXXXX"

static const char *const KWIN_SCRIPT_SEARCH_PATHS[]
    = { "/usr/local/share/kwin/scripts/gridflux/contents/ui/main.qml",
        "/usr/share/kwin/scripts/gridflux/contents/ui/main.qml",
        "/usr/local/share/gridflux/contents/ui/main.qml",
        "/usr/share/gridflux/contents/ui/main.qml",
        "./src/platform/linux/kwin/main.qml",
        NULL };

typedef struct
{
    void *dbus_conn;
    char *script_path;
    char *plugin_name;
    bool is_loaded;
} gf_kwin_platform_data_t;

gf_error_code_t gf_kwin_platform_init (gf_platform_interface_t *platform);
void gf_kwin_platform_cleanup (gf_platform_interface_t *platform);
#endif // GF_KWIN_SUPPORT

#endif
