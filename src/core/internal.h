#ifndef GF_CORE_INTERNAL_H
#define GF_CORE_INTERNAL_H

#include "layout.h"
#include "wm.h"

static inline gf_platform_t *
wm_platform (gf_wm_t *m)
{
    return m->platform;
}

static inline gf_display_t *
wm_display (gf_wm_t *m)
{
    return &m->display;
}

static inline gf_win_list_t *
wm_windows (gf_wm_t *m)
{
    return &m->state.windows;
}

static inline gf_ws_list_t *
wm_workspaces (gf_wm_t *m)
{
    return &m->state.workspaces;
}

static inline gf_layout_engine_t *
wm_geometry (gf_wm_t *m)
{
    return m->layout;
}

static inline bool
wm_is_valid (gf_wm_t *m, gf_handle_t w)
{
    gf_platform_t *p = wm_platform (m);
    return !p->window_is_valid || p->window_is_valid (*wm_display (m), w);
}

static inline bool
wm_is_excluded (gf_wm_t *m, gf_handle_t w)
{
    gf_platform_t *p = wm_platform (m);
    return p->window_is_excluded && p->window_is_excluded (*wm_display (m), w);
}

/* --- Workspace Management --- */
gf_ws_id_t assign_window_workspace (gf_wm_t *m, gf_win_info_t *win,
                                    gf_ws_info_t *current_ws);
void assign_windows_to_workspaces (gf_wm_t *m);
void cleanup_empty_maximized_ws (gf_wm_t *m, gf_ws_id_t ws_id);
void cleanup_unused_workspace (gf_wm_t *m, gf_ws_list_t *list, uint32_t index);
gf_ws_id_t lookup_or_create_maximized_ws (gf_wm_t *m);
gf_ws_id_t lookup_or_create_ws (gf_wm_t *m);
gf_ws_info_t *find_workspace (gf_ws_list_t *workspaces, gf_ws_id_t id);
void switch_workspace (gf_wm_t *m, gf_ws_id_t current_workspace);
void recount_workspace_windows (gf_wm_t *m, gf_ws_list_t *workspaces,
                                gf_win_list_t *windows, uint32_t max_per_ws);
void sync_workspaces (gf_wm_t *m);
bool ws_has_capacity (gf_ws_info_t *ws, uint32_t max_per_ws);
bool ws_is_valid (gf_ws_list_t *workspaces, gf_ws_id_t id);

/* --- Window Management --- */
void detect_minimize_changes (gf_wm_t *m, gf_ws_id_t current_workspace);
int find_maximized_ws_index (gf_win_info_t *windows, uint32_t count, gf_handle_t handle);
uint32_t find_maximized_windows (gf_wm_t *m, gf_win_info_t **out_windows);
gf_monitor_id_t find_active_monitor (gf_wm_t *m);
void enforce_fullscreen (gf_wm_t *m);
void register_new_window (gf_wm_t *m, gf_win_info_t *win, gf_ws_info_t *current_ws);
void minimize_workspace_windows (gf_wm_t *m, gf_ws_id_t ws_id, gf_handle_t exclude_id,
                                 gf_monitor_id_t active_monitor);
void move_window_to_workspace (gf_wm_t *m, gf_win_info_t *win, gf_ws_id_t new_ws_id);
void restore_workspace_windows (gf_wm_t *m, gf_ws_id_t ws_id, gf_handle_t active_window,
                                gf_monitor_id_t active_monitor);
bool win_has_assigned_workspace (gf_win_info_t *win, gf_ws_list_t *workspaces);

/* --- Layout & Rendering --- */
void gf_wm_apply_layout (gf_wm_t *m, gf_win_info_t *windows, gf_rect_t *geometry,
                         uint32_t window_count);
gf_err_t gf_wm_calculate_layout (gf_wm_t *m, gf_win_info_t *windows,
                                 uint32_t window_count, gf_monitor_id_t mon_id,
                                 gf_rect_t **out_geometries);
gf_err_t gf_wm_layout_rebalance (gf_wm_t *m);

/* --- Misc & Debugging --- */
void gf_wm_keymap_event (gf_wm_t *m);
void print_window_info (uint32_t window_id, const char *name);
void print_workspace_header (gf_ws_id_t id, bool is_locked, uint32_t count,
                             uint32_t max_windows, int32_t available);

#endif /* GF_CORE_INTERNAL_H */
