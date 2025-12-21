#ifndef GF_X11_BACKEND_H
#define GF_X11_BACKEND_H

#include "../../core/config.h"
#include "../../core/types.h"
#include <X11/Xlib.h>
#include <stdbool.h>

typedef enum gf_desktop_env_t
{
    GF_DE_UNKNOWN = 0,
    GF_DE_KDE,
    GF_DE_GNOME,
    GF_DE_XFCE,
    GF_DE_LXDE,
    GF_DE_LXQT
} gf_desktop_env_t;

typedef enum
{
    GF_BACKEND_X11,
    GF_BACKEND_KWIN_QML,
    GF_BACKEND_AUTO
} gf_backend_type_t;

typedef struct
{
    gf_error_code_t (*get_screen_bounds) (gf_display_t, gf_rect_t *);
    gf_error_code_t (*set_window_geometry) (gf_display_t, gf_native_window_t,
                                            const gf_rect_t *, gf_geometry_flags_t,
                                            gf_config_t *);
} gf_x11_platform_backend_t;

/**
 * @brief Detects current desktop environment (KDE, GNOME, XFCE, etc.)
 */
gf_desktop_env_t gf_detect_desktop_env (void);

// KDE
gf_error_code_t gf_x11_get_screen_bounds (gf_display_t dpy, gf_rect_t *bounds);

gf_error_code_t gf_x11_set_window_geometry (gf_display_t display,
                                            gf_native_window_t window,
                                            const gf_rect_t *geometry,
                                            gf_geometry_flags_t flags, gf_config_t *cfg);

// GNOME
gf_error_code_t gf_x11_gnome_get_screen_bounds (gf_display_t dpy, gf_rect_t *bounds);

gf_error_code_t gf_x11_gnome_set_window_geometry (gf_display_t display,
                                                  gf_native_window_t window,
                                                  const gf_rect_t *geometry,
                                                  gf_geometry_flags_t flags,
                                                  gf_config_t *cfg);

gf_error_code_t gf_x11_kde_get_screen_bounds (gf_display_t display, gf_rect_t *bounds);

gf_error_code_t gf_x11_kde_set_window_geometry (gf_display_t display,
                                                gf_native_window_t window,
                                                const gf_rect_t *geometry,
                                                gf_geometry_flags_t flags,
                                                gf_config_t *cfg);

gf_backend_type_t gf_detect_backend (void);
#endif
