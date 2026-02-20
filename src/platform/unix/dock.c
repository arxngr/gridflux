#include "internal.h"
#include "../../utils/logger.h"
#include <stdlib.h>
#include <string.h>

void
gf_dock_hide (gf_platform_t *platform)
{
    if (!platform || !platform->platform_data)
        return;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    Display *dpy = data->display;

    if (data->dock_hidden)
        return;

    // Check for GNOME session first
    const char *desktop = getenv ("XDG_CURRENT_DESKTOP");
    if (desktop && strstr (desktop, "GNOME"))
    {
        // Try Ubuntu Dock first (most likely on Ubuntu)
        char *args_ubuntu[]
            = { "gsettings",  "set",   "org.gnome.shell.extensions.ubuntu-dock",
                "dock-fixed", "false", NULL };
        _run_bg_command ("gsettings", args_ubuntu);

        // Also try standard Dash to Dock (fire both, one will fail silently)
        char *args_dash[]
            = { "gsettings",  "set",   "org.gnome.shell.extensions.dash-to-dock",
                "dock-fixed", "false", NULL };
        _run_bg_command ("gsettings", args_dash);

        data->dock_hidden = true;
        GF_LOG_INFO ("Dock auto-hidden (GNOME gsettings)");
        return;
    }

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    if (!atoms)
        return;

    // Find all dock-type windows by scanning the client list
    unsigned char *clients_data = NULL;
    unsigned long clients_count = 0;
    Atom actual_type;
    int actual_format;
    unsigned long bytes_after;

    Window root = DefaultRootWindow (dpy);

    if (XGetWindowProperty (dpy, root, atoms->net_client_list, 0, 4096, False, XA_WINDOW,
                            &actual_type, &actual_format, &clients_count, &bytes_after,
                            &clients_data)
            != Success
        || !clients_data)
    {
        return;
    }

    Window *clients = (Window *)clients_data;
    data->saved_dock_count = 0;

    for (unsigned long i = 0;
         i < clients_count && data->saved_dock_count < GF_MAX_DOCK_WINDOWS; i++)
    {
        if (_window_has_type (dpy, clients[i], atoms->net_wm_window_type_dock))
        {
            data->saved_dock_windows[data->saved_dock_count++] = clients[i];
            XUnmapWindow (dpy, clients[i]);
            GF_LOG_DEBUG ("Hidden dock window %lu", (unsigned long)clients[i]);
        }
    }

    XFree (clients_data);

    // Also check root window children for override-redirect docks not in client list
    Window root_ret, parent_ret;
    Window *children = NULL;
    unsigned int nchildren = 0;

    if (XQueryTree (dpy, root, &root_ret, &parent_ret, &children, &nchildren))
    {
        for (unsigned int i = 0;
             i < nchildren && data->saved_dock_count < GF_MAX_DOCK_WINDOWS; i++)
        {
            // Skip if already saved
            bool already_saved = false;
            for (int j = 0; j < data->saved_dock_count; j++)
            {
                if (data->saved_dock_windows[j] == children[i])
                {
                    already_saved = true;
                    break;
                }
            }
            if (already_saved)
                continue;

            if (_window_has_type (dpy, children[i], atoms->net_wm_window_type_dock))
            {
                data->saved_dock_windows[data->saved_dock_count++] = children[i];
                XUnmapWindow (dpy, children[i]);
                GF_LOG_DEBUG ("Hidden dock window (root child) %lu",
                               (unsigned long)children[i]);
            }
        }
        if (children)
            XFree (children);
    }

    data->dock_hidden = true;
    XFlush (dpy);
    GF_LOG_INFO ("Dock auto-hidden (%d dock windows)", data->saved_dock_count);
}

void
gf_dock_restore (gf_platform_t *platform)
{
    if (!platform || !platform->platform_data)
        return;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    Display *dpy = data->display;

    // Check for GNOME session
    const char *desktop = getenv ("XDG_CURRENT_DESKTOP");
    if (desktop && strstr (desktop, "GNOME"))
    {
        // Try Ubuntu Dock
        char *args_ubuntu[]
            = { "gsettings",  "set",  "org.gnome.shell.extensions.ubuntu-dock",
                "dock-fixed", "true", NULL };
        _run_bg_command ("gsettings", args_ubuntu);

        // Try standard Dash to Dock
        char *args_dash[]
            = { "gsettings",  "set",  "org.gnome.shell.extensions.dash-to-dock",
                "dock-fixed", "true", NULL };
        _run_bg_command ("gsettings", args_dash);

        data->dock_hidden = false;
        GF_LOG_INFO ("Dock restored (GNOME gsettings)");
        return;
    }

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
