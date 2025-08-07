
#ifndef GF_PLATFORM_X11_ATOMS_H
#define GF_PLATFORM_X11_ATOMS_H

#ifdef GF_PLATFORM_X11

#include "../../core/types.h"
#include <X11/Xlib.h>

// X11 specific atoms
typedef struct {
  Atom wm_state;
  Atom net_wm_state;
  Atom net_wm_state_maximized_horz;
  Atom net_wm_state_maximized_vert;
  Atom net_wm_state_hidden;
  Atom net_wm_state_modal;
  Atom net_wm_state_skip_taskbar;

  Atom net_wm_desktop;
  Atom net_current_desktop;
  Atom net_number_of_desktops;

  Atom net_client_list;
  Atom net_client_list_stacking;

  Atom net_wm_window_type;
  Atom net_wm_window_type_normal;
  Atom net_wm_window_type_dialog;
  Atom net_wm_window_type_utility;
  Atom net_wm_window_type_toolbar;
  Atom net_wm_window_type_menu;
  Atom net_wm_window_type_splash;
  Atom net_wm_window_type_dropdown_menu;
  Atom net_wm_window_type_popup_menu;
  Atom net_wm_window_type_tooltip;
  Atom net_wm_window_type_notification;

  Atom net_frame_extents;
  Atom gtk_frame_extents;
  Atom qt_frame_extents;

  Atom net_moveresize_window;
  Atom motif_wm_hints;
} gf_x11_atoms_t;

// X11 atoms management
gf_error_code_t gf_x11_atoms_init(Display *display, gf_x11_atoms_t *atoms);
gf_x11_atoms_t *gf_x11_atoms_get_global(void);

#endif // GF_PLATFORM_X11

#endif // GF_PLATFORM_X11_ATOMS_H
