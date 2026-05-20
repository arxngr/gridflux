#include "../../utils/logger.h"
#include "../../utils/memory.h"
#include "internal.h"
#include "platform/unix/platform.h"
#include <X11/X.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t g_notification_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile time_t g_notification_expire_time = 0;
static bool g_notification_threads_started = false;

static void *
_dbus_monitor_thread (void *arg)
{
    FILE *fp = popen ("dbus-monitor "
                      "\"type='method_call',interface='org.freedesktop.Notifications',member='Notify'\" "
                      "\"type='signal',interface='org.freedesktop.Notifications',member='NotificationClosed'\" "
                      "\"type='method_call',interface='org.gtk.Notifications',member='AddNotification'\" "
                      "\"type='method_call',interface='org.gtk.Notifications',member='RemoveNotification'\" "
                      "\"type='method_call',interface='org.gnome.Shell.Screenshot'\" 2>/dev/null",
                      "r");
    if (!fp)
        return NULL;

    char line[512];
    while (fgets (line, sizeof (line), fp))
    {
        if (strstr (line, "member=Notify") || strstr (line, "member=AddNotification") || strstr (line, "interface=org.gnome.Shell.Screenshot"))
        {
            pthread_mutex_lock (&g_notification_mutex);
            g_notification_expire_time = time (NULL) + 15; // 15-second fail-safe timeout
            pthread_mutex_unlock (&g_notification_mutex);
        }
        else if (strstr (line, "member=NotificationClosed") || strstr (line, "member=RemoveNotification"))
        {
            pthread_mutex_lock (&g_notification_mutex);
            g_notification_expire_time = 0; // instantly clear and restore borders
            pthread_mutex_unlock (&g_notification_mutex);
        }
    }
    pclose (fp);
    return NULL;
}

static void *
_gsettings_monitor_thread (void *arg)
{
    FILE *fp = popen ("gsettings monitor org.gnome.shell favorite-apps 2>/dev/null", "r");
    if (!fp)
        return NULL;

    char line[512];
    while (fgets (line, sizeof (line), fp))
    {
        pthread_mutex_lock (&g_notification_mutex);
        g_notification_expire_time = time (NULL) + 6; // 6-second timeout for favorite-apps settings sync
        pthread_mutex_unlock (&g_notification_mutex);
    }
    pclose (fp);
    return NULL;
}

static void
_get_pictures_dir (char *dest, size_t max_len)
{
    FILE *fp = popen ("xdg-user-dir PICTURES 2>/dev/null", "r");
    if (fp && fgets (dest, max_len, fp))
    {
        dest[strcspn (dest, "\n")] = '\0';
        pclose (fp);
    }
    else
    {
        if (fp) pclose (fp);
        const char *home = getenv ("HOME");
        snprintf (dest, max_len, "%s/Pictures", home ? home : "/");
    }
}

static void *
_inotify_monitor_thread (void *arg)
{
    int fd = inotify_init1 (IN_CLOEXEC | IN_NONBLOCK);
    if (fd < 0)
        fd = inotify_init ();
    if (fd < 0)
        return NULL;

    char xdg_pics[512];
    char xdg_screenshots[1024];
    _get_pictures_dir (xdg_pics, sizeof (xdg_pics));
    snprintf (xdg_screenshots, sizeof (xdg_screenshots), "%s/Screenshots", xdg_pics);

    int wd1 = inotify_add_watch (fd, xdg_pics, IN_CREATE | IN_MOVED_TO);
    int wd2 = inotify_add_watch (fd, xdg_screenshots, IN_CREATE | IN_MOVED_TO);

    while (g_notification_threads_started)
    {
        struct timeval tv = { 1, 0 }; // 1-second timeout for cpu friendliness
        fd_set rfds;
        FD_ZERO (&rfds);
        FD_SET (fd, &rfds);

        int retval = select (fd + 1, &rfds, NULL, NULL, &tv);
        if (retval > 0)
        {
            char buffer[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
            ssize_t len = read (fd, buffer, sizeof (buffer));
            if (len > 0)
            {
                pthread_mutex_lock (&g_notification_mutex);
                g_notification_expire_time = time (NULL) + 12; // 12-second timeout for screenshot toast
                pthread_mutex_unlock (&g_notification_mutex);
            }
        }
    }

    if (wd1 >= 0) inotify_rm_watch (fd, wd1);
    if (wd2 >= 0) inotify_rm_watch (fd, wd2);
    close (fd);
    return NULL;
}

static void
_ensure_notification_threads_started (void)
{
    if (g_notification_threads_started)
        return;

    g_notification_threads_started = true;

    pthread_t tid1, tid2, tid3;
    if (pthread_create (&tid1, NULL, _dbus_monitor_thread, NULL) == 0)
    {
        pthread_detach (tid1);
    }
    if (pthread_create (&tid2, NULL, _gsettings_monitor_thread, NULL) == 0)
    {
        pthread_detach (tid2);
    }
    if (pthread_create (&tid3, NULL, _inotify_monitor_thread, NULL) == 0)
    {
        pthread_detach (tid3);
    }

    GF_LOG_INFO (
        "Native GNOME notification, GSettings, and screenshot watchers started successfully.");
}

static bool
_rect_intersect (const gf_rect_t *a, const gf_rect_t *b, XRectangle *res)
{
    int x1 = (a->x > b->x) ? a->x : b->x;
    int y1 = (a->y > b->y) ? a->y : b->y;
    int x2 = (a->x + a->width < b->x + b->width) ? a->x + a->width : b->x + b->width;
    int y2 = (a->y + a->height < b->y + b->height) ? a->y + a->height : b->y + b->height;

    if (x1 < x2 && y1 < y2)
    {
        res->x = x1 - a->x;
        res->y = y1 - a->y;
        res->width = x2 - x1;
        res->height = y2 - y1;
        return true;
    }
    return false;
}

static Window
_get_toplevel_parent (Display *dpy, Window w)
{
    Window root, parent, *children = NULL;
    unsigned int nchildren = 0;
    Window current = w;

    while (current != None)
    {
        if (!XQueryTree (dpy, current, &root, &parent, &children, &nchildren))
            return w;

        if (children)
        {
            XFree (children);
            children = NULL;
        }

        if (parent == root || parent == None)
            return current;

        current = parent;
    }
    return w;
}

void
_apply_shape_mask (Display *dpy, Window overlay, int w, int h, int thickness, int frame_w,
                   int frame_h, const XRectangle *sub_rects, int sub_count)
{
    int shape_event_base, shape_error_base;
    if (!XShapeQueryExtension (dpy, &shape_event_base, &shape_error_base))
    {
        GF_LOG_WARN ("Shape extension not supported");
        return;
    }

    if (w <= 2 * thickness || h <= 2 * thickness)
    {
        GF_LOG_WARN (
            "_apply_shape_mask: window too small, skipping (w=%d h=%d thickness=%d)", w,
            h, thickness);
        return;
    }

    // Create the full window region
    Region full_reg = XCreateRegion ();
    XRectangle full_rect = { 0, 0, (unsigned short)w, (unsigned short)h };
    XUnionRectWithRegion (&full_rect, full_reg, full_reg);

    // Create the interior hollow region
    Region hollow_reg = XCreateRegion ();
    XRectangle hollow_rect = { (short)thickness, (short)thickness,
                               (unsigned short)frame_w, (unsigned short)frame_h };
    XUnionRectWithRegion (&hollow_rect, hollow_reg, hollow_reg);

    // Create the exclusion region
    Region sub_reg = XCreateRegion ();
    if (sub_count > 0 && sub_rects)
    {
        for (int i = 0; i < sub_count; i++)
        {
            XUnionRectWithRegion ((XRectangle *)&sub_rects[i], sub_reg, sub_reg);
        }
    }

    // Subtract the hollow interior from the full region
    Region border_reg = XCreateRegion ();
    XSubtractRegion (full_reg, hollow_reg, border_reg);

    // Subtract the exclusion region from the border region
    Region final_reg = XCreateRegion ();
    XSubtractRegion (border_reg, sub_reg, final_reg);

    XWindowAttributes attrs;
    bool is_viewable = false;
    if (XGetWindowAttributes (dpy, overlay, &attrs))
    {
        is_viewable = (attrs.map_state == IsViewable);
    }

    // Apply the final bounding shape mask
    XShapeCombineRegion (dpy, overlay, ShapeBounding, 0, 0, final_reg, ShapeSet);

    if (is_viewable)
    {
        // Force Mutter compositor to refresh the shape of the override_redirect window
        XUnmapWindow (dpy, overlay);
        XSync (dpy, False);
        XMapWindow (dpy, overlay);
        XSync (dpy, False);
    }

    // Make the entire overlay click-through (empty input region).
    XShapeCombineRectangles (dpy, overlay, ShapeInput, 0, 0, &full_rect, 0, ShapeSet,
                             Unsorted);

    // Cleanup regions
    XDestroyRegion (full_reg);
    XDestroyRegion (hollow_reg);
    XDestroyRegion (sub_reg);
    XDestroyRegion (border_reg);
    XDestroyRegion (final_reg);

    XSync (dpy, False);
}

void
_resize_border_overlay (Display *dpy, gf_border_t *b, const gf_rect_t *frame)
{
    int thickness = b->thickness;
    int x = frame->x - thickness;
    int y = frame->y - thickness;
    int w = frame->width + 2 * thickness;
    int h = frame->height + 2 * thickness;

    if (w > 0 && h > 0 && x >= SHRT_MIN && y >= SHRT_MIN && x <= SHRT_MAX && y <= SHRT_MAX
        && w <= USHRT_MAX && h <= USHRT_MAX)
    {
        XMoveResizeWindow (dpy, b->overlay, x, y, w, h);
    }

    b->last_rect = *frame;
}

Window
_create_border_overlay (Display *dpy, Window target, gf_color_t color, int thickness)
{
    gf_rect_t frame;
    if (!_get_frame_geometry (dpy, target, &frame))
    {
        GF_LOG_ERROR ("Failed to get frame geometry for target %lu",
                      (unsigned long)target);
        return None;
    }

    // Validate geometry
    if (frame.width <= 0 || frame.height <= 0 || thickness < 0)
    {
        GF_LOG_ERROR ("Invalid geometry for border overlay: w=%d, h=%d, thickness=%d",
                      frame.width, frame.height, thickness);
        return None;
    }

    int x = frame.x - thickness;
    int y = frame.y - thickness;
    int w = frame.width + 2 * thickness;
    int h = frame.height + 2 * thickness;

    Window root = DefaultRootWindow (dpy);
    XWindowAttributes root_attrs;
    if (!XGetWindowAttributes (dpy, root, &root_attrs))
        return None;

    XSetWindowAttributes swa;
    swa.override_redirect = True;
    swa.background_pixel = color & 0x00FFFFFF;
    swa.border_pixel = 0;
    swa.save_under = True;
    swa.colormap = root_attrs.colormap;
    swa.bit_gravity = NorthWestGravity;

    Window overlay = XCreateWindow (
        dpy, root, x, y, w, h, 0, root_attrs.depth, InputOutput, root_attrs.visual,
        CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWSaveUnder | CWColormap,
        &swa);

    if (!overlay)
    {
        GF_LOG_ERROR ("Failed to create border overlay window");
        return None;
    }

    _apply_shape_mask (dpy, overlay, w, h, thickness, frame.width, frame.height, NULL, 0);
    XMapWindow (dpy, overlay);
    return overlay;
}

void
gf_border_add (gf_platform_t *platform, gf_handle_t window, gf_color_t color,
               int thickness)
{
    if (!platform || !platform->platform_data)
        return;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    if (!data || !data->display || !data->borders)
        return;

    if (data->border_count >= GF_MAX_WINDOWS_PER_WORKSPACE * GF_MAX_WORKSPACES)
    {
        GF_LOG_WARN ("Border limit reached, cannot add more borders");
        return;
    }

    if (gf_window_is_excluded (data->display, (Window)window))
    {
        return;
    }

    // Check if border already exists for this window
    for (int i = 0; i < data->border_count; i++)
    {
        if (data->borders[i] && data->borders[i]->target == (Window)window)
        {
            GF_LOG_INFO ("Border already exists for window %lu, updating color",
                         (unsigned long)window);
            // Update existing border color
            gf_border_t *border = data->borders[i];
            border->color = color;
            if (border->overlay)
            {
                XSetWindowAttributes swa;
                swa.background_pixel = color & 0x00FFFFFF;
                XChangeWindowAttributes (data->display, border->overlay, CWBackPixel,
                                         &swa);
                XClearWindow (data->display, border->overlay);
            }
            return;
        }
    }

    Window overlay = _create_border_overlay (data->display, window, color, thickness);
    if (!overlay)
    {
        GF_LOG_WARN ("Failed to create border overlay for window %lu",
                     (unsigned long)window);
        return;
    }

    gf_border_t *border = gf_malloc (sizeof (gf_border_t));
    if (!border)
    {
        XDestroyWindow (data->display, overlay);
        return;
    }

    border->target = (Window)window;
    border->overlay = overlay;
    border->color = color;
    border->thickness = thickness;
    border->last_intersect_count = 0;

    // Init last_rect
    XWindowAttributes attrs;
    if (XGetWindowAttributes (data->display, (Window)window, &attrs))
    {
        Window root = DefaultRootWindow (data->display);
        int abs_x, abs_y;
        Window child;
        XTranslateCoordinates (data->display, (Window)window, root, 0, 0, &abs_x, &abs_y,
                               &child);

        // Store Frame Geometry as last_rect
        int left_ext = 0, right_ext = 0, top_ext = 0, bottom_ext = 0;
        bool is_csd = false;
        gf_platform_get_frame_extents (data->display, (Window)window, &left_ext,
                                       &right_ext, &top_ext, &bottom_ext, &is_csd);

        if (is_csd)
        {
            border->last_rect.x = abs_x + left_ext;
            border->last_rect.y = abs_y + top_ext;
            border->last_rect.width = attrs.width - left_ext - right_ext;
            border->last_rect.height = attrs.height - top_ext - bottom_ext;
        }
        else
        {
            border->last_rect.x = abs_x - left_ext;
            border->last_rect.y = abs_y - top_ext;
            border->last_rect.width = attrs.width + left_ext + right_ext;
            border->last_rect.height = attrs.height + top_ext + bottom_ext;
        }
    }

    data->borders[data->border_count++] = border;

    XFlush (data->display);
    GF_LOG_INFO ("Border added for window %lu (overlay %lu)", (unsigned long)window,
                 (unsigned long)overlay);
}

void
gf_border_cleanup (gf_platform_t *platform)
{
    g_notification_threads_started = false;

    if (!platform || !platform->platform_data)
        return;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    if (!data->borders)
        return;

    for (int i = 0; i < data->border_count; i++)
    {
        if (data->borders[i])
        {
            if (data->borders[i]->overlay)
                XDestroyWindow (data->display, data->borders[i]->overlay);
            gf_free (data->borders[i]);
        }
    }
    data->border_count = 0;
}

void
gf_border_remove (gf_platform_t *platform, gf_handle_t window)
{
    if (!platform || !platform->platform_data)
        return;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    Window target = (Window)window;

    for (int i = 0; i < data->border_count; i++)
    {
        if (data->borders[i] && data->borders[i]->target == target)
        {
            if (data->borders[i]->overlay)
                XDestroyWindow (data->display, data->borders[i]->overlay);

            gf_free (data->borders[i]);

            // Shift
            for (int j = i; j < data->border_count - 1; j++)
                data->borders[j] = data->borders[j + 1];

            data->border_count--;
            return;
        }
    }
}

void
gf_border_update (gf_platform_t *platform, const gf_config_t *config)
{
    if (!platform || !platform->platform_data || !config)
        return;

    _ensure_notification_threads_started ();

    time_t now = time (NULL);
    bool notification_active = false;
    pthread_mutex_lock (&g_notification_mutex);
    if (now < g_notification_expire_time)
    {
        notification_active = true;
    }
    pthread_mutex_unlock (&g_notification_mutex);

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    Display *dpy = data->display;
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();

    // Find all GUI windows geometries
    gf_rect_t gui_geoms[32];
    int gui_count = 0;

    // Load static exclude zones from config
    for (uint32_t i = 0; i < config->exclude_zones_count && gui_count < 32; i++)
    {
        gui_geoms[gui_count++] = config->exclude_zones[i];
    }

    Window root = DefaultRootWindow (dpy);
    unsigned char *prop_data = NULL;
    unsigned long nitems = 0;
    if (gf_platform_get_window_property (dpy, root, atoms->net_client_list, XA_WINDOW,
                                         &prop_data, &nitems)
        == GF_SUCCESS)
    {
        Window *clients = (Window *)prop_data;
        for (unsigned long i = 0; i < nitems && gui_count < 32; i++)
        {
            if (_window_excluded_border (dpy, clients[i])
                && !_window_has_type (dpy, clients[i], atoms->net_wm_window_type_desktop)
                && !_window_has_type (dpy, clients[i], atoms->net_wm_window_type_dock))
            {
                if (_get_frame_geometry (dpy, clients[i], &gui_geoms[gui_count]))
                {
                    gui_count++;
                }
            }
        }
        XFree (prop_data);
    }

    for (int i = 0; i < data->border_count;)
    {
        gf_border_t *b = data->borders[i];
        if (!b || _window_excluded_border (dpy, b->target))
        {
            i++;
            continue;
        }

        XWindowAttributes attrs;
        if (!XGetWindowAttributes (dpy, b->target, &attrs))
        {
            // Window is definitely gone - removing
            XDestroyWindow (dpy, b->overlay);
            gf_free (b);
            // Shift array
            for (int j = i; j < data->border_count - 1; j++)
                data->borders[j] = data->borders[j + 1];
            data->border_count--;
            continue;
        }

        if (attrs.map_state == IsUnmapped || gf_window_is_minimized (dpy, b->target)
            || gf_window_is_maximized (dpy, b->target))
        {
            XUnmapWindow (dpy, b->overlay);
            i++;
            continue;
        }

        XMapWindow (dpy, b->overlay);

        // Keep the overlay stacked directly above its target's top-level parent window.
        // This ensures the border overlay is stacked above the target window, but safely
        // BELOW any other top-level active windows or GNOME Shell's UI elements (like
        // panels, popups, or notifications).
        Window toplevel = _get_toplevel_parent (dpy, b->target);
        if (toplevel != None)
        {
            XWindowChanges changes;
            changes.sibling = toplevel;
            changes.stack_mode = Above;
            XConfigureWindow (dpy, b->overlay, CWSibling | CWStackMode, &changes);
        }

        // Update Color dynamically
        if (b->color != config->border_color)
        {
            b->color = config->border_color;
            XSetWindowAttributes swa;
            swa.background_pixel = b->color & 0x00FFFFFF;
            XChangeWindowAttributes (dpy, b->overlay, CWBackPixel, &swa);
            XClearWindow (dpy, b->overlay);
        }

        gf_rect_t frame;
        if (!_get_frame_geometry (dpy, b->target, &frame))
        {
            i++;
            continue;
        }

        int thickness = b->thickness;
        int win_x = frame.x - thickness;
        int win_y = frame.y - thickness;
        int win_w = frame.width + 2 * thickness;
        int win_h = frame.height + 2 * thickness;

        // Find intersections with GUI windows to subtract from shape
        XRectangle intersections[32];
        int intersect_count = 0;
        gf_rect_t border_rect = { win_x, win_y, win_w, win_h };

        for (int g = 0; g < gui_count && intersect_count < 32; g++)
        {
            if (_rect_intersect (&border_rect, &gui_geoms[g],
                                 &intersections[intersect_count]))
            {
                intersect_count++;
            }
        }

        // Native notification/favorites sync exclusion box injection
        if (notification_active && intersect_count < 32)
        {
            gf_monitor_id_t monitor_id = gf_monitor_from_window (platform, (gf_handle_t)b->target);
            gf_rect_t monitor_bounds;
            gf_err_t err = gf_screen_get_bounds_for_monitor (dpy, monitor_id, &monitor_bounds);
            if (err != GF_SUCCESS)
            {
                // Fallback to primary screen dimensions
                monitor_bounds.x = 0;
                monitor_bounds.y = 0;
                monitor_bounds.width = DisplayWidth (dpy, DefaultScreen (dpy));
                monitor_bounds.height = DisplayHeight (dpy, DefaultScreen (dpy));
            }

            int notify_w = (monitor_bounds.width > 1920) ? 1400 : 900;
            int notify_x = monitor_bounds.x + (monitor_bounds.width - notify_w) / 2;
            gf_rect_t notify_geom = { notify_x, monitor_bounds.y, notify_w, 250 };

            if (_rect_intersect (&border_rect, &notify_geom,
                                 &intersections[intersect_count]))
            {
                intersect_count++;
            }
        }

        bool geom_changed = (frame.x != b->last_rect.x || frame.y != b->last_rect.y
                             || frame.width != b->last_rect.width
                             || frame.height != b->last_rect.height);

        bool shape_changed = geom_changed || (intersect_count != b->last_intersect_count);

        if (!shape_changed && intersect_count > 0)
        {
            for (int k = 0; k < intersect_count; k++)
            {
                if (intersections[k].x != b->last_intersections[k].x
                    || intersections[k].y != b->last_intersections[k].y
                    || intersections[k].width != b->last_intersections[k].width
                    || intersections[k].height != b->last_intersections[k].height)
                {
                    shape_changed = true;
                    break;
                }
            }
        }

        if (shape_changed)
        {
            if (geom_changed)
                XMoveResizeWindow (dpy, b->overlay, win_x, win_y, win_w, win_h);

            if (intersect_count > 0)
            {
                for (int k = 0; k < intersect_count; k++)
                {
                    GF_LOG_INFO ("Applying border shape mask for overlay %lu, exclusion "
                                 "%d: local_x=%d, local_y=%d, w=%d, h=%d",
                                 (unsigned long)b->overlay, k, intersections[k].x,
                                 intersections[k].y, intersections[k].width,
                                 intersections[k].height);
                }
            }

            _apply_shape_mask (dpy, b->overlay, win_w, win_h, thickness, frame.width,
                               frame.height, intersections, intersect_count);

            b->last_rect = frame;
            b->last_intersect_count = intersect_count;
            if (intersect_count > 0)
            {
                memcpy (b->last_intersections, intersections,
                        intersect_count * sizeof (XRectangle));
            }
        }

        i++;
    }
    XFlush (dpy);
}
