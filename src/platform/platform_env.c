#ifdef GF_KWIN_SUPPORT
#include "platform/linux/backend.h"
#endif
#include "platform/linux/backend.h"
#include <stdlib.h>
#include <string.h>

static bool
str_has (const char *env, const char *token)
{
    if (!env || !token)
        return false;

    size_t len = strlen (token);
    const char *p = env;

    while (*p)
    {
        if (strncmp (p, token, len) == 0 && (p[len] == '\0' || p[len] == ':'))
            return true;

        p = strchr (p, ':');
        if (!p)
            break;
        p++;
    }

    return false;
}

gf_desktop_env_t
gf_detect_desktop_env (void)
{
    const char *de = getenv ("XDG_CURRENT_DESKTOP");

    if (de)
    {
        /* KDE / Plasma */
        if (str_has (de, "KDE"))
            return GF_DE_KDE;

        /*
         * GNOME family:
         *  - GNOME
         *  - ubuntu:GNOME
         *  - GNOME-Classic
         *  - Budgie:GNOME
         */
        if (str_has (de, "GNOME"))
            return GF_DE_GNOME;
    }

    /*
     * Fallbacks (important for GNOME Shell)
     */
    if (getenv ("GNOME_SHELL_SESSION_MODE"))
        return GF_DE_GNOME;

    const char *session = getenv ("DESKTOP_SESSION");
    if (session && strstr (session, "gnome"))
        return GF_DE_GNOME;

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
