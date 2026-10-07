#include "config.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char selected_path[PATH_MAX];

gf_err_t
gf_config_set_path (const char *path)
{
    if (!path || !path[0] || strlen (path) >= sizeof (selected_path))
        return GF_ERROR_INVALID_PARAMETER;
    memmove (selected_path, path, strlen (path) + 1);
    return GF_SUCCESS;
}

const char *
gf_config_get_path (void)
{
    static char config_path[PATH_MAX];
    if (selected_path[0])
        return selected_path;

#ifdef GF_DEV_MODE
    strncpy (config_path, "config.json", sizeof (config_path) - 1);
    config_path[sizeof (config_path) - 1] = '\0';
    return config_path;
#else
#ifdef _WIN32
    const char *appdata = getenv ("APPDATA");
    if (!appdata || appdata[0] == '\0')
    {
        fprintf (stderr, "Error: APPDATA environment variable not set or empty\n");
        return NULL;
    }

    snprintf (config_path, sizeof (config_path), "%s\\gridflux\\config.json", appdata);

    // Ensure the directory exists
    char gridflux_dir[PATH_MAX];
    snprintf (gridflux_dir, sizeof (gridflux_dir), "%s\\gridflux", appdata);
    _mkdir (gridflux_dir);

    return config_path;
#else
    // Unix-like systems
    const char *xdg_config = getenv ("XDG_CONFIG_HOME");
    if (xdg_config && xdg_config[0] != '\0')
    {
        snprintf (config_path, sizeof (config_path), "%s/gridflux/config.json",
                  xdg_config);

        // Ensure the directory exists
        char gridflux_dir[PATH_MAX];
        snprintf (gridflux_dir, sizeof (gridflux_dir), "%s/gridflux", xdg_config);
        mkdir (gridflux_dir, 0755);

        return config_path;
    }

    const char *home = getenv ("HOME");
    if (!home || home[0] == '\0')
    {
        fprintf (stderr, "Error: HOME environment variable not set\n");
        return NULL;
    }

    snprintf (config_path, sizeof (config_path), "%s/.config/gridflux/config.json", home);

    // Ensure the directory exists
    char config_dir[PATH_MAX];
    snprintf (config_dir, sizeof (config_dir), "%s/.config", home);
    mkdir (config_dir, 0755);

    char gridflux_dir[PATH_MAX];
    snprintf (gridflux_dir, sizeof (gridflux_dir), "%s/.config/gridflux", home);
    mkdir (gridflux_dir, 0755);

    return config_path;
#endif
#endif
}

#ifdef _WIN32
// Resolve before elevation: runas may start the server under another account.
bool
gf_config_get_launch_args (wchar_t *args, size_t capacity)
{
    const char *path = gf_config_get_path ();
    char absolute[PATH_MAX];
    if (!path || !args || !capacity || capacity > INT_MAX)
        return false;
    DWORD length = GetFullPathNameA (path, sizeof (absolute), absolute, NULL);
    if (!length || length >= sizeof (absolute) || strchr (absolute, '\"'))
        return false;
    wchar_t wide[PATH_MAX];
    if (!MultiByteToWideChar (CP_ACP, 0, absolute, -1, wide, PATH_MAX))
        return false;
    int written = _snwprintf (args, capacity, L"--config \"%s\"", wide);
    return written >= 0 && (size_t)written < capacity;
}
#endif
