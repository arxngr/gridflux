#include "../../core/layout.h"
#include "../../utils/logger.h"
#include "../../utils/memory.h"
#include "atoms.h"
#include "core/types.h"
#include "internal.h"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <string.h>
#include <time.h>

gf_err_t
gf_platform_get_windows (gf_display_t display, gf_ws_id_t *workspace_id,
                         gf_win_info_t **windows, uint32_t *count)
{
    if (!display || !windows || !count)
        return GF_ERROR_INVALID_PARAMETER;

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    Window root = DefaultRootWindow (display);

    unsigned char *data = NULL;
    unsigned long nitems = 0;

    gf_err_t result = gf_platform_get_window_property (
        display, root, atoms->net_client_list, XA_WINDOW, &data, &nitems);
    if (result != GF_SUCCESS)
    {
        *windows = NULL;
        *count = 0;
        return GF_SUCCESS; // No windows is not an error
    }

    Window *window_list = (Window *)data;
    gf_win_info_t *filtered_windows = gf_malloc (nitems * sizeof (gf_win_info_t));
    if (!filtered_windows)
    {
        XFree (data);
        return GF_ERROR_MEMORY_ALLOCATION;
    }

    uint32_t filtered_count = 0;

    for (unsigned long i = 0; i < nitems; i++)
    {
        gf_win_info_t info;
        if (_process_window_for_list (display, window_list[i], atoms, workspace_id,
                                      &info))
        {
            filtered_windows[filtered_count++] = info;
        }
    }

    XFree (data);

    if (filtered_count == 0)
    {
        gf_free (filtered_windows);
        *windows = NULL;
    }
    else
    {
        // Resize to actual count
        *windows = gf_realloc (filtered_windows, filtered_count * sizeof (gf_win_info_t));
        if (!*windows)
        {
            gf_free (filtered_windows);
            return GF_ERROR_MEMORY_ALLOCATION;
        }
    }

    *count = filtered_count;
    return GF_SUCCESS;
}

gf_err_t
gf_window_get_geometry (gf_display_t display, gf_handle_t window, gf_rect_t *geometry)
{
    if (!display || !geometry)
        return GF_ERROR_INVALID_PARAMETER;

    XWindowAttributes attrs;
    if (!XGetWindowAttributes (display, window, &attrs))
    {
        return GF_ERROR_PLATFORM_ERROR;
    }

    geometry->x = attrs.x;
    geometry->y = attrs.y;
    geometry->width = attrs.width;
    geometry->height = attrs.height;

    return GF_SUCCESS;
}

bool
gf_window_is_valid (gf_display_t display, gf_handle_t window)
{
    if (!display)
        return false;

    XWindowAttributes attrs;
    return XGetWindowAttributes (display, window, &attrs) != 0;
}

bool
_window_excluded_border (gf_display_t display, gf_handle_t window)
{
    if (_window_it_self (display, window))
        return true;

    if (_window_screenshot_app (display, window))
        return true;

    if (_window_has_excluded_state (display, window))
        return true;

    return false;
}

bool
_window_it_self (gf_display_t display, gf_handle_t window)
{
    if (!display || window == None)
        return false;

    char class_name[256] = { 0 };
    gf_window_get_class (display, window, class_name, sizeof (class_name));

    if (strcmp (class_name, "gridflux-gui") == 0
        || strstr (class_name, "com.gridflux.gui") != NULL)
    {
        return true;
    }

    return false;
}

bool
gf_window_is_excluded (gf_display_t display, gf_handle_t window)
{
    if (!display || window == None)
        return true;

    if (_window_it_self (display, window))
        return true;

    if (_window_screenshot_app (display, window))
        return true;

    /* Exclude fullscreen OR maximized NORMAL windows */
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    bool is_fullscreen
        = gf_platform_window_has_state (display, window, atoms->net_wm_state_fullscreen);

    if (_window_has_type (display, window, atoms->net_wm_window_type_normal)
        && (is_fullscreen))
    {
        return true;
    }

    if (_window_has_excluded_state (display, window))
        return true;

    if (_window_has_excluded_type (display, window))
        return true;

    return false;
}

bool
gf_window_is_fullscreen (gf_display_t display, gf_handle_t window)
{
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    return gf_platform_window_has_state (display, (Window)window,
                                         atoms->net_wm_state_fullscreen);
}

gf_err_t
gf_window_set_geometry (gf_display_t dpy, gf_handle_t win, const gf_rect_t *geometry,
                        gf_geom_flags_t flags, gf_config_t *cfg)
{
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();

    if (!dpy || !geometry)
        return GF_ERROR_INVALID_PARAMETER;

    if (_remove_size_constraints (dpy, win) != GF_SUCCESS)
    {
        GF_LOG_WARN ("Failed to remove size constraints, continuing anyway");
    }

    gf_rect_t rect = *geometry;

    if (flags & GF_GEOMETRY_APPLY_PADDING)
        gf_rect_apply_padding (&rect, cfg->default_padding);

    // Calculate Frame Extents to correctly position the Client window
    int left = 0, right = 0, top = 0, bottom = 0;
    bool is_csd = false;

    // We try to get frame extents. If we have them, we must adjust our target position.
    if (gf_platform_get_frame_extents (dpy, win, &left, &right, &top, &bottom, &is_csd)
        == GF_SUCCESS)
    {
        if (is_csd)
        {
            // Client Side Decorations (GTK, etc.)
            // The X Window includes the shadows/borders defined by extents.
            // To make the VISUAL content match the grid 'rect', we must EXPAND the X
            // Window. so that the shadows hang 'outside' the grid cell.
            rect.x -= left;
            rect.y -= top;
            rect.width += (left + right);
            rect.height += (top + bottom);
        }
        else
        {
            // Server Side Decorations (Standard X11)
            // The X Window is just the content. The WM adds the frame.
            // The grid 'rect' includes the frame.
            // So we must SHRINK the Client X Window so it fits inside the frame.
            rect.x += left;
            rect.y += top;
            rect.width -= (left + right);
            rect.height -= (top + bottom);
        }
    }

    // Use StaticGravity (10) to force the WM to place the client at exactly x, y
    // This removes ambiguity about how NorthWestGravity is interpreted relative to
    // frames.
    long data[5];

    data[0] = (10) |      // gravity = StaticGravity (10)
              (1 << 8) |  // set x
              (1 << 9) |  // set y
              (1 << 10) | // set width
              (1 << 11);  // set height

    data[1] = rect.x;
    data[2] = rect.y;
    data[3] = rect.width;
    data[4] = rect.height;

    return gf_platform_send_client_message (dpy, win, atoms->net_moveresize_window, data,
                                            5);
}

gf_handle_t
gf_window_get_focused (Display *dpy)
{
    if (!dpy)
        return None;

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    Atom actual;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    Window win = None;

    if (XGetWindowProperty (dpy, DefaultRootWindow (dpy), atoms->net_active_window, 0, 1,
                            False, XA_WINDOW, &actual, &format, &nitems, &bytes_after,
                            &data)
            == Success
        && data && nitems > 0)
    {
        win = *(Window *)data;
        XFree (data);
    }

    if (win == None)
        return None;

    XWindowAttributes attr;
    if (XGetWindowAttributes (dpy, win, &attr) == 0)
    {
        GF_LOG_DEBUG ("Focused window %lu is invalid", win);
        return None;
    }

    if (attr.map_state != IsViewable)
    {
        GF_LOG_DEBUG ("Focused window %lu is not viewable (map_state=%d)", win,
                      attr.map_state);
        return None;
    }

    return win;
}

gf_err_t
gf_window_minimize (gf_display_t display, gf_handle_t window)
{
    if (!display || window == None)
        return GF_ERROR_INVALID_PARAMETER;

    // Verify window exists
    XWindowAttributes attr;
    if (XGetWindowAttributes (display, window, &attr) == 0)
    {
        GF_LOG_WARN ("Cannot minimize invalid window: %lu", window);
        return GF_ERROR_PLATFORM_ERROR;
    }

    if (XIconifyWindow (display, window, DefaultScreen (display)) == 0)
        return GF_ERROR_PLATFORM_ERROR;

    XFlush (display);
    return GF_SUCCESS;
}

gf_err_t
gf_window_unminimize (gf_display_t display, gf_handle_t window)
{
    if (!display || window == None)
        return GF_ERROR_INVALID_PARAMETER;

    XWindowAttributes attr;
    if (XGetWindowAttributes (display, window, &attr) == 0)
    {
        GF_LOG_WARN ("Cannot unminimize invalid window: %lu", window);
        return GF_ERROR_PLATFORM_ERROR;
    }

    /* Map the window first — XIconifyWindow unmaps it, so we need to
     * re-map before any focus requests can succeed. */
    XMapRaised (display, window);

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    if (!atoms)
        return GF_ERROR_PLATFORM_ERROR;

    if (atoms->net_wm_state != None && atoms->net_wm_state_hidden != None)
    {
        long data[5] = { 0, /* _NET_WM_STATE_REMOVE */
                         atoms->net_wm_state_hidden, 0, 0, 0 };

        gf_platform_send_client_message (display, window, atoms->net_wm_state, data, 5);
    }

    if (atoms->net_active_window != None)
    {
        long data[5] = { 2, /* source: pager/task-switcher (authoritative) */
                         CurrentTime, 0, 0, 0 };

        gf_platform_send_client_message (display, window, atoms->net_active_window, data,
                                         5);
    }

    /* Force focus transfer — _NET_ACTIVE_WINDOW is advisory, this is
     * required so gf_wm_event sees the correct focused window. */
    XSetInputFocus (display, window, RevertToPointerRoot, CurrentTime);
    XFlush (display);

    return GF_SUCCESS;
}

void
gf_window_get_class (gf_display_t dpy, gf_handle_t win, char *buffer, size_t bufsize)
{
    if (!dpy || win == None || !buffer || bufsize == 0)
        return;

    buffer[0] = '\0';

    XClassHint class_hint;
    if (XGetClassHint (dpy, win, &class_hint))
    {
        if (class_hint.res_name)
        {
            strncpy (buffer, class_hint.res_name, bufsize - 1);
            buffer[bufsize - 1] = '\0';
            XFree (class_hint.res_name);
        }
        if (class_hint.res_class)
        {
            XFree (class_hint.res_class);
        }
    }
}

bool
gf_window_is_minimized (gf_display_t display, gf_handle_t window)
{
    if (!display || window == None)
        return false;

    if (!gf_window_is_valid (display, window))
        return false;

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    if (!atoms)
        return false;

    return gf_platform_window_has_state (display, window, atoms->net_wm_state_hidden);
}

bool
gf_window_is_maximized (gf_display_t display, gf_handle_t window)
{
    if (!display || window == None)
        return false;

    if (!gf_window_is_valid (display, window))
        return false;

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    if (!atoms)
        return false;

    return gf_platform_window_has_state (display, window,
                                         atoms->net_wm_state_maximized_vert)
           && gf_platform_window_has_state (display, window,
                                            atoms->net_wm_state_maximized_horz);
}
