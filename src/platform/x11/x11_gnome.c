#include "core/config.h"
#include "core/types.h"
#include "platform/x11/x11_backend.h"
#include "platform/x11/x11_window_manager.h"

gf_error_code_t
gf_x11_gnome_get_screen_bounds (gf_display_t dpy, gf_rect_t *bounds)
{
    if (!dpy || !bounds)
        return GF_ERROR_INVALID_PARAMETER;
    Screen *scr = ScreenOfDisplay (dpy, DefaultScreen (dpy));
    bounds->x = 0;
    bounds->y = 0;
    bounds->width = scr->width;
    bounds->height = scr->height;
    return GF_SUCCESS;
}

gf_error_code_t
gf_x11_gnome_set_window_geometry (gf_display_t dpy, gf_native_window_t win,
                                  const gf_rect_t *geometry, gf_geometry_flags_t flags,
                                  gf_config_t *cfg)
{
    if (!dpy || !geometry)
        return GF_ERROR_INVALID_PARAMETER;

    int left = 0, right = 0, top = 0, bottom = 0;
    gf_x11_get_frame_extents (dpy, win, &left, &right, &top, &bottom);

    gf_rect_t final = *geometry;
    if (flags & GF_GEOMETRY_APPLY_PADDING && cfg)
        gf_rect_apply_padding (&final, cfg->default_padding);

    long fx = final.x - left;
    long fy = final.y - top;
    long fw = (long) final.width + left + right;
    long fh = (long) final.height + top + bottom;

    if (cfg)
    {
        if (fw < (long)cfg->min_window_size)
            fw = cfg->min_window_size;
        if (fh < (long)cfg->min_window_size)
            fh = cfg->min_window_size;
    }

    XMoveResizeWindow (dpy, win, fx, fy, fw, fh);
    XFlush (dpy);
    return GF_SUCCESS;
}
