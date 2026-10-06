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

// Per-window cache of the resolved class|exe, so wm_user_excluded avoids the
// platform class+exe lookup (OpenProcess) on every tick. TTL is in loop
// iterations; a window's class is immutable so this only bounds handle reuse.
#define GF_EXCLUDE_CACHE_SIZE 128
#define GF_EXCLUDE_CACHE_TTL 120

typedef struct
{
    gf_handle_t id;
    uint64_t stamp;
    char name[256];
} gf_exclude_cache_entry_t;

bool wm_user_excluded (gf_wm_t *m, gf_handle_t w);

// Platform (system) exclusion only: windows GridFlux never manages (shell, its
// own GUI, tool windows). User exclusions are handled separately by parking the
// window on the excluded workspace, so they stay tracked/managed.
static inline bool
wm_is_excluded (gf_wm_t *m, gf_handle_t w)
{
    gf_platform_t *p = wm_platform (m);
    return p->window_is_excluded && p->window_is_excluded (*wm_display (m), w);
}

/* --- Workspace Management --- */
gf_ws_id_t wm_assign_window_workspace (gf_wm_t *m, gf_win_info_t *win,
                                       gf_ws_info_t *current_ws);
void wm_assign_windows_to_workspaces (gf_wm_t *m);
void wm_cleanup_empty_maximized_ws (gf_wm_t *m, gf_ws_id_t ws_id);
void wm_cleanup_unused_workspace (gf_wm_t *m, gf_ws_list_t *list, uint32_t index);
gf_ws_id_t wm_lookup_or_create_maximized_ws (gf_wm_t *m, gf_monitor_id_t monitor_id);
gf_ws_id_t wm_lookup_or_create_ws (gf_wm_t *m);
gf_ws_id_t wm_lookup_or_create_ws_for_monitor (gf_wm_t *m, gf_monitor_id_t monitor_id);
uint32_t wm_workspace_monitor_window_count (gf_wm_t *m, gf_ws_id_t workspace_id,
                                            gf_monitor_id_t monitor_id);
gf_ws_info_t *wm_find_workspace (gf_ws_list_t *workspaces, gf_ws_id_t id);
void wm_switch_workspace (gf_wm_t *m, gf_ws_id_t current_workspace,
                          gf_monitor_id_t monitor_id);
void wm_sync_dock_visibility (gf_wm_t *m);
void wm_recount_workspace_windows (gf_wm_t *m, gf_ws_list_t *workspaces,
                                   gf_win_list_t *windows, uint32_t max_per_ws);
void wm_sync_workspaces (gf_wm_t *m);
bool wm_ws_has_capacity (gf_ws_info_t *ws, uint32_t max_per_ws);
bool wm_ws_is_valid (gf_ws_list_t *workspaces, gf_ws_id_t id);
gf_ws_id_t wm_lookup_or_create_excluded_ws (gf_wm_t *m, gf_monitor_id_t monitor_id);
void wm_reconcile_excluded_windows (gf_wm_t *m);

/* --- Window Management --- */
void wm_detect_minimize_changes (gf_wm_t *m, gf_ws_id_t current_workspace,
                                 gf_monitor_id_t monitor_id);
int wm_find_maximized_ws_index (gf_win_info_t *windows, uint32_t count,
                                gf_handle_t handle);
uint32_t wm_find_maximized_windows (gf_wm_t *m, gf_win_info_t **out_windows);
gf_monitor_id_t wm_find_active_monitor (gf_wm_t *m);
void wm_enforce_fullscreen (gf_wm_t *m);
void wm_sync_monitor_activity (gf_wm_t *m, gf_monitor_id_t active_monitor);
gf_err_t wm_request_visibility (gf_wm_t *m, gf_win_info_t *win, bool minimized);
void wm_observe_window_state (gf_wm_t *m, gf_win_info_t *win, bool *minimized,
                              bool *maximized);
void wm_request_maximized (gf_wm_t *m, gf_win_info_t *win);
void wm_register_new_window (gf_wm_t *m, gf_win_info_t *win, gf_ws_info_t *current_ws);
void wm_minimize_workspace_windows (gf_wm_t *m, gf_ws_id_t ws_id, gf_handle_t exclude_id,
                                    gf_monitor_id_t active_monitor);
void wm_move_window_to_workspace (gf_wm_t *m, gf_win_info_t *win, gf_ws_id_t new_ws_id);
void wm_move_window_to_monitor (gf_wm_t *m, gf_win_info_t *win,
                                gf_monitor_id_t new_monitor);
void wm_restore_workspace_windows (gf_wm_t *m, gf_ws_id_t ws_id,
                                   gf_handle_t active_window,
                                   gf_monitor_id_t active_monitor);
bool wm_win_has_assigned_workspace (gf_win_info_t *win, gf_ws_list_t *workspaces);

/* --- Layout & Rendering --- */
void gf_wm_apply_layout (gf_wm_t *m, gf_win_info_t *windows, gf_rect_t *geometry,
                         uint32_t window_count);
gf_err_t gf_wm_calculate_layout (gf_wm_t *m, gf_win_info_t *windows,
                                 uint32_t window_count, gf_monitor_id_t mon_id,
                                 gf_rect_t **out_geometries);
gf_err_t gf_wm_layout_rebalance (gf_wm_t *m);

/* --- Misc & Debugging --- */
void gf_wm_keymap_event (gf_wm_t *m);
void wm_print_window_info (uint32_t window_id, const char *name);
void wm_print_workspace_header (gf_ws_id_t id, bool is_locked, uint32_t count,
                                uint32_t max_windows, int32_t available);

#endif /* GF_CORE_INTERNAL_H */
