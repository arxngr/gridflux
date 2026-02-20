#ifndef GF_CORE_WINDOW_MANAGER_H
#define GF_CORE_WINDOW_MANAGER_H

#include "layout.h"
#define GLOB_CFG "config.json"

#include "../config/config.h"
#include "../ipc/ipc.h"
#include "../platform/platform.h"
#include "../utils/list.h"

typedef struct
{
    gf_win_list_t windows;
    gf_ws_list_t workspaces;
    time_t last_scan_time;
    time_t last_cleanup_time;
    uint32_t loop_counter;
    gf_handle_t last_active_window;
    gf_ws_id_t last_active_workspace;
    bool initialized;
    bool dock_hidden;
    bool gesture_initialized;
} gf_wm_state_t;

typedef struct
{
    gf_wm_state_t state;
    gf_platform_t *platform;
    gf_layout_engine_t *layout;
    gf_display_t display;
    gf_config_t *config;
    gf_ipc_handle_t ipc_handle;
} gf_wm_t;

// --- Lifecycle & Initialization ---
void gf_wm_cleanup (gf_wm_t *manager);
gf_err_t gf_wm_create (gf_wm_t **manager, gf_platform_t *platform,
                       gf_layout_engine_t *layout);
void gf_wm_destroy (gf_wm_t *manager);
gf_err_t gf_wm_init (gf_wm_t *manager);
void gf_wm_init_window_list (gf_wm_t *m);
void gf_wm_load_cfg (gf_wm_t *manager);
gf_err_t gf_wm_run (gf_wm_t *manager);

// --- Event Handling ---
void gf_wm_event (gf_wm_t *manager);
void gf_wm_watch (gf_wm_t *m);
void gf_wm_gesture_event (gf_wm_t *m);

// --- Layout Management ---
void gf_wm_apply_layout (gf_wm_t *m, gf_win_info_t *windows, gf_rect_t *geometry,
                         uint32_t window_count);
gf_err_t gf_wm_calculate_layout (gf_wm_t *m, gf_win_info_t *windows,
                                 uint32_t window_count, gf_rect_t **out_geometries);
gf_err_t gf_wm_layout_apply (gf_wm_t *manager);
gf_err_t gf_wm_layout_rebalance (gf_wm_t *m);

// --- Window Management ---
void gf_wm_prune (gf_wm_t *manager);
gf_err_t gf_wm_window_move (gf_wm_t *m, gf_handle_t window_id,
                            gf_ws_id_t target_workspace);
void gf_wm_window_name (const gf_wm_t *m, gf_handle_t handle, char *buffer, size_t size);
gf_err_t gf_wm_window_sync (gf_wm_t *manager, gf_handle_t window,
                            gf_ws_id_t workspace_id);

// --- Workspace Management ---
gf_err_t gf_wm_workspace_lock (gf_wm_t *m, gf_ws_id_t workspace_id);
gf_err_t gf_wm_workspace_unlock (gf_wm_t *m, gf_ws_id_t workspace_id);

// --- Debugging ---
void gf_wm_debug_stats (const gf_wm_t *manager);

#endif // GF_CORE_WINDOW_MANAGER_H
