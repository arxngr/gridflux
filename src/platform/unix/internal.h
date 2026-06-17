#ifndef GF_PLATFORM_UNIX_INTERNAL_H
#define GF_PLATFORM_UNIX_INTERNAL_H

#include "../../core/types.h"
#include "platform.h"
#include <X11/Xatom.h>

/* --- Shell & Background --- */
void run_cmd_sync (const char *cmd, char *const argv[]);

/* --- Window Identification & State --- */
bool window_is_app_exception (gf_display_t display, gf_handle_t window);
bool query_window_info (Display *display, Window window, gf_platform_atoms_t *atoms,
                        gf_ws_id_t *workspace_id, gf_win_info_t *info);
bool window_has_excluded_state (gf_display_t display, gf_handle_t window);
bool window_has_excluded_type (gf_display_t display, gf_handle_t window);
bool window_has_type (gf_display_t display, gf_handle_t window, Atom type);
bool window_name_matches (const char *name, const char *list[], size_t count);
bool window_is_self (gf_display_t display, gf_handle_t window);
bool window_is_border_excluded (gf_display_t display, gf_handle_t window);
void gf_window_get_class (gf_display_t dpy, gf_handle_t win, char *buffer,
                           size_t bufsize);

/* --- Border Management --- */
void gf_border_add (gf_platform_t *platform, gf_handle_t window, gf_color_t color,
                    int thickness);
void gf_border_cleanup (gf_platform_t *platform);
void gf_border_remove (gf_platform_t *platform, gf_handle_t window);
void gf_border_update (gf_platform_t *platform, const gf_config_t *config);

/* --- Geometry & Layout --- */
bool get_frame_geometry (Display *dpy, Window target, gf_rect_t *frame_rect);
gf_err_t remove_size_constraints (Display *dpy, Window win);

/* --- Border Rendering (X11 Shape) --- */
void apply_shape_mask (Display *dpy, Window overlay, int w, int h, int thickness,
                       int frame_w, int frame_h, const XRectangle *sub_rects,
                       int sub_count);
Window create_border_overlay (Display *dpy, Window target, gf_color_t color,
                              int thickness);
void resize_border_overlay (Display *dpy, gf_border_t *b, const gf_rect_t *frame);

#endif /* GF_PLATFORM_UNIX_INTERNAL_H */
