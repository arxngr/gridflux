#include "../../utils/logger.h"
#include "internal.h"
#include <X11/Xatom.h>

gf_ws_id_t
gf_workspace_get_current (gf_display_t display)
{
    if (!display)
        return -1;

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    Window root = DefaultRootWindow (display);

    unsigned char *data = NULL;
    unsigned long nitems = 0;

    if (gf_platform_get_window_property (display, root, atoms->net_current_desktop,
                                         XA_CARDINAL, &data, &nitems)
            == GF_SUCCESS
        && nitems > 0)
    {
        gf_ws_id_t workspace = *(unsigned long *)data;
        XFree (data);
        return workspace;
    }

    return 0; // Default to workspace 0 (0-indexed EWMH)
}

uint32_t
gf_workspace_get_count (gf_display_t display)
{
    if (!display)
        return 1;

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    Window root = DefaultRootWindow (display);

    unsigned char *data = NULL;
    unsigned long nitems = 0;

    if (gf_platform_get_window_property (display, root, atoms->net_number_of_desktops,
                                         XA_CARDINAL, &data, &nitems)
            == GF_SUCCESS
        && nitems > 0)
    {
        uint32_t count = *(unsigned long *)data;
        XFree (data);
        return count;
    }

    return 1; // Default to 1 workspace
}

gf_err_t
gf_screen_get_bounds (gf_display_t dpy, gf_rect_t *bounds)
{
    if (!dpy || !bounds)
        return GF_ERROR_INVALID_PARAMETER;

    int screen = DefaultScreen (dpy);
    Window root = DefaultRootWindow (dpy);
    Screen *scr = ScreenOfDisplay (dpy, screen);
    int sw = scr->width;
    int sh = scr->height;

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();

    // Initialize with full screen
    bounds->x = 0;
    bounds->y = 0;
    bounds->width = sw;
    bounds->height = sh;

    bool workarea_valid = false;

    // Try _NET_WORKAREA first
    unsigned char *data = NULL;
    unsigned long nitems = 0;

    if (gf_platform_get_window_property (dpy, root, atoms->net_workarea, XA_CARDINAL,
                                         &data, &nitems)
            == GF_SUCCESS
        && data && nitems >= 4)
    {
        // _NET_WORKAREA is an array of 4 integers (x, y, w, h) per desktop
        const int EWMH_WORKAREA_STRIDE = 4;
        gf_ws_id_t workspace = gf_workspace_get_current (dpy);
        unsigned long offset = workspace * EWMH_WORKAREA_STRIDE;

        // Ensure we don't go out of bounds
        if (offset + (EWMH_WORKAREA_STRIDE - 1) < nitems)
        {
            long *workarea = (long *)data;
            // Validate workarea makes sense (smaller than screen, non-zero)
            if (workarea[offset + 2] > 0 && workarea[offset + 3] > 0
                && (workarea[offset + 1] > 0 || workarea[offset + 2] < sw
                    || workarea[offset + 3] < sh))
            {
                bounds->x = workarea[offset];
                bounds->y = workarea[offset + 1];
                bounds->width = workarea[offset + 2];
                bounds->height = workarea[offset + 3];
                workarea_valid = true;
            }
        }
    }

    if (data)
        XFree (data);

    // If Workarea gave full screen (or failed), try Struts to be safe
    if (!workarea_valid
        || (bounds->x == 0 && bounds->y == 0 && bounds->width == sw
            && bounds->height == sh))
    {
        int panel_left = 0, panel_right = 0, panel_top = 0, panel_bottom = 0;
        unsigned char *clients_data = NULL;
        unsigned long clients_count = 0;
        Atom actual_type;
        int actual_format;
        unsigned long bytes_after;

        if ((XGetWindowProperty (dpy, root, atoms->net_client_list, 0, 4096, False,
                                 XA_WINDOW, &actual_type, &actual_format, &clients_count,
                                 &bytes_after, &clients_data)
             == Success)
            && clients_data)
        {
            Window *clients = (Window *)clients_data;
            for (unsigned long i = 0; i < clients_count; i++)
            {
                long *strut = NULL;
                unsigned long nitems_strut = 0;

                if (gf_platform_get_window_property (
                        dpy, clients[i], atoms->net_wm_strut_partial, XA_CARDINAL,
                        (unsigned char **)&strut, &nitems_strut)
                        == GF_SUCCESS
                    && strut && nitems_strut >= 12)
                {
                    if (strut[0] > panel_left)
                        panel_left = strut[0];
                    if (strut[1] > panel_right)
                        panel_right = strut[1];
                    if (strut[2] > panel_top)
                        panel_top = strut[2];
                    if (strut[3] > panel_bottom)
                        panel_bottom = strut[3];
                    XFree (strut);
                }
                else if (strut)
                {
                    XFree (strut);
                }
            }
            XFree (clients_data);
        }

        // If Struts found reserved space, use it (intersect with current bounds if valid,
        // else replace)
        if (panel_top > 0 || panel_bottom > 0 || panel_left > 0 || panel_right > 0)
        {
            // Struts are reserved space logic.
            int new_x = panel_left;
            int new_y = panel_top;
            int new_w = sw - panel_left - panel_right;
            int new_h = sh - panel_top - panel_bottom;

            // Prefer the "smaller" area (most restrictive)
            if (new_x > bounds->x)
                bounds->x = new_x;
            if (new_y > bounds->y)
                bounds->y = new_y;
            if (new_w < bounds->width)
                bounds->width = new_w;
            if (new_h < bounds->height)
                bounds->height = new_h;
        }
    }

    return GF_SUCCESS;
}
