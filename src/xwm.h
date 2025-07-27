
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
 * Copyright (C) 2024 Ardinugraha
 */

#ifndef X_SESSION_H
#define X_SESSION_H

#define CHANGE_X (1 << 0)
#define CHANGE_Y (1 << 1)
#define CHANGE_WIDTH (1 << 2)
#define CHANGE_HEIGHT (1 << 3)
#define HINT_NO_DECORATIONS (1 << 4)
#define APPLY_PADDING (1 << 5)
#define DEFAULT_PADDING 6

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <unistd.h>

typedef struct gf_win_node gf_win_node;

struct gf_win_node {
  Window window;               // X11 window identifier
  int height;                  // Window height
  int width;                   // Window width
  int x, y;                    // Window position
  int workspace_id;            // Workspace the window belongs to
  unsigned long last_modified; // Timestamp of last modification
  Bool is_maximized;           // Whether window is currently maximized
  Bool needs_update;           // Flag indicating window needs rearrangement
  struct gf_win_node *next;    // Pointer to next window in list
};

typedef struct {
  int workspace_id;      // Workspace identifier
  int total_window_open; // Current number of windows in workspace
  int available_space;   // Available slots for new windows
} gf_workspace_info;

typedef struct {
  gf_win_node *window_list;     // Head of linked list of windows
  unsigned long total_windows;  // Total number of tracked windows
  unsigned long last_scan_time; // Last time we scanned for invalid windows
  int active_workspace;         // Currently active workspace
} gf_state;

void gf_x_run_layout();
void gf_x_set_geometry(Display *display, Window window, int gravity,
                       unsigned long mask, int x, int y, int width, int height);

#endif /* X_SESSION_H */
