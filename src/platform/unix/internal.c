#include "internal.h"
#include "../../utils/logger.h"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool
_window_name_matches (const char *name, const char *list[], size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        if (strcmp (name, list[i]) == 0)
            return true;
    }
    return false;
}

bool
_window_screenshot_app (gf_display_t display, gf_handle_t window)
{
    const char *screenshot_classes[]
        = { "flameshot", "Gnome-screenshot", "Spectacle", "Shutter", "Plasma" };

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

bool
_window_has_type (gf_display_t display, gf_handle_t window, Atom type)
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

bool
_window_has_excluded_state (gf_display_t display, gf_handle_t window)
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

bool
_window_has_excluded_type (gf_display_t display, gf_handle_t window)
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

gf_err_t
_remove_size_constraints (Display *dpy, Window win)
{
    XSizeHints *hints = XAllocSizeHints ();
    if (!hints)
        return GF_ERROR_MEMORY_ALLOCATION;
    hints->flags = 0;
    hints->min_width = 1;
    hints->min_height = 1;
    hints->max_width = INT_MAX;
    hints->max_height = INT_MAX;
    XSetWMNormalHints (dpy, win, hints);
    XFree (hints);
    return GF_SUCCESS;
}

void
_run_bg_command (const char *cmd, char *const argv[])
{
    pid_t pid = fork ();
    if (pid == 0)
    {
        // Child process
        // Redirect stdout/stderr to /dev/null to avoid cluttering logs
        int null_fd = open ("/dev/null", O_WRONLY);
        if (null_fd >= 0)
        {
            dup2 (null_fd, STDOUT_FILENO);
            dup2 (null_fd, STDERR_FILENO);
            close (null_fd);
        }

        execvp (cmd, argv);
        _exit (1); // Exit if exec fails
    }
}

bool
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

bool
_process_window_for_list (Display *display, Window window, gf_platform_atoms_t *atoms,
                          gf_ws_id_t *workspace_id, gf_win_info_t *info)
{
    if (!gf_window_is_valid (display, window))
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

        if (workspace_id != NULL && (gf_ws_id_t)window_workspace != *workspace_id)
        {
            return false;
        }
    }

    gf_rect_t geometry;
    if (gf_window_get_geometry (display, window, &geometry) != GF_SUCCESS)
        return false;

    bool is_maximized = gf_window_is_maximized (display, window);
    bool is_excluded = gf_window_is_excluded (display, window);

    gf_ws_id_t resolved_workspace
        = (workspace_id != NULL) ? *workspace_id : GF_FIRST_WORKSPACE_ID;

    *info = (gf_win_info_t){ .id = window,
                             .workspace_id = resolved_workspace,
                             .geometry = geometry,
                             .is_maximized = is_maximized,
                             .needs_update = false,
                             .is_valid = !is_excluded,
                             .last_modified = time (NULL) };
    return true;
}
