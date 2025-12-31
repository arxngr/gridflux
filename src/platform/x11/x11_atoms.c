#include <X11/Xlib.h>
#ifdef GF_PLATFORM_X11
#include "../../core/logger.h"
#include "x11_atoms.h"
#include <X11/Xatom.h>

static gf_x11_atoms_t g_atoms = { 0 };

gf_error_code_t
gf_x11_atoms_init (Display *display, gf_x11_atoms_t *atoms)
{
    if (!display || !atoms)
        return GF_ERROR_INVALID_PARAMETER;

    atoms->net_active_window = XInternAtom (display, "_NET_ACTIVE_WINDOW", False);
    atoms->wm_state = XInternAtom (display, "WM_STATE", False);
    atoms->wm_change_state = XInternAtom (display, "WM_CHANGE_STATE", False);
    atoms->net_wm_state = XInternAtom (display, "_NET_WM_STATE", False);
    atoms->net_wm_state_maximized_horz
        = XInternAtom (display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    atoms->net_wm_state_maximized_vert
        = XInternAtom (display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    atoms->net_wm_state_hidden = XInternAtom (display, "_NET_WM_STATE_HIDDEN", False);
    atoms->net_wm_state_modal = XInternAtom (display, "_NET_WM_STATE_MODAL", False);
    atoms->net_wm_state_skip_taskbar
        = XInternAtom (display, "_NET_WM_STATE_SKIP_TASKBAR", False);
    atoms->net_wm_state_above = XInternAtom (display, "_NET_WM_STATE_ABOVE", False);
    atoms->net_wm_state_sticky = XInternAtom (display, "_NET_WM_STATE_STICKY", False);

    atoms->net_wm_desktop = XInternAtom (display, "_NET_WM_DESKTOP", True);
    atoms->net_current_desktop = XInternAtom (display, "_NET_CURRENT_DESKTOP", True);
    atoms->net_number_of_desktops
        = XInternAtom (display, "_NET_NUMBER_OF_DESKTOPS", True);
    atoms->net_client_list = XInternAtom (display, "_NET_CLIENT_LIST", True);
    atoms->net_client_list_stacking
        = XInternAtom (display, "_NET_CLIENT_LIST_STACKING", True);

    atoms->net_wm_window_type = XInternAtom (display, "_NET_WM_WINDOW_TYPE", False);
    atoms->net_wm_window_type_normal
        = XInternAtom (display, "_NET_WM_WINDOW_TYPE_NORMAL", True);
    atoms->net_wm_window_type_dialog
        = XInternAtom (display, "_NET_WM_WINDOW_TYPE_DIALOG", True);
    atoms->net_wm_window_type_utility
        = XInternAtom (display, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    atoms->net_wm_window_type_toolbar
        = XInternAtom (display, "_NET_WM_WINDOW_TYPE_TOOLBAR", True);
    atoms->net_wm_window_type_menu
        = XInternAtom (display, "_NET_WM_WINDOW_TYPE_MENU", True);
    atoms->net_wm_window_type_splash
        = XInternAtom (display, "_NET_WM_WINDOW_TYPE_SPLASH", True);
    atoms->net_wm_window_type_dropdown_menu
        = XInternAtom (display, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", True);
    atoms->net_wm_window_type_popup_menu
        = XInternAtom (display, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
    atoms->net_wm_window_type_tooltip
        = XInternAtom (display, "_NET_WM_WINDOW_TYPE_TOOLTIP", True);
    atoms->net_wm_window_type_notification
        = XInternAtom (display, "_NET_WM_WINDOW_TYPE_NOTIFICATION", True);
    atoms->net_wm_window_type_dock
        = XInternAtom (display, "_NET_WM_WINDOW_TYPE_DOCK", False);
    atoms->net_wm_window_type_combo
        = XInternAtom (display, "_NET_WM_WINDOW_TYPE_COMBO", False);

    atoms->net_frame_extents = XInternAtom (display, "_NET_FRAME_EXTENTS", False);
    atoms->gtk_frame_extents = XInternAtom (display, "_GTK_FRAME_EXTENTS", False);
    atoms->qt_frame_extents = XInternAtom (display, "_QT_FRAME_EXTENTS", False);
    atoms->net_moveresize_window = XInternAtom (display, "_NET_MOVERESIZE_WINDOW", False);
    atoms->motif_wm_hints = XInternAtom (display, "_MOTIF_WM_HINTS", False);

    atoms->net_wm_strut = XInternAtom (display, "_NET_WM_STRUT", False);
    atoms->net_wm_strut_partial = XInternAtom (display, "_NET_WM_STRUT_PARTIAL", False);
    atoms->net_wm_pid = XInternAtom (display, "_NET_WM_PID", False);

    atoms->net_wm_name = XInternAtom (display, "_NET_WM_NAME", False);
    atoms->utf8_string = XInternAtom (display, "UTF8_STRING", False);

    g_atoms = *atoms;
    GF_LOG_DEBUG ("X11 atoms initialized successfully");
    return GF_SUCCESS;
}

gf_x11_atoms_t *
gf_x11_atoms_get_global (void)
{
    return &g_atoms;
}
#endif // GF_PLATFORM_X11
