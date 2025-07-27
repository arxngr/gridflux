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
 * Copyright (C) 2024 Ardi Nugraha
 */

#include "xwm.h"
#include "ewmh.h"
#include "gridflux.h"
#include <X11/Xlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

const int MAX_WIN_OPEN = 8;

// Global state
static gf_state g_gf_state = {NULL, 0, 0, -1};

gf_win_node *gf_x_find_window_node(Window window) {
  gf_win_node *current = g_gf_state.window_list;
  while (current) {
    if (current->window == window)
      return current;
    current = current->next;
  }
  return NULL;
}

gf_win_node *gf_x_add_window_node(Window window, int workspace_id) {
  gf_win_node *node = malloc(sizeof(gf_win_node));
  if (!node) {
    LOG(GF_ERR, ERR_FAIL_ALLOCATE);
    return NULL;
  }

  memset(node, 0, sizeof(gf_win_node));
  node->window = window;
  node->workspace_id = workspace_id;
  node->last_modified = time(NULL);
  node->is_maximized = False;
  node->needs_update = True;
  node->next = g_gf_state.window_list;
  g_gf_state.window_list = node;
  g_gf_state.total_windows++;

  LOG(GF_DBG, "Added window %lu to workspace %d (total: %lu)", window,
      workspace_id, g_gf_state.total_windows);

  return node;
}

static void gf_x_remove_window_node(Window window) {
  gf_win_node **current = &g_gf_state.window_list;

  while (*current) {
    if ((*current)->window == window) {
      gf_win_node *to_remove = *current;
      *current = (*current)->next;
      LOG(GF_DBG, "Removed window %lu from workspace %d", window,
          to_remove->workspace_id);
      free(to_remove);
      g_gf_state.total_windows--;
      return;
    }
    current = &(*current)->next;
  }
}

static void gf_x_update_window_info(Display *display, Window window,
                                    int workspace_id) {
  gf_win_node *node = gf_x_find_window_node(window);
  if (!node) {
    node = gf_x_add_window_node(window, workspace_id);
    if (!node)
      return;
  }

  XWindowAttributes attrs;
  if (XGetWindowAttributes(display, window, &attrs)) {
    Bool changed =
        (node->width != attrs.width || node->height != attrs.height ||
         node->x != attrs.x || node->y != attrs.y);

    node->width = attrs.width;
    node->height = attrs.height;
    node->x = attrs.x;
    node->y = attrs.y;

    if (workspace_id >= 0 && node->workspace_id != workspace_id) {
      node->workspace_id = workspace_id;
      changed = True;
    }

    if (changed) {
      node->last_modified = time(NULL);
      node->needs_update = True;
    }
  }
}

static int gf_x_get_workspace_window_count(int workspace_id) {
  int count = 0;
  gf_win_node *current = g_gf_state.window_list;
  while (current) {
    if (current->workspace_id == workspace_id)
      count++;
    current = current->next;
  }
  return count;
}

static Window *gf_x_get_workspace_windows(int workspace_id, int *count) {
  *count = 0;

  gf_win_node *current = g_gf_state.window_list;
  while (current) {
    if (current->workspace_id == workspace_id)
      (*count)++;
    current = current->next;
  }

  if (*count == 0)
    return NULL;

  Window *windows = malloc(sizeof(Window) * (*count));
  if (!windows) {
    LOG(GF_ERR, ERR_FAIL_ALLOCATE);
    *count = 0;
    return NULL;
  }

  int idx = 0;
  current = g_gf_state.window_list;
  while (current && idx < *count) {
    if (current->workspace_id == workspace_id) {
      windows[idx++] = current->window;
    }
    current = current->next;
  }

  return windows;
}

static void gf_x_cleanup_invalid_windows(Display *display) {
  gf_win_node **current = &g_gf_state.window_list;
  int cleaned = 0;

  while (*current) {
    XWindowAttributes attrs;

    if (!XGetWindowAttributes(display, (*current)->window, &attrs)) {
      gf_win_node *to_remove = *current;
      *current = (*current)->next;
      LOG(GF_DBG, "Cleaned invalid window %lu", to_remove->window);
      free(to_remove);
      g_gf_state.total_windows--;
      cleaned++;
    } else {
      current = &(*current)->next;
    }
  }

  if (cleaned > 0) {
    LOG(GF_DBG, "Cleaned %d invalid windows", cleaned);
  }
}

static void gf_x_mark_windows_for_update(int workspace_id) {
  gf_win_node *current = g_gf_state.window_list;
  while (current) {
    if (workspace_id < 0 || current->workspace_id == workspace_id) {
      current->needs_update = True;
    }
    current = current->next;
  }
}

static Bool gf_x_workspace_needs_arrangement(int workspace_id) {
  gf_win_node *current = g_gf_state.window_list;
  while (current) {
    if (current->workspace_id == workspace_id && current->needs_update) {
      return True;
    }
    current = current->next;
  }
  return False;
}

static void gf_x_clear_update_flags(int workspace_id) {
  gf_win_node *current = g_gf_state.window_list;
  while (current) {
    if (current->workspace_id == workspace_id) {
      current->needs_update = False;
    }
    current = current->next;
  }
}

static Display *gf_x_initialize_display() {
  int try_index = 0;
  while (1) {
    Display *display = XOpenDisplay(NULL);
    if (!display) {
      try_index++;
      LOG(GF_ERR, ERR_DISPLAY_NULL);
      sleep(1);
    } else {
      return display;
    }
  }
}

static Window gf_x_get_root_window(Display *display) {
  return RootWindow(display, DefaultScreen(display));
}

static int gf_x_error_handler(Display *display, XErrorEvent *error) {
  if (error->error_code == BadWindow) {
    LOG(GF_ERR, ERR_BAD_WINDOW);
  }
  return 0;
}

static unsigned char *gf_x_get_window_property(Display *display, Window window,
                                               Atom property, Atom type,
                                               unsigned long *nitems,
                                               int *status_out) {
  Atom actual_type;
  int actual_format;
  unsigned long bytes_after;
  unsigned char *data = NULL;

  int status = XGetWindowProperty(display, window, property, 0, (~0L), False,
                                  type, &actual_type, &actual_format, nitems,
                                  &bytes_after, &data);

  if (status_out)
    *status_out = status;

  if (status != Success || !data || *nitems == 0) {
    LOG(GF_ERR, "Failed to fetch property (status=%d, nitems=%lu)", status,
        *nitems);
    if (data)
      XFree(data);
    return NULL;
  }

  return data;
}

static int gf_x_send_client_message(Display *display, Window window,
                                    Atom message_type, Atom *atoms,
                                    int atom_count, long *data) {
  if (message_type == None) {
    LOG(GF_ERR, ERR_MSG_NULL);
    return -1;
  }

  XClientMessageEvent event = {0};
  event.type = ClientMessage;
  event.window = window;
  event.message_type = message_type;
  event.format = 32;

  for (int i = 0; i < atom_count; i++) {
    event.data.l[i] = data[i];
  }

  if (!XSendEvent(display, DefaultRootWindow(display), False,
                  SubstructureRedirectMask | SubstructureNotifyMask,
                  (XEvent *)&event)) {
    LOG(GF_ERR, ERR_SEND_MSG_FAIL);
    return -1;
  }

  XFlush(display);
  return 0;
}

static void gf_x_unmaximize_window(Display *display, Window window) {
  long data[] = {0, atoms.net_gf_max_horz, atoms.net_gf_max_vert};
  gf_x_send_client_message(display, window, atoms.gf_state, NULL, 3, data);
}

static int gf_x_move_window_to_workspace(Display *display, Window window,
                                         int workspace) {
  long data[] = {workspace, CurrentTime};
  if (gf_x_send_client_message(display, window, atoms.net_gf_desktop, NULL, 2,
                               data) == 0) {
    // Update our internal tracking
    gf_win_node *node = gf_x_find_window_node(window);
    if (node)
      node->workspace_id = workspace;
    return 0;
  }
  return 1;
}

static void gf_x_get_window_dimension(Display *display, Window window,
                                      int *width, int *height, int *x, int *y) {
  XWindowAttributes attributes;
  int status = XGetWindowAttributes(display, window, &attributes);

  if (status == 0) {
    LOG(GF_ERR, "Invalid window.\n");
    return;
  }

  if (x != NULL)
    *x = attributes.x;
  if (y != NULL)
    *y = attributes.y;
  if (width != NULL)
    *width = attributes.width;
  if (height != NULL)
    *height = attributes.height;
}

static int gf_x_excluded_window(Display *display, Window window) {
  Atom actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  unsigned char *prop = NULL;

  if (XGetWindowProperty(display, window, atoms.net_gf_state, 0, 1024, False,
                         XA_ATOM, &actual_type, &actual_format, &nitems,
                         &bytes_after, &prop) != Success ||
      prop == NULL) {
    return 0;
  }

  Atom *states = (Atom *)prop;
  int result = 0;

  const Atom excluded_states[] = {
      atoms.net_gf_hidden,       atoms.net_gf_notification,
      atoms.net_gf_popup_menu,   atoms.net_gf_tooltip,
      atoms.net_gf_toolbar,      atoms.net_gf_modal,
      atoms.net_gf_skip_taskbar, atoms.net_gf_utility};

  const size_t excluded_count =
      sizeof(excluded_states) / sizeof(excluded_states[0]);

  for (unsigned long i = 0; i < nitems && !result; i++) {
    for (size_t j = 0; j < excluded_count; j++) {
      if (states[i] == excluded_states[j]) {
        result = 1;
        break;
      }
    }
  }

  XFree(prop);
  return result;
}

void gf_x_set_geometry(Display *display, Window window, int gravity,
                       unsigned long mask, int x, int y, int width,
                       int height) {
  XSizeHints hints;
  long supplied_return;

  if (gravity != 0) {
    Status status =
        XGetWMNormalHints(display, window, &hints, &supplied_return);
    if (status == 0) {
      memset(&hints, 0, sizeof(hints));
      hints.flags = 0;
    }

    hints.flags |= PWinGravity;
    hints.win_gravity = gravity;
    XSetWMNormalHints(display, window, &hints);
  }

  int pad = (mask & APPLY_PADDING) ? DEFAULT_PADDING : 0;
  x += pad;
  y += pad;
  width = width > pad * 2 ? width - pad * 2 : width;
  height = height > pad * 2 ? height - pad * 2 : height;

  if (mask & HINT_NO_DECORATIONS) {
    struct {
      unsigned long flags;
      unsigned long functions;
      unsigned long decorations;
      long input_mode;
      unsigned long status;
    } hints = {2, 0, 0, 0, 0};

    XChangeProperty(display, window, atoms.motif_gf_hints, atoms.motif_gf_hints,
                    32, PropModeReplace, (unsigned char *)&hints, 5);
  }

  XWindowChanges changes;
  unsigned int value_mask = 0;

  if ((mask & (CHANGE_X | CHANGE_Y | CHANGE_WIDTH | CHANGE_HEIGHT)) !=
      (CHANGE_X | CHANGE_Y | CHANGE_WIDTH | CHANGE_HEIGHT)) {
    Window root;
    int cur_x, cur_y;
    unsigned int cur_width, cur_height, border, depth;

    XGetGeometry(display, window, &root, &cur_x, &cur_y, &cur_width,
                 &cur_height, &border, &depth);

    changes.x = (mask & CHANGE_X) ? x : cur_x;
    changes.y = (mask & CHANGE_Y) ? y : cur_y;
    changes.width = (mask & CHANGE_WIDTH) ? width : cur_width;
    changes.height = (mask & CHANGE_HEIGHT) ? height : cur_height;
  } else {
    changes.x = x;
    changes.y = y;
    changes.width = width;
    changes.height = height;
  }

  if (mask & CHANGE_X)
    value_mask |= CWX;
  if (mask & CHANGE_Y)
    value_mask |= CWY;
  if (mask & CHANGE_WIDTH)
    value_mask |= CWWidth;
  if (mask & CHANGE_HEIGHT)
    value_mask |= CWHeight;

  XConfigureWindow(display, window, value_mask, &changes);
  XFlush(display);

  // Update our internal tracking
  gf_x_update_window_info(display, window,
                          -1); // -1 means keep current workspace
}

static void gf_x_arrange_window(int window_count, Window windows[],
                                Display *display, int screen) {
  Screen *scr = ScreenOfDisplay(display, screen);
  int screen_width = scr->width - 5;
  int screen_height = scr->height;

  void *wins[MAX_WIN_OPEN];
  for (int i = 0; i < window_count; i++)
    wins[i] = &windows[i];

  gf_split_ctx ctx = {
      .set_geometry = gf_set_geometry, .user_data = display, .session = GF_X11};

  gf_split_window_generic(wins, window_count, 0, 0, screen_width, screen_height,
                          0, &ctx);
}

static Bool gf_x_window_in_workspace(Display *display, Window window,
                                     int workspace_id) {
  if (atoms.net_gf_desktop == None)
    return False;

  unsigned long nitems = 0;
  int status;

  unsigned char *data = gf_x_get_window_property(
      display, window, atoms.net_gf_desktop, XA_CARDINAL, &nitems, &status);

  if (!data || status != Success || nitems < 1)
    return False;

  unsigned long window_workspace_id = *(unsigned long *)data;
  XFree(data);

  return window_workspace_id == (unsigned long)workspace_id;
}

static Window *gf_x_filter_windows(Display *display, Window *windows,
                                   unsigned long *nitems, int workspace_id) {
  if (!windows || !nitems || *nitems == 0)
    return NULL;

  Window *filtered = malloc(sizeof(Window) * (*nitems));
  if (!filtered) {
    LOG(GF_ERR, ERR_FAIL_ALLOCATE);
    return NULL;
  }

  unsigned long count = 0;
  for (unsigned long i = 0; i < *nitems; ++i) {
    Window window = windows[i];
    if (gf_x_excluded_window(display, window))
      continue;

    if (gf_x_window_in_workspace(display, window, workspace_id)) {
      filtered[count++] = window;
      // Update our tracking for this window
      gf_x_update_window_info(display, window, workspace_id);
    }
  }

  *nitems = count;
  return filtered;
}

static Window *gf_x_get_window_property_list(Display *display, Window root,
                                             Atom atom, unsigned long *nitems) {
  if (!display || !nitems)
    return NULL;

  int status;
  unsigned char *data =
      gf_x_get_window_property(display, root, atom, XA_WINDOW, nitems, &status);

  if (status != Success || !data || *nitems == 0) {
    if (data)
      XFree(data);
    return NULL;
  }

  return (Window *)data;
}

static Window *gf_x_fetch_window_list(Display *display, Window root,
                                      unsigned long *nitems, Atom atom,
                                      int workspace_id) {
  if (!display || !nitems)
    return NULL;

  Window *windows = gf_x_get_window_property_list(display, root, atom, nitems);
  if (!windows || *nitems == 0)
    return NULL;

  Window *filtered =
      gf_x_filter_windows(display, windows, nitems, workspace_id);
  XFree(windows);

  return filtered;
}

static unsigned long int gf_x_get_current_workspace(Display *display,
                                                    Window root) {
  unsigned long *desktop = NULL;
  Atom actualType;
  int actualFormat;
  unsigned long nItems, bytesAfter;

  if (XGetWindowProperty(display, root, atoms.net_curr_desktop, 0, 1, False,
                         XA_CARDINAL, &actualType, &actualFormat, &nItems,
                         &bytesAfter, (unsigned char **)&desktop) == Success &&
      desktop) {
    int workspaceNumber = (int)*desktop;
    XFree(desktop);
    return workspaceNumber;
  } else {
    LOG(GF_ERR, ERR_BAD_WINDOW);
    XCloseDisplay(display);
    return -1;
  }
}

static Window gf_x_get_last_opened_window(Display *display) {
  Atom actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  unsigned char *data = NULL;

  if (XGetWindowProperty(display, DefaultRootWindow(display),
                         atoms.client_list_stack, 0, (~0L), False, XA_WINDOW,
                         &actual_type, &actual_format, &nitems, &bytes_after,
                         &data) == Success &&
      data) {
    Window *stacking_list = (Window *)data;
    Window last_window = stacking_list[nitems - 1];
    XFree(data);
    return last_window;
  }

  return 0;
}

static unsigned long gf_x_get_total_workspace(Display *display, Window root) {
  Atom actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  unsigned char *data = NULL;
  unsigned long total_workspaces = 0;

  if (XGetWindowProperty(display, root, atoms.num_of_desktop, 0, 1, False,
                         XA_CARDINAL, &actual_type, &actual_format, &nitems,
                         &bytes_after, &data) == Success) {
    if (data) {
      total_workspaces = *(unsigned long *)data;
      XFree(data);
    } else {
      LOG(GF_ERR, ERR_BAD_WINDOW);
    }
  } else {
    LOG(GF_ERR, ERR_BAD_WINDOW);
  }

  return total_workspaces;
}

static int gf_x_get_total_window(Display *display, Window root) {
  return (int)g_gf_state.total_windows;
}

static void gf_x_arrange_dimension(Display *display, Window root,
                                   unsigned long base_win_items,
                                   Window *curr_win_open, int screen) {
  Bool needs_rearrange = False;
  int workspace_id = gf_x_get_current_workspace(display, root);

  for (unsigned long int i = 0; i < base_win_items; i++) {
    gf_win_node *node = gf_x_find_window_node(curr_win_open[i]);
    if (!node) {
      gf_x_update_window_info(display, curr_win_open[i], workspace_id);
      node = gf_x_find_window_node(curr_win_open[i]);
    }

    if (!node)
      continue;

    int current_width, current_height;
    gf_x_get_window_dimension(display, curr_win_open[i], &current_width,
                              &current_height, NULL, NULL);

    if (current_width != node->width || current_height != node->height ||
        node->needs_update) {
      node->width = current_width;
      node->height = current_height;
      node->last_modified = time(NULL);
      needs_rearrange = True;
    }
  }

  if (needs_rearrange || gf_x_workspace_needs_arrangement(workspace_id)) {
    LOG(GF_DBG, "Rearranging %lu windows in workspace %d", base_win_items,
        workspace_id);

    for (unsigned long int i = 0; i < base_win_items; i++) {
      gf_x_unmaximize_window(display, curr_win_open[i]);

      gf_win_node *node = gf_x_find_window_node(curr_win_open[i]);
      if (node)
        node->is_maximized = False;
    }

    gf_x_arrange_window(base_win_items, curr_win_open, display, screen);
    gf_x_clear_update_flags(workspace_id);
  }
}

static void gf_x_distribute_overflow_window(
    Display *display, int *overflow_workspace, int overflow_workspace_total,
    gf_workspace_info *free_workspace, int free_workspace_total,
    Atom window_list_atom, Window root, int screen) {
  for (int i = 0; i < overflow_workspace_total; i++) {
    unsigned long current_window_count = 0;
    Window *active_windows =
        gf_x_fetch_window_list(display, root, &current_window_count,
                               window_list_atom, overflow_workspace[i]);

    for (int j = 0; j <= free_workspace_total; j++) {
      for (int x = 0; x <= free_workspace[j].available_space; x++) {
        if (current_window_count >= MAX_WIN_OPEN) {
          gf_x_unmaximize_window(display,
                                 active_windows[current_window_count--]);
          gf_x_move_window_to_workspace(display,
                                        active_windows[current_window_count--],
                                        free_workspace[j].workspace_id);
          free_workspace[j].available_space--;
        }
      }
    }

    if (active_windows) {
      gf_x_arrange_window(current_window_count, active_windows, display,
                          screen);
    }
  }
}

static void gf_x_handle_window_overflow(Display *display, Window root,
                                        unsigned long *current_window_count,
                                        Atom window_list_atom,
                                        int total_workspace, int screen) {
  int overflow_workspace[total_workspace];
  int overflow_workspace_total = 0;

  gf_workspace_info free_workspace[total_workspace];
  int free_workspace_total = 0;

  for (int workspace = 0; workspace <= total_workspace; workspace++) {
    Window *active_windows = gf_x_fetch_window_list(
        display, root, current_window_count, window_list_atom, workspace);

    if (*current_window_count >= MAX_WIN_OPEN) {
      overflow_workspace[overflow_workspace_total] = workspace;
      overflow_workspace_total++;
    } else if (*current_window_count + 1 < MAX_WIN_OPEN) {
      int available_space = MAX_WIN_OPEN - *current_window_count;
      free_workspace[free_workspace_total].workspace_id = workspace;
      free_workspace[free_workspace_total].total_window_open =
          *current_window_count;
      free_workspace[free_workspace_total].available_space = available_space;
      free_workspace_total++;
    }
  }

  gf_x_distribute_overflow_window(
      display, overflow_workspace, overflow_workspace_total, free_workspace,
      free_workspace_total, window_list_atom, root, screen);
}

static void gf_x_manage_workspace_window(Display *display, Window root,
                                         unsigned long *previous_window_count,
                                         int total_workspace, int screen) {
  unsigned long current_window_count = 0;
  Window *active_windows;

  for (int workspace = 0; workspace < total_workspace; workspace++) {
    active_windows = gf_x_fetch_window_list(
        display, root, &current_window_count, atoms.client_list, workspace);
    if (!active_windows)
      continue;

    if (current_window_count > MAX_WIN_OPEN) {
      gf_x_handle_window_overflow(display, root, &current_window_count,
                                  atoms.client_list, total_workspace, screen);
    }
  }
}

static void
gf_x_rearrange_current_workspace(Display *display, Window root,
                                 unsigned long *previous_window_count,
                                 int screen) {
  unsigned long current_window_count = 0;
  int current_workspace = gf_x_get_current_workspace(display, root);

  // Check if workspace changed
  if (g_gf_state.active_workspace != current_workspace) {
    LOG(GF_DBG, "Workspace changed from %d to %d", g_gf_state.active_workspace,
        current_workspace);
    g_gf_state.active_workspace = current_workspace;
    gf_x_mark_windows_for_update(current_workspace);
  }

  Window *active_windows =
      gf_x_fetch_window_list(display, root, &current_window_count,
                             atoms.client_list, current_workspace);

  // Check if window count changed or if this is first run
  Bool count_changed = (current_window_count != *previous_window_count);
  Bool needs_arrangement =
      count_changed || gf_x_workspace_needs_arrangement(current_workspace);

  if (count_changed) {
    LOG(GF_DBG, "Window count changed: %lu -> %lu in workspace %d",
        *previous_window_count, current_window_count, current_workspace);
    *previous_window_count = current_window_count;
    gf_x_mark_windows_for_update(current_workspace);
  }

  if (needs_arrangement && active_windows && current_window_count > 0) {
    // Unmaximize all windows before arranging
    for (unsigned long i = 0; i < current_window_count; i++) {
      if (active_windows[i]) {
        gf_x_unmaximize_window(display, active_windows[i]);

        gf_win_node *node = gf_x_find_window_node(active_windows[i]);
        if (node) {
          node->is_maximized = False;
          node->last_modified = time(NULL);
        }
      }
    }

    gf_x_arrange_window(current_window_count, active_windows, display, screen);
    LOG(GF_DBG, "Arranged %lu windows in workspace %d", current_window_count,
        current_workspace);
  }

  // Always check dimensions for potential changes
  if (active_windows && current_window_count > 0) {
    gf_x_arrange_dimension(display, root, current_window_count, active_windows,
                           screen);
    XFree(active_windows);
  }
}

const char *gf_x_detect_desktop_environment() {
  const char *xdg_current_desktop = getenv("XDG_CURRENT_DESKTOP");
  const char *desktop_session = getenv("DESKTOP_SESSION");
  const char *kde_full_session = getenv("KDE_FULL_SESSION");
  const char *gnome_session_id = getenv("GNOME_DESKTOP_SESSION_ID");

  if (kde_full_session && strcmp(kde_full_session, "true") == 0) {
    return "KDE";
  } else if (gnome_session_id ||
             (xdg_current_desktop && strstr(xdg_current_desktop, "GNOME"))) {
    return "GNOME";
  } else if (xdg_current_desktop) {
    return xdg_current_desktop;
  } else if (desktop_session) {
    return desktop_session;
  } else {
    return "Unknown";
  }
}

static void gf_x_create_new_workspace(Display *display, Window root,
                                      unsigned long new_workspace) {
  XChangeProperty(display, root, atoms.net_curr_desktop, XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)&new_workspace, 1);
  XSync(display, False);
}

static void gf_x_set_workspace(Display *display, Window root,
                               unsigned long workspace) {
  const char *desktop_session = gf_x_detect_desktop_environment();
  if (desktop_session == NULL || strcmp(desktop_session, "Unknown") == 0) {
    LOG(GF_DBG, "No desktop session detected. Exiting.\n");
    return;
  }

  pid_t pid = fork();
  if (pid == -1) {
    perror("Failed to fork");
    return;
  }

  if (pid == 0) {
    if (strcmp(desktop_session, "KDE") == 0) {
      if (execlp("qdbus", "qdbus", "org.kde.KWin", "/VirtualDesktopManager",
                 "createDesktop", "1", "LittleWin", NULL) == -1) {
        perror("Error executing qdbus");
        _exit(EXIT_FAILURE);
      }
    } else if (strcmp(desktop_session, "GNOME") == 0) {
      if (execlp("gsettings", "gsettings", "set", "org.gnome.mutter",
                 "dynamic-workspaces", "true", NULL) == -1) {
        perror("Error executing gsettings");
        _exit(EXIT_FAILURE);
      }
      gf_x_create_new_workspace(display, root, workspace);
    } else {
      int status;
      waitpid(pid, &status, 0);
      if (WIFEXITED(status)) {
        LOG(GF_DBG, "command exited with status %d\n", WEXITSTATUS(status));
      } else {
        perror("command did not terminate normally\n");
        _exit(EXIT_FAILURE);
      }
    }
  }
}

static void gf_x_manage_window(Display *display, Window root,
                               unsigned long *previous_window_count,
                               int screen) {
  if (!display) {
    LOG(GF_WARN, ERR_DISPLAY_NULL);
    return;
  }

  // Periodic cleanup of invalid windows
  static int cleanup_counter = 0;
  if (++cleanup_counter % 50 == 0) { // Every ~1 second (50 * 20ms)
    gf_x_cleanup_invalid_windows(display);
    g_gf_state.last_scan_time = time(NULL);
  }

  unsigned long total_workspace = gf_x_get_total_workspace(display, root);

  // Manage workspace overflow
  gf_x_manage_workspace_window(display, root, previous_window_count,
                               total_workspace, screen);

  // Handle current workspace arrangement
  gf_x_rearrange_current_workspace(display, root, previous_window_count,
                                   screen);
}

// Additional utility functions for debugging and monitoring
void gf_x_print_window_stats() {
  int workspace_counts[32] = {0}; // Assume max 32 workspaces
  int max_workspace = -1;

  gf_win_node *current = g_gf_state.window_list;
  while (current) {
    if (current->workspace_id >= 0 && current->workspace_id < 32) {
      workspace_counts[current->workspace_id]++;
      if (current->workspace_id > max_workspace) {
        max_workspace = current->workspace_id;
      }
    }
    current = current->next;
  }

  LOG(GF_DBG, "Window distribution across workspaces (total: %lu):",
      g_gf_state.total_windows);
  for (int i = 0; i <= max_workspace; i++) {
    if (workspace_counts[i] > 0) {
      LOG(GF_DBG, "  Workspace %d: %d windows", i, workspace_counts[i]);
    }
  }
}

void gf_x_save_window_state_to_file(const char *filename) {
  FILE *fp = fopen(filename, "w");
  if (!fp) {
    LOG(GF_ERR, "Cannot open %s for writing", filename);
    return;
  }

  fprintf(fp, "# Window Manager State Dump\n");
  fprintf(fp, "# Total Windows: %lu\n", g_gf_state.total_windows);
  fprintf(fp, "# Active Workspace: %d\n", g_gf_state.active_workspace);
  fprintf(fp, "# Last Scan: %lu\n", g_gf_state.last_scan_time);
  fprintf(fp, "\n");

  gf_win_node *current = g_gf_state.window_list;
  while (current) {
    fprintf(fp, "Window: %lu\n", current->window);
    fprintf(fp, "  Workspace: %d\n", current->workspace_id);
    fprintf(fp, "  Geometry: %dx%d+%d+%d\n", current->width, current->height,
            current->x, current->y);
    fprintf(fp, "  Maximized: %s\n", current->is_maximized ? "yes" : "no");
    fprintf(fp, "  Last Modified: %lu\n", current->last_modified);
    fprintf(fp, "  Needs Update: %s\n", current->needs_update ? "yes" : "no");
    fprintf(fp, "\n");
    current = current->next;
  }

  fclose(fp);
  LOG(GF_DBG, "Window state saved to %s", filename);
}

void gf_x_initialize_state(Display *display, Window root) {
  g_gf_state.last_scan_time = time(NULL);
  g_gf_state.active_workspace = gf_x_get_current_workspace(display, root);
}

void gf_x_setup_initial_windows(Display *display, Window root, int screen) {
  unsigned long base_win_items = 0;
  Window *windows =
      gf_x_fetch_window_list(display, root, &base_win_items, atoms.client_list,
                             g_gf_state.active_workspace);

  if (!windows)
    return;

  LOG(GF_DBG, "Initial setup: found %lu windows in workspace %d",
      base_win_items, g_gf_state.active_workspace);

  for (unsigned long i = 0; i < base_win_items; i++) {
    gf_x_unmaximize_window(display, windows[i]);
    gf_x_update_window_info(display, windows[i], g_gf_state.active_workspace);
  }

  if (base_win_items > 0) {
    gf_x_arrange_window(base_win_items, windows, display, screen);
  }

  XFree(windows);
}

void gf_x_manage_workspace_expansion(Display *display, Window root) {
  int total_workspaces = gf_x_get_total_workspace(display, root);
  unsigned long total_windows = gf_x_get_total_window(display, root);
  int required_workspaces = (int)total_windows / MAX_WIN_OPEN;

  if (total_workspaces <= required_workspaces) {
    LOG(GF_DBG, "Creating new workspace: need %d, have %d", required_workspaces,
        total_workspaces);
    gf_x_set_workspace(display, root, required_workspaces);
    usleep(20000);
  }
}

void gf_x_main_event_loop(Display *display, Window root, int screen) {
  int loop_counter = 0;
  time_t last_stats_time = time(NULL);
  time_t last_state_save = time(NULL);
  unsigned long base_win_items = 0;

  LOG(GF_DBG, "Starting main event loop");

  while (1) {
    loop_counter++;
    time_t current_time = time(NULL);

    if (current_time - last_stats_time >= 10) {
      gf_x_print_window_stats();
      last_stats_time = current_time;
    }

    if (current_time - last_state_save >= 300) {
      gf_x_save_window_state_to_file("/tmp/gf_state.dump");
      last_state_save = current_time;
    }

    gf_x_manage_workspace_expansion(display, root);
    gf_x_manage_window(display, root, &base_win_items, screen);

    usleep(g_gf_state.total_windows > 10 ? 15000 : 20000);
  }
}

void gf_x_cleanup_state() {
  LOG(GF_DBG, "Cleaning up window manager state");
  gf_win_node *current = g_gf_state.window_list;
  while (current) {
    gf_win_node *next = current->next;
    free(current);
    current = next;
  }
  g_gf_state.window_list = NULL;
  g_gf_state.total_windows = 0;
}

void gf_x_run_layout() {
  Display *display = gf_x_initialize_display();
  if (!display) {
    LOG(GF_ERR, ERR_DISPLAY_NULL);
    return;
  }

  XSetErrorHandler(gf_x_error_handler);
  gf_x_init_atom(display);

  int screen = DefaultScreen(display);
  Window root = gf_x_get_root_window(display);

  gf_x_initialize_state(display, root);
  gf_x_setup_initial_windows(display, root, screen);

  gf_x_main_event_loop(display, root, screen);

  gf_x_cleanup_state();
  XCloseDisplay(display);
}
