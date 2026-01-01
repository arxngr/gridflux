#pragma once

#include "window_manager.h"

static inline gf_platform_interface_t *
wm_platform (gf_window_manager_t *m)
{
    return m->platform;
}

static inline gf_display_t *
wm_display (gf_window_manager_t *m)
{
    return &m->display;
}

static inline gf_window_list_t *
wm_windows (gf_window_manager_t *m)
{
    return &m->state.windows;
}

static inline gf_workspace_list_t *
wm_workspaces (gf_window_manager_t *m)
{
    return &m->state.workspaces;
}

static inline gf_geometry_calculator_t *
wm_geometry (gf_window_manager_t *m)
{
    return m->geometry_calc;
}

static inline bool
wm_is_valid (gf_window_manager_t *m, gf_native_window_t w)
{
    gf_platform_interface_t *p = wm_platform (m);
    return !p->is_window_valid || p->is_window_valid (*wm_display (m), w);
}

static inline bool
wm_is_excluded (gf_window_manager_t *m, gf_native_window_t w)
{
    gf_platform_interface_t *p = wm_platform (m);
    return p->is_window_excluded && p->is_window_excluded (*wm_display (m), w);
}
