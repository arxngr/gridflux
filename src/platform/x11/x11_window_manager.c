#include "x11_window_manager.h"
#include "../../core/logger.h"
#include "../../core/types.h"
#include "x11_backend.h"
#include "../../utils/memory.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// X11 platform implementation
static int
gf_x11_error_handler (Display *display, XErrorEvent *error)
{
    char error_text[256];
    XGetErrorText (display, error->error_code, error_text, sizeof (error_text));
    GF_LOG_ERROR ("X11 Error: %s (code: %d)", error_text, error->error_code);
    return 0;
}

gf_platform_interface_t *
gf_x11_platform_create (void)
{
    gf_platform_interface_t *platform = gf_malloc (sizeof (gf_platform_interface_t));
    if (!platform)
        return NULL;

    gf_x11_platform_data_t *data = gf_malloc (sizeof (gf_x11_platform_data_t));
    if (!data)
    {
        gf_free (platform);
        return NULL;
    }

    memset (platform, 0, sizeof (gf_platform_interface_t));
    memset (data, 0, sizeof (gf_x11_platform_data_t));

    platform->init = gf_x11_platform_init;
    platform->cleanup = gf_x11_platform_cleanup;
    platform->get_windows = gf_x11_platform_get_windows;
    platform->move_window_to_workspace = gf_x11_platform_move_window_to_workspace;
    platform->unmaximize_window = gf_x11_platform_unmaximize_window;
    platform->get_window_geometry = gf_x11_platform_get_window_geometry;
    platform->get_current_workspace = gf_x11_platform_get_current_workspace;
    platform->get_workspace_count = gf_x11_platform_get_workspace_count;
    platform->create_workspace = gf_x11_platform_create_workspace;
    platform->is_window_valid = gf_x11_platform_is_window_valid;
    platform->is_window_excluded = gf_x11_platform_is_window_excluded;
    platform->is_window_drag = gf_x11_platform_is_window_drag;
    platform->platform_data = data;

    gf_desktop_env_t env = gf_detect_desktop_env ();

    switch (env)
    {
    case GF_DE_KDE:
        GF_LOG_DEBUG ("KDE Desktop detected");
        platform->get_screen_bounds = gf_x11_kde_get_screen_bounds;
        platform->set_window_geometry = gf_x11_kde_set_window_geometry;
        break;

    default:
        GF_LOG_DEBUG ("GNOME / Other Desktop detected");
        platform->get_screen_bounds = gf_x11_gnome_get_screen_bounds;
        platform->set_window_geometry = gf_x11_gnome_set_window_geometry;
        break;
    }

    return platform;
}

void
gf_x11_platform_destroy (gf_platform_interface_t *platform)
{
    if (!platform)
        return;

    gf_free (platform->platform_data);
    gf_free (platform);
}

static gf_error_code_t
gf_x11_platform_init (gf_platform_interface_t *platform, gf_display_t *display)
{
    if (!display)
        return GF_ERROR_INVALID_PARAMETER;

    *display = XOpenDisplay (NULL);
    if (!*display)
    {
        GF_LOG_ERROR ("Failed to open X11 display");
        return GF_ERROR_DISPLAY_CONNECTION;
    }

    XSetErrorHandler (gf_x11_error_handler);

    // Get platform data and initialize atoms
    gf_x11_platform_data_t *data = (gf_x11_platform_data_t *)platform->platform_data;

    data->screen = DefaultScreen (*display);
    data->root_window = RootWindow (*display, data->screen);

    gf_error_code_t result = gf_x11_atoms_init (*display, &data->atoms);
    if (result != GF_SUCCESS)
    {
        XCloseDisplay (*display);
        *display = NULL;
        return result;
    }

    GF_LOG_INFO ("X11 platform initialized successfully");
    return GF_SUCCESS;
}

static void
gf_x11_platform_cleanup (gf_display_t display)
{
    if (display)
    {
        XCloseDisplay (display);
        GF_LOG_INFO ("X11 platform cleaned up");
    }
}

gf_error_code_t
gf_x11_get_window_property (Display *display, Window window, Atom property, Atom type,
                            unsigned char **data, unsigned long *nitems)
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
gf_x11_send_client_message (Display *display, Window window, Atom message_type,
                            long *data, int count)
{
    if (!display || message_type == None)
        return GF_ERROR_INVALID_PARAMETER;

    XClientMessageEvent event = { 0 };
    event.type = ClientMessage;
    event.window = window;
    event.message_type = message_type;
    event.format = 32;

    for (int i = 0; i < count && i < 5; i++)
    {
        event.data.l[i] = data[i];
    }

    if (!XSendEvent (display, DefaultRootWindow (display), False,
                     SubstructureRedirectMask | SubstructureNotifyMask, (XEvent *)&event))
    {
        return GF_ERROR_PLATFORM_ERROR;
    }

    XFlush (display);
    return GF_SUCCESS;
}

bool
gf_x11_window_has_state (Display *display, Window window, Atom state)
{
    if (!display)
        return false;

    unsigned char *data = NULL;
    unsigned long nitems = 0;
    gf_x11_atoms_t *atoms = gf_x11_atoms_get_global ();

    if (gf_x11_get_window_property (display, window, atoms->net_wm_state, XA_ATOM, &data,
                                    &nitems)
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
gf_x11_get_frame_extents (Display *dpy, Window win, int *left, int *right, int *top,
                          int *bottom)
{
    if (!dpy || !left || !right || !top || !bottom)
        return GF_ERROR_INVALID_PARAMETER;

    *left = *right = *top = *bottom = 0;

    gf_x11_atoms_t *atoms = gf_x11_atoms_get_global ();

    Atom candidates[] = {
        atoms->net_frame_extents, // Standard
        atoms->gtk_frame_extents, // GTK CSD
        atoms->qt_frame_extents   // KDE Qt apps
    };

    for (size_t i = 0; i < 3; i++)
    {
        unsigned char *data = NULL;
        unsigned long nitems = 0;

        if (gf_x11_get_window_property (dpy, win, candidates[i], XA_CARDINAL, &data,
                                        &nitems)
                == GF_SUCCESS
            && data && nitems >= 4)
        {
            unsigned long *ext = (unsigned long *)data;
            *left = ext[0];
            *right = ext[1];
            *top = ext[2];
            *bottom = ext[3];

            XFree (data);
            return GF_SUCCESS;
        }

        if (data)
            XFree (data);
    }

    return GF_ERROR_PLATFORM_ERROR;
}

const char *
gf_x11_detect_desktop_environment (void)
{
    const char *xdg_current_desktop = getenv ("XDG_CURRENT_DESKTOP");
    const char *desktop_session = getenv ("DESKTOP_SESSION");
    const char *kde_full_session = getenv ("KDE_FULL_SESSION");
    const char *gnome_session_id = getenv ("GNOME_DESKTOP_SESSION_ID");

    if (kde_full_session && strcmp (kde_full_session, "true") == 0)
    {
        return "KDE";
    }
    else if (gnome_session_id
             || (xdg_current_desktop && strstr (xdg_current_desktop, "GNOME")))
    {
        return "GNOME";
    }
    else if (xdg_current_desktop)
    {
        return xdg_current_desktop;
    }
    else if (desktop_session)
    {
        return desktop_session;
    }
    else
    {
        return "Unknown";
    }
}

static gf_error_code_t
gf_x11_platform_get_windows (gf_display_t display, gf_workspace_id_t workspace_id,
                             gf_window_info_t **windows, uint32_t *count)
{
    if (!display || !windows || !count)
        return GF_ERROR_INVALID_PARAMETER;

    gf_x11_atoms_t *atoms = gf_x11_atoms_get_global ();
    Window root = DefaultRootWindow (display);

    unsigned char *data = NULL;
    unsigned long nitems = 0;

    gf_error_code_t result = gf_x11_get_window_property (
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
        Window window = window_list[i];

        if (!gf_x11_platform_is_window_valid (display, window))
        {
            continue;
        }

        unsigned char *desktop_data = NULL;
        unsigned long desktop_nitems = 0;
        if (gf_x11_get_window_property (display, window, atoms->net_wm_desktop,
                                        XA_CARDINAL, &desktop_data, &desktop_nitems)
                == GF_SUCCESS
            && desktop_nitems > 0)
        {
            unsigned long window_workspace = *(unsigned long *)desktop_data;
            XFree (desktop_data);

            if (workspace_id >= 0 && (gf_workspace_id_t)window_workspace != workspace_id)
            {
                continue;
            }
        }

        gf_rect_t geometry;
        if (gf_x11_platform_get_window_geometry (display, window, &geometry)
            != GF_SUCCESS)
        {
            continue;
        }

        bool is_maximized = gf_x11_window_has_state (display, window,
                                                     atoms->net_wm_state_maximized_horz)
                            || gf_x11_window_has_state (
                                display, window, atoms->net_wm_state_maximized_vert);

        bool is_valid = gf_x11_platform_is_window_excluded (display, window) == false;

        filtered_windows[filtered_count]
            = (gf_window_info_t){ .id = (gf_window_id_t)window,
                                  .native_handle = window,
                                  .workspace_id = workspace_id,
                                  .geometry = geometry,
                                  .is_maximized = is_maximized,
                                  .needs_update = false,
                                  .is_valid = is_valid,
                                  .last_modified = time (NULL) };
        filtered_count++;
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

static gf_error_code_t
gf_x11_platform_move_window_to_workspace (gf_display_t display, gf_native_window_t window,
                                          gf_workspace_id_t workspace_id)
{
    gf_x11_atoms_t *atoms = gf_x11_atoms_get_global ();
    gf_error_code_t err;

    // Switch workspace
    long switch_data[] = { workspace_id, CurrentTime };
    err = gf_x11_send_client_message (display, DefaultRootWindow (display),
                                      atoms->net_current_desktop, switch_data, 2);
    if (err != GF_SUCCESS)
        return err;

    long move_data[] = { workspace_id, CurrentTime };
    err = gf_x11_send_client_message (display, window, atoms->net_wm_desktop, move_data,
                                      2);
    if (err != GF_SUCCESS)
        return err;

    // Focus the window opened
    long focus_data[5] = { 1, CurrentTime, None, 0, 0 };
    return gf_x11_send_client_message (display, window, atoms->net_active_window,
                                       focus_data, 3);
}

static gf_error_code_t
gf_x11_platform_unmaximize_window (gf_display_t display, gf_native_window_t window)
{
    if (!display)
        return GF_ERROR_INVALID_PARAMETER;

    gf_x11_atoms_t *atoms = gf_x11_atoms_get_global ();
    long data[]
        = { 0, atoms->net_wm_state_maximized_horz, atoms->net_wm_state_maximized_vert };

    return gf_x11_send_client_message (display, window, atoms->net_wm_state, data, 3);
}

static gf_error_code_t
gf_x11_platform_get_window_geometry (gf_display_t display, gf_native_window_t window,
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

static gf_workspace_id_t
gf_x11_platform_get_current_workspace (gf_display_t display)
{
    if (!display)
        return -1;

    gf_x11_atoms_t *atoms = gf_x11_atoms_get_global ();
    Window root = DefaultRootWindow (display);

    unsigned char *data = NULL;
    unsigned long nitems = 0;

    if (gf_x11_get_window_property (display, root, atoms->net_current_desktop,
                                    XA_CARDINAL, &data, &nitems)
            == GF_SUCCESS
        && nitems > 0)
    {
        gf_workspace_id_t workspace = *(unsigned long *)data;
        XFree (data);
        return workspace;
    }

    return 0; // Default to workspace 0
}

static uint32_t
gf_x11_platform_get_workspace_count (gf_display_t display)
{
    if (!display)
        return 1;

    gf_x11_atoms_t *atoms = gf_x11_atoms_get_global ();
    Window root = DefaultRootWindow (display);

    unsigned char *data = NULL;
    unsigned long nitems = 0;

    if (gf_x11_get_window_property (display, root, atoms->net_number_of_desktops,
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

static gf_error_code_t
gf_x11_platform_create_workspace (gf_display_t display)
{
    if (!display)
        return GF_ERROR_INVALID_PARAMETER;

    const char *desktop_env = gf_x11_detect_desktop_environment ();
    if (!desktop_env || strcmp (desktop_env, "Unknown") == 0)
    {
        GF_LOG_WARN ("Unknown desktop environment, cannot create workspace");
        return GF_ERROR_PLATFORM_ERROR;
    }

    pid_t pid = fork ();
    if (pid == -1)
    {
        GF_LOG_ERROR ("Failed to fork for workspace creation");
        return GF_ERROR_PLATFORM_ERROR;
    }

    if (pid == 0)
    {
        // Child process
        if (strcmp (desktop_env, "KDE") == 0)
        {
            execlp ("qdbus", "qdbus", "org.kde.KWin", "/VirtualDesktopManager",
                    "createDesktop", "1", "GridFlux", NULL);
        }
        else if (strcmp (desktop_env, "GNOME") == 0)
        {
            execlp ("gsettings", "gsettings", "set", "org.gnome.mutter",
                    "dynamic-workspaces", "true", NULL);
        }
        _exit (EXIT_FAILURE);
    }
    else
    {
        // Parent process
        int status;
        waitpid (pid, &status, 0);
        return WIFEXITED (status) && WEXITSTATUS (status) == 0 ? GF_SUCCESS
                                                               : GF_ERROR_PLATFORM_ERROR;
    }
}

static bool
gf_x11_platform_is_window_valid (gf_display_t display, gf_native_window_t window)
{
    if (!display)
        return false;

    XWindowAttributes attrs;
    return XGetWindowAttributes (display, window, &attrs) != 0;
}

static bool
gf_x11_platform_is_window_excluded (gf_display_t display, gf_native_window_t window)
{
    if (!display)
        return true;

    gf_x11_atoms_t *atoms = gf_x11_atoms_get_global ();

    // Check window states
    Atom excluded_states[]
        = { atoms->net_wm_state_hidden, atoms->net_wm_state_skip_taskbar,
            atoms->net_wm_state_modal };

    for (size_t i = 0; i < sizeof (excluded_states) / sizeof (excluded_states[0]); i++)
    {
        if (gf_x11_window_has_state (display, window, excluded_states[i]))
        {
            return true;
        }
    }

    // Check window types
    unsigned char *data = NULL;
    unsigned long nitems = 0;

    if (gf_x11_get_window_property (display, window, atoms->net_wm_window_type, XA_ATOM,
                                    &data, &nitems)
        == GF_SUCCESS)
    {
        Atom *types = (Atom *)data;

        Atom excluded_types[] = { atoms->net_wm_window_type_toolbar,
                                  atoms->net_wm_window_type_menu,
                                  atoms->net_wm_window_type_splash,
                                  atoms->net_wm_window_type_dropdown_menu,
                                  atoms->net_wm_window_type_popup_menu,
                                  atoms->net_wm_window_type_tooltip,
                                  atoms->net_wm_window_type_notification,
                                  atoms->net_wm_window_type_utility };

        for (unsigned long i = 0; i < nitems; i++)
        {
            for (size_t j = 0; j < sizeof (excluded_types) / sizeof (excluded_types[0]);
                 j++)
            {
                if (types[i] == excluded_types[j])
                {
                    XFree (data);
                    return true;
                }
            }
        }

        XFree (data);
    }

    return false;
}

static gf_error_code_t
gf_x11_platform_is_window_drag (gf_display_t display, gf_native_window_t window,
                                gf_rect_t *geometry)
{
    memset (geometry, 0, sizeof (*geometry));

    Window root = DefaultRootWindow (display);
    Window root_return, parent_return;
    Window *children = NULL;
    unsigned int nchildren = 0;

    if (!XQueryTree (display, root, &root_return, &parent_return, &children, &nchildren))
    {
        GF_LOG_ERROR ("Failed to get window list\n");
        return GF_ERROR_PLATFORM_ERROR;
    }

    int *initial_x = gf_malloc (nchildren * sizeof (int));
    int *initial_y = gf_malloc (nchildren * sizeof (int));
    Bool *valid = gf_malloc (nchildren * sizeof (Bool));

    for (unsigned int i = 0; i < nchildren; i++)
    {
        Window child;
        unsigned int width, height, border_width, depth;

        valid[i] = XGetGeometry (display, children[i], &child, &initial_x[i],
                                 &initial_y[i], &width, &height, &border_width, &depth);

        if (valid[i])
        {
            // Convert to absolute screen coords
            int abs_x = 0, abs_y = 0;
            Window dummy;
            XTranslateCoordinates (display, children[i], root, 0, 0, &abs_x, &abs_y,
                                   &dummy);
            initial_x[i] = abs_x;
            initial_y[i] = abs_y;
        }
    }

    while (1)
    {
        Window child_return;
        int root_x, root_y, win_x, win_y;
        unsigned int mask;

        if (XQueryPointer (display, root, &root_return, &child_return, &root_x, &root_y,
                           &win_x, &win_y, &mask))
        {
            if (mask & Button1Mask)
            {
                for (unsigned int i = 0; i < nchildren; i++)
                {
                    if (!valid[i])
                        continue;

                    Window child;
                    int x, y;
                    unsigned int width, height, border_width, depth;

                    if (XGetGeometry (display, children[i], &child, &x, &y, &width,
                                      &height, &border_width, &depth))
                    {
                        // Translate coordinates to root
                        int abs_x = 0, abs_y = 0;
                        Window dummy;
                        XTranslateCoordinates (display, children[i], root, 0, 0, &abs_x,
                                               &abs_y, &dummy);

                        if (abs_x != initial_x[i] || abs_y != initial_y[i])
                        {
                            geometry->x = abs_x;
                            geometry->y = abs_y;
                            geometry->width = (gf_dimension_t)width;
                            geometry->height = (gf_dimension_t)height;
                        }
                    }
                }
            }
            else
            {
                break;
            }
        }

        usleep (20000); // 20ms delay
    }

    gf_free (initial_x);
    gf_free (initial_y);
    gf_free (valid);
    if (children)
        XFree (children);

    return GF_SUCCESS;
}
