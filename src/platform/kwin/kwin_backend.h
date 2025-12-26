#ifndef GF_KWIN_BACKEND_H
#define GF_KWIN_BACKEND_H

#include "../../core/interfaces.h"
#include "../../core/types.h"

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
        "./src/platform/kwin/main.qml",
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

#endif
