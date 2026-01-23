#include "platform.h"
#include "../../layout.h"
#include "../../logger.h"
#include "../../memory.h"
#include "../../types.h"
#include "backend.h"
#include "platform_compat.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static bool
_window_name_matches (const char *name, const char *list[], size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        if (strcmp (name, list[i]) == 0)
            return true;
    }
    return false;
}

static bool
_is_screenshot_app (gf_display_t display, gf_native_window_t window)
{
    const char *screenshot_classes[]
        = { "flameshot", "Gnome-screenshot", "Spectacle", "Shutter" };

    XClassHint hint;
    if (XGetClassHint (display, window, &hint))
    {
        bool match = (hint.res_class
                      && _window_name_matches (hint.res_class, screenshot_classes,
                                               sizeof (screenshot_classes)
                                                   / sizeof (screenshot_classes[0])))
                     || (hint.res_name
                         && _window_name_matches (hint.res_name, screenshot_classes,
                                                  sizeof (screenshot_classes)
                                                      / sizeof (screenshot_classes[0])));

        if (hint.res_name)
            XFree (hint.res_name);
        if (hint.res_class)
            XFree (hint.res_class);

        return match;
    }
    return false;
}

static bool
_window_has_type (gf_display_t display, gf_native_window_t window, Atom type)
{
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    unsigned char *data = NULL;
    unsigned long nitems = 0;

    if (gf_platform_get_window_property (display, window, atoms->net_wm_window_type,
                                         XA_ATOM, &data, &nitems)
            != GF_SUCCESS
        || nitems == 0)
        return false;

    Atom *types = (Atom *)data;
    bool found = false;
    for (unsigned long i = 0; i < nitems; i++)
    {
        if (types[i] == type)
        {
            found = true;
            break;
        }
    }
    XFree (data);
    return found;
}

static bool
_window_has_excluded_state (gf_display_t display, gf_native_window_t window)
{
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    Atom excluded_states[] = { atoms->net_wm_state_skip_taskbar,
                               atoms->net_wm_state_modal, atoms->net_wm_state_above };

    for (size_t i = 0; i < sizeof (excluded_states) / sizeof (excluded_states[0]); i++)
    {
        if (gf_platform_window_has_state (display, window, excluded_states[i]))
            return true;
    }
    return false;
}

static bool
_window_has_excluded_type (gf_display_t display, gf_native_window_t window)
{
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    Atom excluded_types[] = {
        atoms->net_wm_window_type_dock,         atoms->net_wm_window_type_desktop,
        atoms->net_wm_window_type_toolbar,      atoms->net_wm_window_type_menu,
        atoms->net_wm_window_type_splash,       atoms->net_wm_window_type_dropdown_menu,
        atoms->net_wm_window_type_popup_menu,   atoms->net_wm_window_type_tooltip,
        atoms->net_wm_window_type_notification, atoms->net_wm_window_type_utility,
        atoms->net_wm_window_type_combo
    };

    for (size_t i = 0; i < sizeof (excluded_types) / sizeof (excluded_types[0]); i++)
    {
        if (_window_has_type (display, window, excluded_types[i]))
            return true;
    }
    return false;
}

static int
gf_platform_error_handler (Display *display, XErrorEvent *error)
{
    char error_text[256];
    XGetErrorText (display, error->error_code, error_text, sizeof (error_text));
    GF_LOG_ERROR ("Platform Error: %s (code: %d)", error_text, error->error_code);
    return 0;
}

gf_platform_interface_t *
gf_platform_create (void)
{
    gf_platform_interface_t *platform = gf_malloc (sizeof (gf_platform_interface_t));
    if (!platform)
        return NULL;

    gf_linux_platform_data_t *data = gf_malloc (sizeof (gf_linux_platform_data_t));
    if (!data)
    {
        gf_free (platform);
        return NULL;
    }

    memset (platform, 0, sizeof (gf_platform_interface_t));
    memset (data, 0, sizeof (gf_linux_platform_data_t));

    platform->init = gf_platform_init;
    platform->cleanup = gf_platform_cleanup;
    platform->get_windows = gf_platform_get_windows;
    platform->unmaximize_window = gf_platform_unmaximize_window;
    platform->window_name_info = gf_platform_get_window_name;
    platform->minimize_window = gf_platform_minimize_window;
    platform->unminimize_window = gf_platform_unminimize_window;
    platform->get_window_geometry = gf_platform_get_window_geometry;
    platform->get_current_workspace = gf_platform_get_current_workspace;
    platform->get_workspace_count = gf_platform_get_workspace_count;
    platform->create_workspace = gf_platform_create_workspace;
    platform->is_window_valid = gf_platform_is_window_valid;
    platform->is_window_excluded = gf_platform_is_window_excluded;
    platform->get_active_window = gf_platform_active_window;
    platform->is_window_minimized = gf_platform_is_window_minimized;
    platform->is_fullscreen = gf_platform_is_fullscreen;

    platform->add_border = gf_platform_add_border;
    platform->update_border = gf_platform_update_borders;
    platform->set_border_color = gf_platform_set_border_color;
    platform->cleanup_borders = gf_platform_cleanup_borders;
    platform->remove_border = gf_platform_remove_border;

    platform->platform_data = data;

    gf_desktop_env_t env = gf_detect_desktop_env ();
    switch (env)
    {
    case GF_DE_GNOME:
        GF_LOG_INFO ("Using GNOME Extensions backend for tilling");
        platform->get_screen_bounds = gf_platform_noop_get_screen_bounds;
        platform->set_window_geometry = gf_platform_noop_set_window_geometry;
        break;

    default:
        platform->get_screen_bounds = gf_platform_get_screen_bounds;
        platform->set_window_geometry = gf_platform_set_window_geometry;
        break;
    }

    gf_backend_type_t backend = gf_detect_backend ();
    data->use_kwin_backend = false;

#ifdef GF_KWIN_SUPPORT
    if (backend == GF_BACKEND_KWIN_QML)
    {
        GF_LOG_INFO ("Using KWin QML backend for tiling");
        data->use_kwin_backend = true;

        platform->get_screen_bounds = gf_platform_noop_get_screen_bounds;
        platform->set_window_geometry = gf_platform_noop_set_window_geometry;

        return platform;
    }
#endif

    return platform;
}

void
gf_platform_destroy (gf_platform_interface_t *platform)
{
    if (!platform)
        return;

    gf_free (platform->platform_data);
    gf_free (platform);
}

gf_error_code_t
gf_platform_init (gf_platform_interface_t *platform, gf_display_t *display)
{
    GF_LOG_INFO ("Initialize platform...");
    if (!display)
        return GF_ERROR_INVALID_PARAMETER;

    *display = XOpenDisplay (NULL);
    if (!*display)
    {
        GF_LOG_ERROR ("Failed to open display");
        return GF_ERROR_DISPLAY_CONNECTION;
    }

    XSetErrorHandler (gf_platform_error_handler);

    // Get platform data and initialize atoms
    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;

    data->screen = DefaultScreen (*display);
    data->root_window = RootWindow (*display, data->screen);
    data->display = *display;

    gf_error_code_t result = gf_platform_atoms_init (*display, &data->atoms);
    if (result != GF_SUCCESS)
    {
        XCloseDisplay (*display);
        *display = NULL;
        return result;
    }

    // Initialize borders array (Do this before KWin init might return)
    data->borders = gf_calloc (GF_MAX_WINDOWS_PER_WORKSPACE * GF_MAX_WORKSPACES,
                               sizeof (gf_border_t *));
    if (!data->borders)
    {
        XCloseDisplay (*display);
        return GF_ERROR_MEMORY_ALLOCATION;
    }
    data->border_count = 0;

#ifdef GF_KWIN_SUPPORT
    // If KWin backend, also initialize D-Bus and load script
    if (data->use_kwin_backend)
    {
        return gf_kwin_platform_init (platform);
    }
#endif

    GF_LOG_INFO ("Platform initialized successfully");
    return GF_SUCCESS;
}

static int
platform_io_error_handler (Display *dpy)
{
    (void)dpy;
    return 0; // prevent abort
}

void
gf_platform_cleanup (gf_display_t display, gf_platform_interface_t *platform)
{
    if (!platform || !platform->platform_data)
        return;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;

#ifdef GF_KWIN_SUPPORT
    if (data->use_kwin_backend)
    {
        gf_kwin_platform_cleanup (platform);
        /* Give KWin time to release X resources */
        usleep (100 * 1000); // 100ms
    }
#endif

    if (display)
    {
        XSetIOErrorHandler (platform_io_error_handler);

        XSync (display, False);
        XFlush (display);

        /* Defensive close */
        XCloseDisplay (display);
        display = NULL;

        GF_LOG_INFO ("Platform cleaned up");
    }

    gf_free (data);
}

gf_error_code_t
gf_platform_get_window_property (Display *display, Window window, Atom property,
                                 Atom type, unsigned char **data, unsigned long *nitems)
{
    if (!display || !data || !nitems)
        return GF_ERROR_INVALID_PARAMETER;

    Atom actual_type;
    int actual_format;
    unsigned long bytes_after;

    int status
        = XGetWindowProperty (display, window, property, 0, (~0L), False, type,
                              &actual_type, &actual_format, nitems, &bytes_after, data);

    if (status != Success || !*data || *nitems == 0)
    {
        if (*data)
            XFree (*data);
        *data = NULL;
        *nitems = 0;
        return GF_ERROR_PLATFORM_ERROR;
    }

    return GF_SUCCESS;
}

gf_error_code_t
gf_platform_send_client_message (Display *display, Window window, Atom message_type,
                                 long *data, int count)
{
    if (!display)
    {
        GF_LOG_ERROR ("XSendEvent failed: display is NULL");
        return GF_ERROR_INVALID_PARAMETER;
    }

    if (message_type == None)
    {
        GF_LOG_ERROR ("XSendEvent failed: message_type is None");
        return GF_ERROR_INVALID_PARAMETER;
    }

    XClientMessageEvent event = { 0 };
    event.type = ClientMessage;
    event.window = window;
    event.message_type = message_type;
    event.format = 32;

    for (int i = 0; i < count && i < 5; i++)
        event.data.l[i] = data[i];

    Status ok = XSendEvent (display, DefaultRootWindow (display), False,
                            SubstructureRedirectMask | SubstructureNotifyMask,
                            (XEvent *)&event);

    if (ok == 0)
    {
        GF_LOG_ERROR ("XSendEvent rejected by WM (atom=%lu, target=%lu)", message_type,
                      window);
        return GF_ERROR_PLATFORM_ERROR;
    }

    XFlush (display);
    return GF_SUCCESS;
}

bool
gf_platform_window_has_state (Display *display, Window window, Atom state)
{
    if (!display)
        return false;

    unsigned char *data = NULL;
    unsigned long nitems = 0;
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();

    if (gf_platform_get_window_property (display, window, atoms->net_wm_state, XA_ATOM,
                                         &data, &nitems)
        != GF_SUCCESS)
    {
        return false;
    }

    Atom *states = (Atom *)data;
    bool found = false;

    for (unsigned long i = 0; i < nitems; i++)
    {
        if (states[i] == state)
        {
            found = true;
            break;
        }
    }

    XFree (data);
    return found;
}

gf_error_code_t
gf_platform_get_frame_extents (Display *dpy, Window win, int *left, int *right, int *top,
                               int *bottom, bool *is_csd)
{
    if (!dpy || !left || !right || !top || !bottom)
        return GF_ERROR_INVALID_PARAMETER;

    *left = *right = *top = *bottom = 0;
    if (is_csd)
        *is_csd = false;

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();

    Atom candidates[] = {
        atoms->net_frame_extents, // Standard
        atoms->gtk_frame_extents, // GTK CSD
        atoms->qt_frame_extents   // KDE Qt apps
    };

    for (size_t i = 0; i < 3; i++)
    {
        unsigned char *data = NULL;
        unsigned long nitems = 0;

        if (gf_platform_get_window_property (dpy, win, candidates[i], XA_CARDINAL, &data,
                                             &nitems)
                == GF_SUCCESS
            && data && nitems >= 4)
        {
            unsigned long *ext = (unsigned long *)data;
            *left = ext[0];
            *right = ext[1];
            *top = ext[2];
            *bottom = ext[3];

            if (is_csd)
                *is_csd = (candidates[i] == atoms->gtk_frame_extents);

            XFree (data);
            return GF_SUCCESS;
        }

        if (data)
            XFree (data);
    }

    return GF_ERROR_PLATFORM_ERROR;
}

static bool
_get_frame_geometry (Display *dpy, Window target, gf_rect_t *frame_rect)
{
    XWindowAttributes attrs;
    if (!XGetWindowAttributes (dpy, target, &attrs))
        return false;

    int left_ext = 0, right_ext = 0, top_ext = 0, bottom_ext = 0;
    bool is_csd = false;
    gf_platform_get_frame_extents (dpy, target, &left_ext, &right_ext, &top_ext,
                                   &bottom_ext, &is_csd);

    Window root = DefaultRootWindow (dpy);
    int abs_x, abs_y;
    Window child;
    if (!XTranslateCoordinates (dpy, target, root, 0, 0, &abs_x, &abs_y, &child))
        return false;

    if (is_csd)
    {
        frame_rect->x = abs_x + left_ext;
        frame_rect->y = abs_y + top_ext;
        frame_rect->width = attrs.width - left_ext - right_ext;
        frame_rect->height = attrs.height - top_ext - bottom_ext;
    }
    else
    {
        frame_rect->x = abs_x - left_ext;
        frame_rect->y = abs_y - top_ext;
        frame_rect->width = attrs.width + left_ext + right_ext;
        frame_rect->height = attrs.height + top_ext + bottom_ext;
    }
    return true;
}

static void
_apply_shape_mask (Display *dpy, Window overlay, int w, int h, int thickness, int frame_w,
                   int frame_h)
{
    int shape_event_base, shape_error_base;
    if (!XShapeQueryExtension (dpy, &shape_event_base, &shape_error_base))
    {
        GF_LOG_WARN ("Shape extension not supported");
        return;
    }

    if (w <= 2 * thickness || h <= 2 * thickness)
        return;

    XRectangle rects[1];
    rects[0].x = thickness;
    rects[0].y = thickness;
    rects[0].width = frame_w;
    rects[0].height = frame_h;

    XShapeCombineMask (dpy, overlay, ShapeBounding, 0, 0, None, ShapeSet);
    XShapeCombineRectangles (dpy, overlay, ShapeBounding, 0, 0, rects, 1, ShapeSubtract,
                             Unsorted);
    XShapeCombineRectangles (dpy, overlay, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
    XShapeCombineRectangles (dpy, overlay, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
}

static void
_resize_border_overlay (Display *dpy, gf_border_t *b, const gf_rect_t *frame)
{
    if (frame->x == b->last_rect.x && frame->y == b->last_rect.y
        && frame->width == b->last_rect.width && frame->height == b->last_rect.height)
    {
        return;
    }

    int thickness = b->thickness;
    int x = frame->x - thickness;
    int y = frame->y - thickness;
    int w = frame->width + 2 * thickness;
    int h = frame->height + 2 * thickness;

    if (w > 0 && h > 0 && x >= SHRT_MIN && y >= SHRT_MIN && x <= SHRT_MAX && y <= SHRT_MAX
        && w <= USHRT_MAX && h <= USHRT_MAX)
    {
        XMoveResizeWindow (dpy, b->overlay, x, y, w, h);
        _apply_shape_mask (dpy, b->overlay, w, h, thickness, frame->width, frame->height);
    }

    b->last_rect = *frame;
}

static Window
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

    _apply_shape_mask (dpy, overlay, w, h, thickness, frame.width, frame.height);
    XMapWindow (dpy, overlay);
    return overlay;
}

void
gf_platform_add_border (gf_platform_interface_t *platform, gf_native_window_t window,
                        gf_color_t color, int thickness)
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

    if (gf_platform_is_window_excluded (data->display, (Window)window))
    {
        return;
    }

    if (gf_platform_is_window_excluded (data->display, (Window)window))
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
                XChangeWindowAttributes (data->display, border->overlay, CWBackPixel, &swa);
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
gf_platform_cleanup_borders (gf_platform_interface_t *platform)
{
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
gf_platform_remove_border (gf_platform_interface_t *platform, gf_native_window_t window)
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
gf_platform_set_border_color (gf_platform_interface_t *platform, gf_color_t color)
{
    if (!platform || !platform->platform_data)
        return;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;

    // Update existing borders
    for (int i = 0; i < data->border_count; i++)
    {
        gf_border_t *b = data->borders[i];
        if (b && b->overlay)
        {
            b->color = color;
            XSetWindowAttributes swa;
            swa.background_pixel = color & 0x00FFFFFF;
            XChangeWindowAttributes (data->display, b->overlay, CWBackPixel, &swa);
            XClearWindow (data->display, b->overlay);
        }
    }

    XFlush (data->display);
}

void
gf_platform_update_borders (gf_platform_interface_t *platform)
{
    if (!platform || !platform->platform_data)
        return;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;
    Display *dpy = data->display;

    for (int i = 0; i < data->border_count;)
    {
        gf_border_t *b = data->borders[i];
        if (!b)
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

        if (attrs.map_state == IsUnmapped
            || gf_platform_is_window_minimized (dpy, b->target))
        {
            XUnmapWindow (dpy, b->overlay);
            i++;
            continue;
        }

        XMapWindow (dpy, b->overlay);

        gf_rect_t frame;
        if (!_get_frame_geometry (dpy, b->target, &frame))
        {
            i++;
            continue;
        }

        _resize_border_overlay (dpy, b, &frame);
        i++;
    }
    XFlush (dpy);
}

static bool
_process_window_for_list (Display *display, Window window, gf_platform_atoms_t *atoms,
                          gf_workspace_id_t *workspace_id, gf_window_info_t *info)
{
    if (!gf_platform_is_window_valid (display, window))
        return false;

    unsigned char *desktop_data = NULL;
    unsigned long desktop_nitems = 0;
    if (gf_platform_get_window_property (display, window, atoms->net_wm_desktop,
                                         XA_CARDINAL, &desktop_data, &desktop_nitems)
            == GF_SUCCESS
        && desktop_nitems > 0)
    {
        unsigned long window_workspace = *(unsigned long *)desktop_data;
        XFree (desktop_data);

        if (workspace_id != NULL && *workspace_id >= GF_FIRST_WORKSPACE_ID
            && (gf_workspace_id_t)window_workspace != *workspace_id)
        {
            return false;
        }
    }

    gf_rect_t geometry;
    if (gf_platform_get_window_geometry (display, window, &geometry) != GF_SUCCESS)
        return false;

    bool is_maximized = gf_platform_window_has_state (display, window,
                                                      atoms->net_wm_state_maximized_horz)
                        || gf_platform_window_has_state (
                            display, window, atoms->net_wm_state_maximized_vert);

    bool is_excluded = gf_platform_is_window_excluded (display, window);

    gf_workspace_id_t resolved_workspace
        = (workspace_id != NULL) ? *workspace_id : GF_FIRST_WORKSPACE_ID;

    *info = (gf_window_info_t){ .id = (gf_window_id_t)window,
                                .native_handle = window,
                                .workspace_id = resolved_workspace,
                                .geometry = geometry,
                                .is_maximized = is_maximized,
                                .needs_update = false,
                                .is_valid = !is_excluded,
                                .last_modified = time (NULL) };
    return true;
}

gf_error_code_t
gf_platform_get_windows (gf_display_t display, gf_workspace_id_t *workspace_id,
                         gf_window_info_t **windows, uint32_t *count)
{
    if (!display || !windows || !count)
        return GF_ERROR_INVALID_PARAMETER;

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    Window root = DefaultRootWindow (display);

    unsigned char *data = NULL;
    unsigned long nitems = 0;

    gf_error_code_t result = gf_platform_get_window_property (
        display, root, atoms->net_client_list, XA_WINDOW, &data, &nitems);
    if (result != GF_SUCCESS)
    {
        *windows = NULL;
        *count = 0;
        return GF_SUCCESS; // No windows is not an error
    }

    Window *window_list = (Window *)data;
    gf_window_info_t *filtered_windows = gf_malloc (nitems * sizeof (gf_window_info_t));
    if (!filtered_windows)
    {
        XFree (data);
        return GF_ERROR_MEMORY_ALLOCATION;
    }

    uint32_t filtered_count = 0;

    for (unsigned long i = 0; i < nitems; i++)
    {
        gf_window_info_t info;
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
        *windows
            = gf_realloc (filtered_windows, filtered_count * sizeof (gf_window_info_t));
        if (!*windows)
        {
            gf_free (filtered_windows);
            return GF_ERROR_MEMORY_ALLOCATION;
        }
    }

    *count = filtered_count;
    return GF_SUCCESS;
}

gf_error_code_t
gf_platform_unmaximize_window (gf_display_t display, gf_native_window_t window)
{
    if (!display)
        return GF_ERROR_INVALID_PARAMETER;

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    long data[]
        = { 0, atoms->net_wm_state_maximized_horz, atoms->net_wm_state_maximized_vert };

    return gf_platform_send_client_message (display, window, atoms->net_wm_state, data,
                                            3);
}

gf_error_code_t
gf_platform_get_window_geometry (gf_display_t display, gf_native_window_t window,
                                 gf_rect_t *geometry)
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

gf_workspace_id_t
gf_platform_get_current_workspace (gf_display_t display)
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
        gf_workspace_id_t workspace = *(unsigned long *)data;
        XFree (data);
        return workspace;
    }

    return GF_FIRST_WORKSPACE_ID; // Default to workspace 1
}

uint32_t
gf_platform_get_workspace_count (gf_display_t display)
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

gf_error_code_t
gf_platform_create_workspace (gf_display_t display)
{
    if (!display)
        return GF_ERROR_INVALID_PARAMETER;

    Display *dpy = (Display *)display;
    Window root = DefaultRootWindow (dpy);

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();

    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    int status
        = XGetWindowProperty (dpy, root, atoms->net_number_of_desktops, 0, 1, False,
                              XA_CARDINAL, &type, &format, &nitems, &bytes_after, &data);

    if (status != Success || !data || nitems != 1)
    {
        if (data)
            XFree (data);
        GF_LOG_ERROR ("Failed to read current workspace count");
        return GF_ERROR_PLATFORM_ERROR;
    }

    uint32_t current = *(uint32_t *)data;
    XFree (data);

    uint32_t new_count = current + 1;

    XChangeProperty (dpy, root, atoms->net_number_of_desktops, XA_CARDINAL, 32,
                     PropModeReplace, (unsigned char *)&new_count, 1);

    XFlush (dpy);

    GF_LOG_INFO ("Requested new workspace: %u → %u", current, new_count);

    return GF_SUCCESS;
}

bool
gf_platform_is_window_valid (gf_display_t display, gf_native_window_t window)
{
    if (!display)
        return false;

    XWindowAttributes attrs;
    return XGetWindowAttributes (display, window, &attrs) != 0;
}

bool

gf_platform_is_window_excluded (gf_display_t display, gf_native_window_t window)
{
    if (!display || window == None)
        return true;

    char name[256] = { 0 };
    gf_platform_get_window_name (display, window, name, sizeof (name));

    /* Exclude our own app */
    if (strstr (name, "GridFlux") != NULL)
        return true;

    if (_is_screenshot_app (display, window))
        return true;

    /* Exclude fullscreen OR maximized NORMAL windows */
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    bool is_fullscreen
        = gf_platform_window_has_state (display, window, atoms->net_wm_state_fullscreen);
    bool is_maximized = gf_platform_window_has_state (display, window,
                                                      atoms->net_wm_state_maximized_horz)
                        && gf_platform_window_has_state (
                            display, window, atoms->net_wm_state_maximized_vert);

    if (_window_has_type (display, window, atoms->net_wm_window_type_normal)
        && (is_fullscreen || is_maximized))
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
gf_platform_is_fullscreen (gf_display_t display, gf_native_window_t window)
{
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    return gf_platform_window_has_state (display, (Window)window,
                                         atoms->net_wm_state_fullscreen);
}

gf_error_code_t
gf_platform_get_screen_bounds (gf_display_t dpy, gf_rect_t *bounds)
{
    if (!dpy || !bounds)
        return GF_ERROR_INVALID_PARAMETER;

    int screen = DefaultScreen (dpy);
    Window root = DefaultRootWindow (dpy);
    Screen *scr = ScreenOfDisplay (dpy, screen);

    int sw = scr->width;
    int sh = scr->height;

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();

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

            if (gf_platform_get_window_property (dpy, clients[i],
                                                 atoms->net_wm_strut_partial, XA_CARDINAL,
                                                 (unsigned char **)&strut, &nitems)
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
gf_platform_set_window_geometry (gf_display_t dpy, gf_native_window_t win,
                                 const gf_rect_t *geometry, gf_geometry_flags_t flags,
                                 gf_config_t *cfg)
{
    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();

    if (!dpy || !geometry)
        return GF_ERROR_INVALID_PARAMETER;

    gf_rect_t rect = *geometry;

    if (flags & GF_GEOMETRY_APPLY_PADDING)
        gf_rect_apply_padding (&rect, cfg->default_padding);

    long data[5];

    data[0] = NorthWestGravity | // gravity = NorthWestGravity
              (1 << 8) |         // set x
              (1 << 9) |         // set y
              (1 << 10) |        // set width
              (1 << 11);         // set height

    data[1] = rect.x;
    data[2] = rect.y;
    data[3] = rect.width;
    data[4] = rect.height;

    return gf_platform_send_client_message (dpy, win, atoms->net_moveresize_window, data,
                                            5);
}

static Window
gf_platform_get_focused_window (Display *dpy)
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

    return win;
}

gf_window_id_t
gf_platform_active_window (Display *dpy)
{
    if (!dpy)
        return None;

    Window focused = gf_platform_get_focused_window (dpy);
    if (focused == None)
        return None;

    XWindowAttributes attr;
    if (XGetWindowAttributes (dpy, focused, &attr) == 0)
    {
        GF_LOG_DEBUG ("Focused window %lu is invalid", focused);
        return None;
    }

    if (attr.map_state != IsViewable)
    {
        GF_LOG_DEBUG ("Focused window %lu is not viewable (map_state=%d)", focused,
                      attr.map_state);
        return None;
    }

    return focused;
}

gf_error_code_t
gf_platform_minimize_window (gf_display_t display, gf_native_window_t window)
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

gf_error_code_t
gf_platform_unminimize_window (gf_display_t display, Window window)
{
    if (!display || window == None)
        return GF_ERROR_INVALID_PARAMETER;

    XWindowAttributes attr;
    if (XGetWindowAttributes (display, window, &attr) == 0)
    {
        GF_LOG_WARN ("Cannot unminimize invalid window: %lu", window);
        return GF_ERROR_PLATFORM_ERROR;
    }

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    if (!atoms)
        return GF_ERROR_PLATFORM_ERROR;

    gf_desktop_env_t env = gf_detect_desktop_env ();

    if (env == GF_DE_KDE)
    {
        XMapWindow (display, window);
        XRaiseWindow (display, window);
        XFlush (display);
        return GF_SUCCESS;
    }

    if (atoms->net_wm_state != None && atoms->net_wm_state_hidden != None)
    {
        long data[5] = { 0, /* _NET_WM_STATE_REMOVE */
                         atoms->net_wm_state_hidden, 0, 0, 0 };

        gf_platform_send_client_message (display, window, atoms->net_wm_state, data, 5);
    }

    if (atoms->net_active_window != None)
    {
        long data[5] = { 0, /* source: application */
                         CurrentTime, 0, 0, 0 };

        gf_platform_send_client_message (display, window, atoms->net_active_window, data,
                                         5);
    }

    return GF_SUCCESS;
}

void
gf_platform_get_window_name (gf_display_t dpy, gf_native_window_t win, char *buffer,
                             size_t bufsize)
{
    if (!dpy || win == None || !buffer || bufsize == 0)
        return;

    buffer[0] = '\0'; // default empty

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    if (!atoms)
        return;

    Atom actual;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    if (XGetWindowProperty (dpy, win, atoms->net_wm_name, 0, (~0L), False,
                            atoms->utf8_string, &actual, &format, &nitems, &bytes_after,
                            &data)
            == Success
        && data && nitems > 0)
    {
        strncpy (buffer, (char *)data, bufsize - 1);
        buffer[bufsize - 1] = '\0';
        XFree (data);
        return;
    }

    if (data)
        XFree (data);

    char *legacy_name = NULL;
    if (XFetchName (dpy, win, &legacy_name) > 0 && legacy_name)
    {
        strncpy (buffer, legacy_name, bufsize - 1);
        buffer[bufsize - 1] = '\0';
        XFree (legacy_name);
        return;
    }

    buffer[0] = '\0';
}

static gf_error_code_t
gf_platform_noop_set_window_geometry (gf_display_t display, gf_native_window_t window,
                                      const gf_rect_t *geometry,
                                      gf_geometry_flags_t flags, gf_config_t *cfg)
{
    (void)display;
    (void)window;
    (void)geometry;
    (void)flags;
    (void)cfg;

    return GF_SUCCESS;
}

static gf_error_code_t
gf_platform_noop_get_screen_bounds (gf_display_t display, gf_rect_t *bounds)
{
    (void)display;
    (void)bounds;
    return GF_SUCCESS;
}

bool
gf_platform_is_window_minimized (gf_display_t display, gf_native_window_t window)
{
    if (!display || window == None)
        return false;

    if (!gf_platform_is_window_valid (display, window))
        return false;

    gf_platform_atoms_t *atoms = gf_platform_atoms_get_global ();
    if (!atoms)
        return false;

    return gf_platform_window_has_state (display, window, atoms->net_wm_state_hidden);
}
