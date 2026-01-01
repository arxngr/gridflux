#ifdef GF_KWIN_SUPPORT
#include "platform/linux/backend.h"
#endif
#include "platform/linux/backend.h"
#include <stdlib.h>
#include <string.h>

gf_desktop_env_t
gf_detect_desktop_env (void)
{
    const char *de = getenv ("XDG_CURRENT_DESKTOP");
    if (de)
    {
        if (strstr (de, "KDE"))
            return GF_DE_KDE;
        if (strstr (de, "GNOME"))
            return GF_DE_GNOME;
    }
    return GF_DE_UNKNOWN;
}

gf_backend_type_t
gf_detect_backend (void)
{
#ifdef GF_KWIN_SUPPORT
    // If KDE detected, use KWin backend
    if (gf_detect_desktop_env () == GF_DE_KDE)
    {
        return GF_BACKEND_KWIN_QML;
    }
#endif

    // Otherwise use native backend
    return GF_BACKEND_NATIVE;
}
