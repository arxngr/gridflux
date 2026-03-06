#include "../../utils/logger.h"
#include "internal.h"
#include <X11/Xatom.h>

static void
_gf_get_global_struts (Display *dpy, Window root, gf_platform_atoms_t *atoms,
                       int *panel_left, int *panel_right, int *panel_top,
                       int *panel_bottom)
{
    unsigned char *clients_data = NULL;
    unsigned long clients_count = 0;
    Atom actual_type;
    int actual_format;
    unsigned long bytes_after;

    *panel_left = *panel_right = *panel_top = *panel_bottom = 0;

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
                if (strut[0] > *panel_left)
                    *panel_left = strut[0];
                if (strut[1] > *panel_right)
                    *panel_right = strut[1];
                if (strut[2] > *panel_top)
                    *panel_top = strut[2];
                if (strut[3] > *panel_bottom)
                    *panel_bottom = strut[3];
                XFree (strut);
            }
            else if (strut)
            {
                XFree (strut);
            }
        }
        XFree (clients_data);
    }
}


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
        _gf_get_global_struts(dpy, root, atoms, &panel_left, &panel_right, &panel_top, &panel_bottom);

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

uint32_t
gf_monitor_get_count (gf_platform_t *platform)
{
    if (!platform || !platform->platform_data)
        return 1;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    Display *dpy = data->display;

    int event_base, error_base;
    if (XineramaQueryExtension (dpy, &event_base, &error_base) && XineramaIsActive (dpy))
    {
        int count = 0;
        XineramaScreenInfo *screens = XineramaQueryScreens (dpy, &count);
        if (screens)
        {
            XFree (screens);
            return (uint32_t)count;
        }
    }

    return 1;
}

gf_err_t
gf_monitor_enumerate (gf_platform_t *platform, gf_monitor_t *monitors, uint32_t *count)
{
    if (!platform || !monitors || !count)
        return GF_ERROR_INVALID_PARAMETER;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    Display *dpy = data->display;

    int event_base, error_base;
    if (XineramaQueryExtension (dpy, &event_base, &error_base) && XineramaIsActive (dpy))
    {
        int screen_count = 0;
        XineramaScreenInfo *screens = XineramaQueryScreens (dpy, &screen_count);

        if (screens)
        {
            uint32_t n
                = (*count < (uint32_t)screen_count) ? *count : (uint32_t)screen_count;
            for (uint32_t i = 0; i < n; i++)
            {
                monitors[i].id = screens[i].screen_number;
                monitors[i].bounds.x = screens[i].x_org;
                monitors[i].bounds.y = screens[i].y_org;
                monitors[i].bounds.width = screens[i].width;
                monitors[i].bounds.height = screens[i].height;
                monitors[i].full_bounds = monitors[i].bounds;
                monitors[i].is_primary = (i == 0); // Simplification: first is primary

                if (i < GF_MAX_MONITORS)
                    data->monitors[i] = monitors[i];
            }
            *count = n;
            data->enumerated_monitor_count = n;
            XFree (screens);
            return GF_SUCCESS;
        }
    }

    // Fallback: single monitor
    *count = 1;
    monitors[0].id = 0;
    monitors[0].is_primary = true;
    gf_screen_get_bounds (dpy, &monitors[0].bounds);
    monitors[0].full_bounds = monitors[0].bounds;

    data->monitors[0] = monitors[0];
    data->enumerated_monitor_count = 1;

    return GF_SUCCESS;
}

gf_monitor_id_t
gf_monitor_from_window (gf_platform_t *platform, gf_handle_t window)
{
    if (!platform || !window)
        return 0;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    Display *dpy = data->display;

    XWindowAttributes attrs;
    if (XGetWindowAttributes (dpy, (Window)window, &attrs))
    {
        int x, y;
        Window child;
        XTranslateCoordinates (dpy, (Window)window, DefaultRootWindow (dpy), 0, 0, &x, &y,
                               &child);

        // Center point check
        int cx = x + attrs.width / 2;
        int cy = y + attrs.height / 2;

        for (uint32_t i = 0; i < data->enumerated_monitor_count; i++)
        {
            gf_rect_t *b = &data->monitors[i].full_bounds;
            if (cx >= b->x && cx < b->x + b->width && cy >= b->y && cy < b->y + b->height)
            {
                return data->monitors[i].id;
            }
        }
    }

    return 0;
}

gf_err_t
gf_screen_get_bounds_for_monitor (gf_display_t display, gf_monitor_id_t monitor_id,
                                  gf_rect_t *bounds)
{
    if (!display || !bounds)
        return GF_ERROR_INVALID_PARAMETER;

    int screen_count = 0;
    XineramaScreenInfo *screens = XineramaQueryScreens (display, &screen_count);
    if (screens)
    {
        for (int i = 0; i < screen_count; i++)
        {
            if (screens[i].screen_number == (int)monitor_id)
            {
                bounds->x = screens[i].x_org;
                bounds->y = screens[i].y_org;
                bounds->width = screens[i].width;
                bounds->height = screens[i].height;
                XFree (screens);

                // Apply struts to the physical monitor bounds
                int screen = DefaultScreen (display);
                Window root = DefaultRootWindow (display);
                Screen *scr = ScreenOfDisplay (display, screen);
                int sw = scr->width;
                int sh = scr->height;

                int panel_left = 0, panel_right = 0, panel_top = 0, panel_bottom = 0;
                gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
                _gf_get_global_struts(display, root, atoms, &panel_left, &panel_right, &panel_top, &panel_bottom);

                if (panel_top > 0 || panel_bottom > 0 || panel_left > 0 || panel_right > 0)
                {
                    int new_x = panel_left;
                    int new_y = panel_top;
                    int new_w = sw - panel_left - panel_right;
                    int new_h = sh - panel_top - panel_bottom;

                    // Prefer the "smaller" area (most restrictive)
                    // Intersect monitor physical bounds with the strut-reduced global bounds
                    if (new_x > bounds->x)
                    {
                        bounds->width -= (new_x - bounds->x);
                        bounds->x = new_x;
                    }
                    if (new_y > bounds->y)
                    {
                        bounds->height -= (new_y - bounds->y);
                        bounds->y = new_y;
                    }
                    if (bounds->x + bounds->width > new_x + new_w)
                    {
                        bounds->width = (new_x + new_w) - bounds->x;
                    }
                    if (bounds->y + bounds->height > new_y + new_h)
                    {
                        bounds->height = (new_y + new_h) - bounds->y;
                    }
                }

                return GF_SUCCESS;
            }
        }
        XFree (screens);
    }

    return gf_screen_get_bounds (display, bounds);
}
