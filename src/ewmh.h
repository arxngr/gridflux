/*
 * This file is part of gridflux.
 *
 * gridflux is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gridflux is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gridflux.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2025 Ardinugraha
 */

#ifndef GF_EWMH
#define GF_EWMH

#include <X11/Xlib.h>

#define ERR_DISPLAY_NULL "Display is NULL"
#define ERR_WINDOW_INVALID "Invalid window"
#define ERR_DIMENSIONS_NULL "Width or height pointer is NULL"
#define ERR_BAD_WINDOW "Bad window"
#define ERR_UNKNOWN "Unknown error"
#define ERR_MSG_NULL "Message is NULL"
#define ERR_SEND_MSG_FAIL "Fail to send message"
#define ERR_FAIL_ALLOCATE "Fail to allocate"

typedef void (*gf_set_geometry_func)(void *window, int x, int y, int width,
                                     int height, void *user_data, char *sess);

typedef struct {
  gf_set_geometry_func set_geometry;
  void *user_data;
  char *session;
} gf_split_ctx;

typedef struct {
  Atom gf_state;
  Atom net_gf_state;
  Atom net_gf_max_horz;
  Atom net_gf_max_vert;
  Atom net_gf_desktop;
  Atom net_gf_type;
  Atom net_gf_tooltip;
  Atom net_gf_notification;
  Atom net_gf_toolbar;
  Atom net_gf_normal;
  Atom net_gf_hidden;
  Atom net_gf_popup_menu;
  Atom net_gf_utility;

  Atom client_list;
  Atom client_list_stack;
  Atom num_of_desktop;
  Atom net_curr_desktop;

  Atom motif_gf_hints;
  Atom net_gf_modal;
  Atom net_gf_skip_taskbar;
  Atom gtk_frame_extents;
  Atom net_frame_extents;
  Atom net_moveresize_window;
} gf_atom_type;

void gf_set_geometry(void *window_ptr, int x, int y, int width, int height,
                     void *user_data, char *session);

void gf_split_window_generic(void **windows, int window_count, int x, int y,
                             int width, int height, int depth,
                             gf_split_ctx *ctx);

extern gf_atom_type atoms;
void gf_init_atom(Display *display);

#endif // GF_EWMH
