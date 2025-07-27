#include "ewmh.h"
#include "gridflux.h"
#include "xwm.h"
#include <string.h>

gf_atom_type atoms;

void gf_x_init_atom(Display *display) {
  atoms.gf_state = XInternAtom(display, "WM_STATE", False);
  atoms.net_gf_state = XInternAtom(display, "_NET_WM_STATE", False);
  atoms.net_gf_max_horz =
      XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
  atoms.net_gf_max_vert =
      XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
  atoms.net_gf_desktop = XInternAtom(display, "_NET_WM_DESKTOP", True);
  atoms.net_gf_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
  atoms.net_gf_tooltip =
      XInternAtom(display, "_NET_WM_WINDOW_TYPE_TOOLTIP", True);
  atoms.net_gf_notification =
      XInternAtom(display, "_NET_WM_WINDOW_TYPE_NOTIFICATION", True);
  atoms.net_gf_toolbar =
      XInternAtom(display, "_NET_WM_WINDOW_TYPE_TOOLBAR", True);
  atoms.net_gf_hidden = XInternAtom(display, "_NET_WM_STATE_HIDDEN", False);
  atoms.net_gf_popup_menu =
      XInternAtom(display, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
  atoms.net_gf_normal =
      XInternAtom(display, "_NET_WM_WINDOW_TYPE_NORMAL", True);

  atoms.net_gf_utility =
      XInternAtom(display, "_NET_WM_WINDOW_TYPE_UTILITY", False);

  atoms.client_list = XInternAtom(display, "_NET_CLIENT_LIST", True);
  atoms.client_list_stack =
      XInternAtom(display, "_NET_CLIENT_LIST_STACKING", True);
  atoms.num_of_desktop = XInternAtom(display, "_NET_NUMBER_OF_DESKTOPS", True);
  atoms.net_curr_desktop = XInternAtom(display, "_NET_CURRENT_DESKTOP", True);
  atoms.motif_gf_hints = XInternAtom(display, "_MOTIF_WM_HINTS", False);
  atoms.net_gf_modal = XInternAtom(display, "_NET_WM_STATE_MODAL", False);
  atoms.net_gf_skip_taskbar =
      XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False);
  atoms.net_frame_extents = XInternAtom(display, "_NET_FRAME_EXTENTS", False);
  atoms.gtk_frame_extents = XInternAtom(display, "_GTK_FRAME_EXTENTS", False);
  atoms.net_moveresize_window =
      XInternAtom(display, "_NET_MOVERESIZE_WINDOW", False);
}

void gf_split_window_generic(void **windows, int window_count, int x, int y,
                             int width, int height, int depth,
                             gf_split_ctx *ctx) {
  if (window_count <= 0)
    return;

  if (window_count == 1 && windows[0] != NULL && ctx->set_geometry != NULL) {
    ctx->set_geometry(windows[0], x, y, width, height, ctx->user_data,
                      ctx->session);
    return;
  }

  int split_vertically = depth % 2 == 0;
  int left_count = window_count / 2;
  int right_count = window_count - left_count;

  if (split_vertically) {
    int left_width = width / 2;
    int right_width = width - left_width;

    gf_split_window_generic(windows, left_count, x, y, left_width, height,
                            depth + 1, ctx);
    gf_split_window_generic(windows + left_count, right_count, x + left_width,
                            y, right_width, height, depth + 1, ctx);
  } else {
    int top_height = height / 2;
    int bottom_height = height - top_height;

    gf_split_window_generic(windows, left_count, x, y, width, top_height,
                            depth + 1, ctx);
    gf_split_window_generic(windows + left_count, right_count, x,
                            y + top_height, width, bottom_height, depth + 1,
                            ctx);
  }
}

void gf_set_geometry(void *window_ptr, int x, int y, int width, int height,
                     void *user_data, char *session) {
  if (!window_ptr || !user_data) {
    LOG(GF_ERR, " NULL Pointer detected \n");
    return;
  }

  if (strcmp(session, GF_X11) == 0) {
    Display *display = (Display *)user_data;
    Window window = *(Window *)window_ptr;

    gf_x_set_geometry(display, window, StaticGravity,
                      CHANGE_X | CHANGE_Y | CHANGE_WIDTH | CHANGE_HEIGHT |
                          APPLY_PADDING,
                      x, y, width, height);
  }
}
