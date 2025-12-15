#include "../../core/types.h"
#include "x11_atoms.h"
#include "x11_window_manager.h"
#include <X11/X.h>
#include <X11/Xatom.h>
#include <platform/x11/x11_backend.h>

gf_error_code_t
gf_x11_kde_get_screen_bounds (gf_display_t dpy, gf_rect_t *bounds)
{
    if (!dpy || !bounds)
        return GF_ERROR_INVALID_PARAMETER;

    int screen = DefaultScreen (dpy);
    Window root = DefaultRootWindow (dpy);
    Screen *scr = ScreenOfDisplay (dpy, screen);

    int sw = scr->width;
    int sh = scr->height;

    gf_x11_atoms_t *atoms = gf_x11_atoms_get_global ();

    int panel_left = 0, panel_right = 0, panel_top = 0, panel_bottom = 0;
    Atom type;
    int format;
    unsigned long count, bytes_after;
    Window *clients = NULL;

    if (XGetWindowProperty (dpy, root, atoms->net_client_list, 0, 2048, False, XA_WINDOW,
                            &type, &format, &count, &bytes_after,
                            (unsigned char **)&clients)
            == Success
        && clients)
    {
        for (unsigned long i = 0; i < count; i++)
        {
            long *strut = NULL;
            unsigned long nitems = 0;

            if (gf_x11_get_window_property (dpy, clients[i], atoms->net_strut_partial,
                                            XA_CARDINAL, (unsigned char **)&strut,
                                            &nitems)
                    == GF_SUCCESS
                && strut && nitems >= 12)
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
        }
        XFree (clients);
    }

    bounds->x = panel_left;
    bounds->y = panel_top;
    bounds->width = sw - panel_left - panel_right;
    bounds->height = sh - panel_top - panel_bottom;
    return GF_SUCCESS;
}

gf_error_code_t
gf_x11_kde_set_window_geometry (gf_display_t dpy, gf_native_window_t win,
                                const gf_rect_t *geometry, gf_geometry_flags_t flags,
                                gf_config_t *cfg)
{
    gf_x11_atoms_t *atoms = gf_x11_atoms_get_global ();

    if (!dpy || !geometry)
        return GF_ERROR_INVALID_PARAMETER;

    gf_rect_t rect = *geometry;

    if (flags & GF_GEOMETRY_APPLY_PADDING)
        gf_rect_apply_padding (&rect, cfg->default_padding);

    if (rect.width < GF_MIN_WINDOW_SIZE)
        rect.width = GF_MIN_WINDOW_SIZE;
    if (rect.height < GF_MIN_WINDOW_SIZE)
        rect.height = GF_MIN_WINDOW_SIZE;

    int left = 0, right = 0, top = 0, bottom = 0;
    gf_x11_get_frame_extents (dpy, win, &left, &right, &top, &bottom);

    // Just adjust the size to be CLIENT size (KDE will add frame decorations)
    rect.width -= (left + right);
    rect.height -= (top + bottom);

    // Simple bounds checking - just ensure we don't go negative or too large
    if (rect.width < GF_MIN_WINDOW_SIZE)
        rect.width = GF_MIN_WINDOW_SIZE;
    if (rect.height < GF_MIN_WINDOW_SIZE)
        rect.height = GF_MIN_WINDOW_SIZE;

    long data[5];

    // Use NorthWestGravity so KDE positions the FRAME at rect.x, rect.y
    // not the client area
    data[0] = NorthWestGravity | // gravity = NorthWestGravity
              (1 << 8) |         // set x
              (1 << 9) |         // set y
              (1 << 10) |        // set width
              (1 << 11);         // set height

    data[1] = rect.x;
    data[2] = rect.y;
    data[3] = rect.width;
    data[4] = rect.height;

    return gf_x11_send_client_message (dpy, win, atoms->net_moveresize_window, data, 5);
}
