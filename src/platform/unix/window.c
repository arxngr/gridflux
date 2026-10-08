#include "../../core/layout.h"
#include "../../utils/logger.h"
#include "../../utils/memory.h"
#include "atoms.h"
#include "core/types.h"
#include "internal.h"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// Shrink the over-allocated window list to its final size, or free it if empty.
static gf_err_t
finalize_window_list (gf_win_info_t *list, uint32_t count, gf_win_info_t **out)
{
    if (count == 0)
    {
        gf_free (list);
        *out = NULL;
        return GF_SUCCESS;
    }

    *out = gf_realloc (list, count * sizeof (gf_win_info_t));
    if (!*out)
    {
        gf_free (list);
        return GF_ERROR_MEMORY_ALLOCATION;
    }
    return GF_SUCCESS;
}

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
        if (gf_window_query_window_info (display, window_list[i], atoms, workspace_id,
                                         &info))
        {
            filtered_windows[filtered_count++] = info;
        }
    }

    XFree (data);

    gf_err_t fin = finalize_window_list (filtered_windows, filtered_count, windows);
    if (fin != GF_SUCCESS)
        return fin;

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

    // XGetWindowAttributes reports parent-relative x/y; translate to root
    // coordinates so callers get absolute geometry.
    Window root = DefaultRootWindow (display);
    int abs_x, abs_y;
    Window child;
    if (!XTranslateCoordinates (display, window, root, 0, 0, &abs_x, &abs_y, &child))
        return GF_ERROR_PLATFORM_ERROR;

    geometry->x = abs_x;
    geometry->y = abs_y;
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
gf_window_is_border_excluded (gf_display_t display, gf_handle_t window)
{
    // The GUI and its dialogs/popups share the app's WM_CLASS, so gf_window_is_self
    // already excludes them — the border is clipped around them, not drawn over.
    if (gf_window_is_self (display, window))
        return true;

    if (gf_window_is_app_exception (display, window))
        return true;

    if (gf_window_has_excluded_state (display, window))
        return true;
    if (gf_window_has_excluded_type (display, window))
        return true;

    return false;
}

bool
gf_window_is_self (gf_display_t display, gf_handle_t window)
{
    if (!display || window == None)
        return false;

    // Check res_name/res_class individually (not via gf_window_get_class,
    // which concatenates both for rule matching) so this exact-identity
    // check can't be thrown off by whatever the other half contains.
    XClassHint class_hint = { NULL, NULL };
    if (!XGetClassHint (display, window, &class_hint))
        return false;

    bool match
        = (class_hint.res_name && strcmp (class_hint.res_name, "gridflux-gui") == 0)
          || (class_hint.res_class
              && strstr (class_hint.res_class, "com.gridflux.gui") != NULL);

    if (class_hint.res_name)
        XFree (class_hint.res_name);
    if (class_hint.res_class)
        XFree (class_hint.res_class);

    return match;
}

bool
gf_window_is_excluded (gf_display_t display, gf_handle_t window)
{
    if (!display || window == None)
        return true;

    if (gf_window_is_self (display, window))
        return true;

    if (gf_window_is_app_exception (display, window))
        return true;

    // Exclude fullscreen OR maximized NORMAL windows
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    bool is_fullscreen
        = gf_platform_window_has_state (display, window, atoms->net_wm_state_fullscreen);

    if (gf_window_has_type (display, window, atoms->net_wm_window_type_normal)
        && (is_fullscreen))
    {
        return true;
    }

    if (gf_window_has_excluded_state (display, window))
        return true;

    if (gf_window_has_excluded_type (display, window))
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

// Adjust the target rect for window frame extents so the visible content lands on
// the grid cell: CSD windows expand (shadows hang outside), SSD windows shrink
// (client fits inside the WM frame).
static void
adjust_rect_for_frame (gf_display_t dpy, gf_handle_t win, gf_rect_t *rect)
{
    int left = 0, right = 0, top = 0, bottom = 0;
    bool is_csd = false;

    if (gf_platform_get_frame_extents (dpy, win, &left, &right, &top, &bottom, &is_csd)
        != GF_SUCCESS)
        return;

    if (is_csd)
    {
        rect->x -= left;
        rect->y -= top;
        rect->width += (left + right);
        rect->height += (top + bottom);
    }
    else
    {
        rect->x += left;
        rect->y += top;
        rect->width -= (left + right);
        rect->height -= (top + bottom);
    }
}

gf_err_t
gf_window_set_geometry (gf_display_t dpy, gf_handle_t win, const gf_rect_t *geometry,
                        gf_geom_flags_t flags, gf_config_t *cfg)
{
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();

    if (!dpy || !geometry)
        return GF_ERROR_INVALID_PARAMETER;

    if (gf_window_remove_size_constraints (dpy, win) != GF_SUCCESS)
    {
        GF_LOG_WARN ("Failed to remove size constraints, continuing anyway");
    }

    gf_rect_t rect = *geometry;

    if (flags & GF_GEOMETRY_APPLY_PADDING)
        gf_rect_apply_padding (&rect, GF_DEFAULT_PADDING);

    adjust_rect_for_frame (dpy, win, &rect);

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

gf_err_t
gf_window_restore_monitor (gf_platform_t *platform, const gf_win_info_t *window,
                           const gf_rect_t *previous_bounds)
{
    if (!platform || !platform->platform_data || !window || !previous_bounds)
        return GF_ERROR_INVALID_PARAMETER;
    gf_linux_platform_data_t *data = platform->platform_data;
    if (window->monitor_id >= data->enumerated_monitor_count)
        return GF_ERROR_DISPLAY_CONNECTION;
    gf_rect_t bounds = data->monitors[window->monitor_id].bounds;
    if (!bounds.width || !bounds.height)
        return GF_ERROR_DISPLAY_CONNECTION;
    gf_rect_t target = window->geometry;
    target.x += bounds.x - previous_bounds->x;
    target.y += bounds.y - previous_bounds->y;
    if (!target.width || target.width > bounds.width)
        target.width = bounds.width;
    if (!target.height || target.height > bounds.height)
        target.height = bounds.height;
    if (target.x < bounds.x)
        target.x = bounds.x;
    if (target.y < bounds.y)
        target.y = bounds.y;
    if (target.x + (int32_t)target.width > bounds.x + (int32_t)bounds.width)
        target.x = bounds.x + (int32_t)(bounds.width - target.width);
    if (target.y + (int32_t)target.height > bounds.y + (int32_t)bounds.height)
        target.y = bounds.y + (int32_t)(bounds.height - target.height);
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    bool maximized = window->is_maximized && !window->is_minimized;
    if (!atoms)
        return GF_ERROR_PLATFORM_ERROR;
    long mode[5] = { 0, atoms->net_wm_state_maximized_vert,
                     atoms->net_wm_state_maximized_horz, 2, 0 };
    if (maximized)
    {
        gf_err_t result = gf_platform_send_client_message (data->display, window->id,
                                                           atoms->net_wm_state, mode, 5);
        if (result != GF_SUCCESS)
            return result;
    }
    gf_err_t result = gf_window_set_geometry (data->display, window->id, &target,
                                              GF_GEOMETRY_CHANGE_ALL, NULL);
    if (maximized)
    {
        mode[0] = 1;
        gf_err_t restored = gf_platform_send_client_message (
            data->display, window->id, atoms->net_wm_state, mode, 5);
        if (result == GF_SUCCESS)
            result = restored;
    }
    return result;
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

    // Restoring sibling tiles must not activate them and move keyboard focus
    // to another monitor. Only map windows that are actually iconified.
    if (attr.map_state == IsViewable && !gf_window_is_minimized (display, window))
        return GF_SUCCESS;
    XMapWindow (display, window);
    XSync (display, False);

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    if (!atoms)
        return GF_ERROR_PLATFORM_ERROR;

    if (atoms->net_wm_state != None && atoms->net_wm_state_hidden != None)
    {
        long data[5] = { 0, // _NET_WM_STATE_REMOVE
                         atoms->net_wm_state_hidden, 0, 0, 0 };

        gf_platform_send_client_message (display, window, atoms->net_wm_state, data, 5);
    }

    XFlush (display);

    return GF_SUCCESS;
}

gf_err_t
gf_window_focus (gf_display_t display, gf_handle_t window)
{
    if (!display || !gf_window_is_valid (display, window))
        return GF_ERROR_INVALID_PARAMETER;
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    if (atoms && atoms->net_active_window != None)
    {
        long data[5] = { 2, CurrentTime, 0, 0, 0 };
        gf_platform_send_client_message (display, window, atoms->net_active_window, data,
                                         5);
    }
    XWindowAttributes attr;
    if (XGetWindowAttributes (display, window, &attr) && attr.map_state == IsViewable)
        XSetInputFocus (display, window, RevertToPointerRoot, CurrentTime);
    XFlush (display);
    return GF_SUCCESS;
}

gf_err_t
gf_window_set_maximized (gf_display_t display, gf_handle_t window, bool maximized)
{
    if (!display || !gf_window_is_valid (display, window))
        return GF_ERROR_INVALID_PARAMETER;
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    if (!atoms || atoms->net_wm_state == None)
        return GF_ERROR_PLATFORM_ERROR;
    bool native_maximized = gf_window_is_maximized (display, window);
    if (!maximized && !native_maximized)
        return GF_SUCCESS;
    gf_rect_t destination;
    if (gf_window_get_geometry (display, window, &destination) != GF_SUCCESS)
        return GF_ERROR_PLATFORM_ERROR;
    long data[5] = { 0, atoms->net_wm_state_maximized_vert,
                     atoms->net_wm_state_maximized_horz, 2, 0 };
    gf_err_t result = GF_SUCCESS;
    if (native_maximized)
    {
        result = gf_platform_send_client_message (display, window, atoms->net_wm_state,
                                                  data, 5);
        if (result != GF_SUCCESS)
            return result;
        // Removing maximize can restore a saved position on the old monitor.
        // Move back to the captured destination before applying its new mode.
        long position[5] = { StaticGravity | (1 << 8) | (1 << 9) | (2 << 12),
                             destination.x, destination.y, 0, 0 };
        result = gf_platform_send_client_message (
            display, window, atoms->net_moveresize_window, position, 5);
        if (result != GF_SUCCESS)
            return result;
    }
    if (maximized)
    {
        data[0] = 1;
        result = gf_platform_send_client_message (display, window, atoms->net_wm_state,
                                                  data, 5);
    }
    XFlush (display);
    return result;
}

void
gf_window_get_class (gf_display_t dpy, gf_handle_t win, char *buffer, size_t bufsize)
{
    if (!dpy || win == None || !buffer || bufsize == 0)
        return;

    buffer[0] = '\0';

    XClassHint class_hint = { NULL, NULL };
    if (!XGetClassHint (dpy, win, &class_hint))
        return;

    // WM_CLASS has two parts (instance, class); which one carries the
    // identifier a rule targets varies by app (e.g. GTK apps commonly leave
    // the reverse-DNS id, like "org.gnome.Nautilus", only in res_class).
    // Concatenate both so rule matching can find either.
    const char *name = class_hint.res_name ? class_hint.res_name : "";
    const char *cls = class_hint.res_class ? class_hint.res_class : "";
    const char *sep = (name[0] && cls[0]) ? " " : "";
    snprintf (buffer, bufsize, "%s%s%s", name, sep, cls);

    if (class_hint.res_name)
        XFree (class_hint.res_name);
    if (class_hint.res_class)
        XFree (class_hint.res_class);
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
