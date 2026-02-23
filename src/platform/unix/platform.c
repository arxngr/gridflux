#include "../../utils/logger.h"
#include "../../utils/memory.h"
#include "../platform_compat.h"
#include "gesture.h"
#include "internal.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int
gf_platform_error_handler (Display *display, XErrorEvent *error)
{
    char error_text[256];
    XGetErrorText (display, error->error_code, error_text, sizeof (error_text));
    GF_LOG_ERROR ("Platform Error: %s (code: %d)", error_text, error->error_code);
    return 0;
}

static int
platform_io_error_handler (Display *dpy)
{
    (void)dpy;
    return 0; // prevent abort
}

gf_platform_t *
gf_platform_create (void)
{
    gf_platform_t *platform = gf_malloc (sizeof (gf_platform_t));
    if (!platform)
        return NULL;

    gf_linux_platform_data_t *data = gf_malloc (sizeof (gf_linux_platform_data_t));
    if (!data)
    {
        gf_free (platform);
        return NULL;
    }

    memset (platform, 0, sizeof (gf_platform_t));
    memset (data, 0, sizeof (gf_linux_platform_data_t));

    // --- Lifecycle & Core ---
    platform->init = gf_platform_init;
    platform->cleanup = gf_platform_cleanup;

    // --- Window Enumeration & Info ---
    platform->window_enumerate = gf_platform_get_windows;
    platform->window_get_focused = gf_window_get_focused;
    platform->window_get_name = gf_window_get_name;

    // --- Window Geometry & State ---
    platform->window_get_geometry = gf_window_get_geometry;
    platform->window_is_excluded = gf_window_is_excluded;
    platform->window_is_fullscreen = gf_window_is_fullscreen;
    platform->window_is_hidden = NULL;
    platform->window_is_maximized = gf_window_is_maximized;
    platform->window_is_minimized = gf_window_is_minimized;
    platform->window_is_valid = gf_window_is_valid;
    platform->window_minimize = gf_window_minimize;
    platform->window_set_geometry = gf_window_set_geometry;
    platform->window_unminimize = gf_window_unminimize;

    // --- Workspace & Screen ---
    platform->screen_get_bounds = gf_screen_get_bounds;
    platform->workspace_get_count = gf_workspace_get_count;

    // --- Border Management ---
    platform->border_add = gf_border_add;
    platform->border_cleanup = gf_border_cleanup;
    platform->border_remove = gf_border_remove;
    platform->border_update = gf_border_update;

    // --- Dock Management ---
    platform->dock_hide = gf_dock_hide;
    platform->dock_restore = gf_dock_restore;

    // --- Gesture Support ---
    platform->gesture_cleanup = gf_gesture_cleanup;
    platform->gesture_init = gf_gesture_init;
    platform->gesture_poll = gf_gesture_poll;

    platform->platform_data = data;

    return platform;
}

void
gf_platform_destroy (gf_platform_t *platform)
{
    if (!platform)
        return;

    gf_free (platform->platform_data);
    gf_free (platform);
}

gf_err_t
gf_platform_init (gf_platform_t *platform, gf_display_t *display)
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

    gf_err_t result = gf_platform_atoms_init (*display, &data->atoms);
    if (result != GF_SUCCESS)
    {
        XCloseDisplay (*display);
        *display = NULL;
        return result;
    }

    // Initialize borders array
    data->borders = gf_calloc (GF_MAX_WINDOWS_PER_WORKSPACE * GF_MAX_WORKSPACES,
                               sizeof (gf_border_t *));
    if (!data->borders)
    {
        XCloseDisplay (*display);
        return GF_ERROR_MEMORY_ALLOCATION;
    }
    data->border_count = 0;

    GF_LOG_INFO ("Platform initialized successfully");
    return GF_SUCCESS;
}

void
gf_platform_cleanup (gf_display_t display, gf_platform_t *platform)
{
    if (!platform || !platform->platform_data)
        return;

    gf_linux_platform_data_t *data = (gf_linux_platform_data_t *)platform->platform_data;

    if (display)
    {
        XSetIOErrorHandler (platform_io_error_handler);

        XSync (display, False);
        XFlush (display);

        // Restore dock if it was hidden (fallback if core cleanup didn't do it)
        gf_dock_restore (platform);

        /* Defensive close */
        XCloseDisplay (display);
        display = NULL;

        GF_LOG_INFO ("Platform cleaned up");
    }

    gf_free (data->borders);
    gf_free (data);
}

gf_err_t
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

gf_err_t
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

gf_err_t
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
