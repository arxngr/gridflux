#include "../../core/types.h"
#include "x11_atoms.h"
#include "x11_window_manager.h"
#include <X11/X.h>
#include <X11/Xatom.h>
#include <platform/x11/x11_backend.h>

gf_error_code_t
gf_x11_kde_get_screen_bounds (gf_display_t dpy, gf_rect_t *bounds)
{
    return GF_SUCCESS; // handled by kwin via dbus
}

gf_error_code_t
gf_x11_kde_set_window_geometry (gf_display_t dpy, gf_native_window_t win,
                                const gf_rect_t *geometry, gf_geometry_flags_t flags,
                                gf_config_t *cfg)
{
    return GF_SUCCESS; // handled by kwin via dbus
}
