#include "../../utils/logger.h"
#include "internal.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <unistd.h>

// Merge a strut array {left, right, top, bottom} into the running maximums.
static void
_strut_apply_max (const long *strut, int *panel_left, int *panel_right, int *panel_top,
                  int *panel_bottom)
{
    if (strut[0] > *panel_left)
        *panel_left = strut[0];
    if (strut[1] > *panel_right)
        *panel_right = strut[1];
    if (strut[2] > *panel_top)
        *panel_top = strut[2];
    if (strut[3] > *panel_bottom)
        *panel_bottom = strut[3];
}

// Merge one client's reserved-space struts into the running maximums, trying
// _NET_WM_STRUT_PARTIAL first and falling back to legacy _NET_WM_STRUT.
static void
_accumulate_client_strut (Display *dpy, Window client, gf_platform_atoms_t *atoms,
                          int *panel_left, int *panel_right, int *panel_top,
                          int *panel_bottom)
{
    long *strut = NULL;
    unsigned long nitems_strut = 0;

    if (gf_platform_get_window_property (dpy, client, atoms->net_wm_strut_partial,
                                         XA_CARDINAL, (unsigned char **)&strut,
                                         &nitems_strut)
            == GF_SUCCESS
        && strut && nitems_strut >= 12)
    {
        _strut_apply_max (strut, panel_left, panel_right, panel_top, panel_bottom);
        XFree (strut);
        return;
    }

    if (strut)
    {
        XFree (strut);
        strut = NULL;
    }

    // Fallback to legacy _NET_WM_STRUT
    if (gf_platform_get_window_property (dpy, client, atoms->net_wm_strut, XA_CARDINAL,
                                         (unsigned char **)&strut, &nitems_strut)
            == GF_SUCCESS
        && strut && nitems_strut >= 4)
    {
        _strut_apply_max (strut, panel_left, panel_right, panel_top, panel_bottom);
        XFree (strut);
    }
    else if (strut)
    {
        XFree (strut);
    }
}

static void
_get_global_struts (Display *dpy, Window root, gf_platform_atoms_t *atoms,
                    int *panel_left, int *panel_right, int *panel_top, int *panel_bottom)
{
    unsigned char *clients_data = NULL;
    unsigned long clients_count = 0;
    Atom actual_type;
    int actual_format;
    unsigned long bytes_after;

    *panel_left = *panel_right = *panel_top = *panel_bottom = 0;

    if (XGetWindowProperty (dpy, root, atoms->net_client_list, 0, 4096, False, XA_WINDOW,
                            &actual_type, &actual_format, &clients_count, &bytes_after,
                            &clients_data)
            != Success
        || !clients_data)
        return;

    Window *clients = (Window *)clients_data;
    for (unsigned long i = 0; i < clients_count; i++)
        _accumulate_client_strut (dpy, clients[i], atoms, panel_left, panel_right,
                                  panel_top, panel_bottom);

    XFree (clients_data);
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

// Fill bounds from _NET_WORKAREA for the current workspace. Returns true if a
// sane (non-zero, sub-screen) work area was found.
static bool
_workarea_get_bounds (gf_display_t dpy, Window root, gf_platform_atoms_t *atoms, int sw,
                      int sh, gf_rect_t *bounds)
{
    unsigned char *data = NULL;
    unsigned long nitems = 0;
    bool valid = false;

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
                valid = true;
            }
        }
    }

    if (data)
        XFree (data);
    return valid;
}

// Shrink bounds by the panel struts, keeping the most restrictive edge.
static void
_struts_clip_bounds (gf_display_t dpy, Window root, gf_platform_atoms_t *atoms, int sw,
                     int sh, gf_rect_t *bounds)
{
    int panel_left = 0, panel_right = 0, panel_top = 0, panel_bottom = 0;
    _get_global_struts (dpy, root, atoms, &panel_left, &panel_right, &panel_top,
                        &panel_bottom);

    if (!(panel_top > 0 || panel_bottom > 0 || panel_left > 0 || panel_right > 0))
        return;

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

gf_err_t
gf_screen_get_bounds (gf_display_t dpy, gf_rect_t *bounds)
{
    if (!dpy || !bounds)
        return GF_ERROR_INVALID_PARAMETER;

    XSync (dpy, False);
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

    bool workarea_valid = _workarea_get_bounds (dpy, root, atoms, sw, sh, bounds);

    // If Workarea gave full screen (or failed), try Struts to be safe
    if (!workarea_valid
        || (bounds->x == 0 && bounds->y == 0 && bounds->width == sw
            && bounds->height == sh))
    {
        _struts_clip_bounds (dpy, root, atoms, sw, sh, bounds);
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

// Fill bounds with the physical geometry of the given Xinerama monitor.
// Returns false if that monitor id was not found.
static bool
_xinerama_monitor_bounds (gf_display_t display, gf_monitor_id_t monitor_id,
                          gf_rect_t *bounds)
{
    int screen_count = 0;
    XineramaScreenInfo *screens = XineramaQueryScreens (display, &screen_count);
    if (!screens)
        return false;

    bool found = false;
    for (int i = 0; i < screen_count; i++)
    {
        if (screens[i].screen_number == (int)monitor_id)
        {
            bounds->x = screens[i].x_org;
            bounds->y = screens[i].y_org;
            bounds->width = screens[i].width;
            bounds->height = screens[i].height;
            found = true;
            break;
        }
    }
    XFree (screens);
    return found;
}

// Determine the global "safe zone" (work area) for the current workspace,
// defaulting to the whole display when the property is unavailable.
static void
_workarea_safe_zone (gf_display_t display, Window root, gf_platform_atoms_t *atoms,
                     int *safe_x, int *safe_y, int *safe_w, int *safe_h)
{
    *safe_x = 0;
    *safe_y = 0;
    *safe_w = DisplayWidth (display, DefaultScreen (display));
    *safe_h = DisplayHeight (display, DefaultScreen (display));

    unsigned char *data = NULL;
    unsigned long nitems = 0;

    if (gf_platform_get_window_property (display, root, atoms->net_workarea, XA_CARDINAL,
                                         &data, &nitems)
            == GF_SUCCESS
        && data && nitems >= 4)
    {
        // _NET_WORKAREA is an array of 4 longs per workspace
        long *workareas = (long *)data;
        gf_ws_id_t workspace = gf_workspace_get_current (display);
        unsigned long offset = workspace * 4;

        if (offset + 3 < nitems)
        {
            *safe_x = (int)workareas[offset];
            *safe_y = (int)workareas[offset + 1];
            *safe_w = (int)workareas[offset + 2];
            *safe_h = (int)workareas[offset + 3];
        }
    }

    if (data)
        XFree (data);
}

// Intersect the safe zone with the panel struts, clamping dimensions to >= 0.
static void
_struts_shrink_safe_zone (gf_display_t display, Window root, gf_platform_atoms_t *atoms,
                          int *safe_x, int *safe_y, int *safe_w, int *safe_h)
{
    int panel_left = 0, panel_right = 0, panel_top = 0, panel_bottom = 0;
    _get_global_struts (display, root, atoms, &panel_left, &panel_right, &panel_top,
                        &panel_bottom);

    if (!(panel_top > 0 || panel_bottom > 0 || panel_left > 0 || panel_right > 0))
        return;

    int sw = DisplayWidth (display, DefaultScreen (display));
    int sh = DisplayHeight (display, DefaultScreen (display));

    int strut_x = panel_left;
    int strut_y = panel_top;
    int strut_w = sw - panel_left - panel_right;
    int strut_h = sh - panel_top - panel_bottom;

    // Intersect strut area with the currently determined safe area
    if (strut_x > *safe_x)
    {
        *safe_w -= (strut_x - *safe_x);
        *safe_x = strut_x;
    }
    if (strut_y > *safe_y)
    {
        *safe_h -= (strut_y - *safe_y);
        *safe_y = strut_y;
    }
    if (*safe_x + *safe_w > strut_x + strut_w)
        *safe_w = (strut_x + strut_w) - *safe_x;
    if (*safe_y + *safe_h > strut_y + strut_h)
        *safe_h = (strut_y + strut_h) - *safe_y;

    // Ensure we don't get negative dimensions
    if (*safe_w < 0)
        *safe_w = 0;
    if (*safe_h < 0)
        *safe_h = 0;
}

// Clip bounds to its overlap with the safe zone, zeroing size on no overlap.
static void
_clip_bounds_to_safe (gf_rect_t *bounds, int safe_x, int safe_y, int safe_w, int safe_h)
{
    int monitor_right = bounds->x + bounds->width;
    int monitor_bottom = bounds->y + bounds->height;
    int safe_right = safe_x + safe_w;
    int safe_bottom = safe_y + safe_h;

    // Calculate the overlap
    int final_x = (bounds->x > safe_x) ? bounds->x : safe_x;
    int final_y = (bounds->y > safe_y) ? bounds->y : safe_y;
    int final_r = (monitor_right < safe_right) ? monitor_right : safe_right;
    int final_b = (monitor_bottom < safe_bottom) ? monitor_bottom : safe_bottom;

    bounds->x = final_x;
    bounds->y = final_y;
    bounds->width = (final_r > final_x) ? (final_r - final_x) : 0;
    bounds->height = (final_b > final_y) ? (final_b - final_y) : 0;
}

gf_err_t
gf_screen_get_bounds_for_monitor (gf_display_t display, gf_monitor_id_t monitor_id,
                                  gf_rect_t *bounds)
{
    if (!display || !bounds)
        return GF_ERROR_INVALID_PARAMETER;

    // Force X server roundtrip to ensure we see the latest property changes
    // (like _NET_WORKAREA) after the dock visibility changes.
    XSync (display, False);

    // Get physical geometry from Xinerama; fall back to single-screen bounds.
    if (!_xinerama_monitor_bounds (display, monitor_id, bounds))
        return gf_screen_get_bounds (display, bounds);

    Window root = DefaultRootWindow (display);
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();

    int safe_x, safe_y, safe_w, safe_h;
    _workarea_safe_zone (display, root, atoms, &safe_x, &safe_y, &safe_w, &safe_h);

    // Always check Struts to be safe, because GNOME's _NET_WORKAREA can be unreliable
    // especially during or after workspace transitions or dynamic dock visibility
    // changes.
    _struts_shrink_safe_zone (display, root, atoms, &safe_x, &safe_y, &safe_w, &safe_h);

    // Clip the physical monitor against the global safe zone.
    // This is the logic that supports multiple monitors of different sizes.
    _clip_bounds_to_safe (bounds, safe_x, safe_y, safe_w, safe_h);

    return GF_SUCCESS;
}
