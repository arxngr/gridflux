
#include "platform/x11/x11_backend.h"
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

    // fallback: process detection
    if (system ("pgrep -x kwin_x11 > /dev/null") == 0)
        return GF_DE_KDE;

    if (system ("pgrep -x gnome-shell > /dev/null") == 0)
        return GF_DE_GNOME;

    return GF_DE_UNKNOWN;
}
