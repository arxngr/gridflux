#include "../../utils/logger.h"
#include "internal.h"
#include <X11/Xlib.h>
#include <stdlib.h>
#include <string.h>

// Apply a single `gsettings set <schema> <key> <value>` (fails silently if the
// schema is absent, which is expected when a given dock extension isn't installed).
static void
_gsettings_set (const char *schema, const char *key, const char *value)
{
    char *args[]
        = { "gsettings", "set", (char *)schema, (char *)key, (char *)value, NULL };
    run_cmd_sync ("gsettings", args);
}

// Ask GNOME's Ubuntu Dock / Dash to Dock extensions to auto-hide.
static void
_dock_hide_gnome (void)
{
    const char *desktop = getenv ("XDG_CURRENT_DESKTOP");
    if (!desktop || !strstr (desktop, "GNOME"))
        return;

    // Fire at both extensions; whichever isn't installed fails silently.
    const char *schemas[] = { "org.gnome.shell.extensions.ubuntu-dock",
                              "org.gnome.shell.extensions.dash-to-dock" };
    for (int i = 0; i < 2; i++)
    {
        _gsettings_set (schemas[i], "dock-fixed", "false");
        _gsettings_set (schemas[i], "intellihide", "true");
    }

    GF_LOG_INFO ("Tried auto-hiding GNOME docks");
}

// True if the window is already recorded in the saved dock list.
static bool
_dock_already_saved (gf_linux_platform_data_t *data, Window w)
{
    for (int j = 0; j < data->saved_dock_count; j++)
        if (data->saved_dock_windows[j] == w)
            return true;
    return false;
}

// Unmap a dock-type window and record it for later restore.
static void
_dock_save_and_hide (Display *dpy, Window w, gf_linux_platform_data_t *data,
                     const char *origin)
{
    data->saved_dock_windows[data->saved_dock_count++] = w;
    XUnmapWindow (dpy, w);
    GF_LOG_DEBUG ("Hidden dock window%s %lu", origin, (unsigned long)w);
}

// Hide dock-type windows listed in the EWMH _NET_CLIENT_LIST.
static void
_dock_hide_from_client_list (Display *dpy, Window root, gf_platform_atoms_t *atoms,
                             gf_linux_platform_data_t *data)
{
    unsigned char *clients_data = NULL;
    unsigned long clients_count = 0;
    Atom actual_type;
    int actual_format;
    unsigned long bytes_after;

    if (XGetWindowProperty (dpy, root, atoms->net_client_list, 0, 4096, False, XA_WINDOW,
                            &actual_type, &actual_format, &clients_count, &bytes_after,
                            &clients_data)
            != Success
        || !clients_data)
        return;

    Window *clients = (Window *)clients_data;
    for (unsigned long i = 0;
         i < clients_count && data->saved_dock_count < GF_MAX_DOCK_WINDOWS; i++)
    {
        if (window_has_type (dpy, clients[i], atoms->net_wm_window_type_dock))
            _dock_save_and_hide (dpy, clients[i], data, "");
    }

    XFree (clients_data);
}

// Hide override-redirect docks that are root children but not in the client list.
static void
_dock_hide_from_root_children (Display *dpy, Window root, gf_platform_atoms_t *atoms,
                               gf_linux_platform_data_t *data)
{
    Window root_ret, parent_ret;
    Window *children = NULL;
    unsigned int nchildren = 0;

    if (!XQueryTree (dpy, root, &root_ret, &parent_ret, &children, &nchildren))
        return;

    for (unsigned int i = 0;
         i < nchildren && data->saved_dock_count < GF_MAX_DOCK_WINDOWS; i++)
    {
        if (_dock_already_saved (data, children[i]))
            continue;

        if (window_has_type (dpy, children[i], atoms->net_wm_window_type_dock))
            _dock_save_and_hide (dpy, children[i], data, " (root child)");
    }

    if (children)
        XFree (children);
}

void
gf_dock_hide (gf_platform_t *platform)
{
    if (!platform || !platform->platform_data)
        return;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    Display *dpy = data->display;

    if (data->dock_hidden)
        return;

    _dock_hide_gnome ();

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    if (!atoms)
        return;

    Window root = DefaultRootWindow (dpy);
    data->saved_dock_count = 0;

    _dock_hide_from_client_list (dpy, root, atoms, data);
    _dock_hide_from_root_children (dpy, root, atoms, data);

    data->dock_hidden = true;
    XFlush (dpy);
    GF_LOG_INFO ("Dock auto-hidden (%d dock windows)", data->saved_dock_count);
}

// Ask GNOME's Ubuntu Dock / Dash to Dock extensions to become fixed again.
static void
_dock_restore_gnome (void)
{
    const char *desktop = getenv ("XDG_CURRENT_DESKTOP");
    if (!desktop || !strstr (desktop, "GNOME"))
        return;

    _gsettings_set ("org.gnome.shell.extensions.ubuntu-dock", "dock-fixed", "true");
    _gsettings_set ("org.gnome.shell.extensions.dash-to-dock", "dock-fixed", "true");

    GF_LOG_INFO ("Tried restoring GNOME docks");
}

void
gf_dock_restore (gf_platform_t *platform)
{
    if (!platform || !platform->platform_data)
        return;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    Display *dpy = data->display;

    _dock_restore_gnome ();

    if (!data->dock_hidden)
        return;

    if (data->saved_dock_count == 0)
    {
        // Still reset flag if we think it's hidden but have no windows saved
        data->dock_hidden = false;
        return;
    }

    if (!dpy)
        return;

    for (int i = 0; i < data->saved_dock_count; i++)
    {
        XMapWindow (dpy, data->saved_dock_windows[i]);
        XMapRaised (dpy, data->saved_dock_windows[i]);
        GF_LOG_DEBUG ("Restored dock window %lu",
                      (unsigned long)data->saved_dock_windows[i]);
    }

    data->dock_hidden = false;
    data->saved_dock_count = 0;
    XFlush (dpy);
    GF_LOG_INFO ("Dock restored");
}
