#ifndef GF_PLATFORM_LINUX_BACKEND_H
#define GF_PLATFORM_LINUX_BACKEND_H

#include "../../config.h"
#include "../../types.h"
#include "../../platform/platform.h"
#include <X11/Xlib.h>
#include <stdbool.h>

#ifdef GF_KWIN_SUPPORT

#define KWIN_DBUS_SERVICE "org.kde.KWin"
#define KWIN_DBUS_OBJECT_PATH "/Scripting"
#define KWIN_DBUS_INTERFACE "org.kde.kwin.Scripting"
#define KWIN_SCRIPT_NAME "gridflux-tiler"
#define KWIN_DBUS_TIMEOUT_MS 5000
#define KWIN_UNLOAD_SLEEP_SEC 1
#define KWIN_TEMP_TEMPLATE "/tmp/kwin_script_XXXXXX"

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
    void *dbus_conn;
    char *script_path;
    char *plugin_name;
    bool is_loaded;
} gf_kwin_platform_data_t;

static const char *const KWIN_SCRIPT_SEARCH_PATHS[]
    = { "/usr/local/share/kwin/scripts/gridflux/contents/ui/main.qml",
        "/usr/share/kwin/scripts/gridflux/contents/ui/main.qml",
        "/usr/local/share/gridflux/contents/ui/main.qml",
        "/usr/share/gridflux/contents/ui/main.qml",
        "./src/platform/unix/kwin/main.qml",
        NULL };

gf_desktop_env_t gf_detect_desktop_env (void);
gf_backend_type_t gf_detect_backend (void);

gf_error_code_t gf_kwin_platform_init (gf_platform_interface_t *platform);
void gf_kwin_platform_cleanup (gf_platform_interface_t *platform);
#endif // GF_KWIN_SUPPORT

#endif
