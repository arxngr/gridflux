#ifndef GF_PLATFORM_X11_WINDOW_MANAGER_H
#define GF_PLATFORM_X11_WINDOW_MANAGER_H

#ifdef GF_PLATFORM_X11

#include "../../core/interfaces.h"
#include "x11_atoms.h"

// X11 platform data
typedef struct {
  gf_x11_atoms_t atoms;
  int screen;
  Window root_window;
} gf_x11_platform_data_t;

// X11 platform interface
gf_platform_interface_t *gf_x11_platform_create(void);
void gf_x11_platform_destroy(gf_platform_interface_t *platform);

// X11 utility functions
gf_error_code_t gf_x11_get_window_property(Display *display, Window window,
                                           Atom property, Atom type,
                                           unsigned char **data,
                                           unsigned long *nitems);
gf_error_code_t gf_x11_send_client_message(Display *display, Window window,
                                           Atom message_type, long *data,
                                           int count);
bool gf_x11_window_has_state(Display *display, Window window, Atom state);
gf_error_code_t gf_x11_get_frame_extents(Display *display, Window window,
                                         int *left, int *right, int *top,
                                         int *bottom);

const char *gf_x11_detect_desktop_environment(void);

#endif // GF_PLATFORM_X11

#endif // GF_PLATFORM_X11_WINDOW_MANAGER_H
