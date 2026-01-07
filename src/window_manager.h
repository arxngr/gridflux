#ifndef GF_CORE_WINDOW_MANAGER_H
#define GF_CORE_WINDOW_MANAGER_H

#include "layout.h"
#define GLOB_CFG "config.json"

#include "config.h"
#include "ipc.h"
#include "list.h"
#include "platform/platform.h"

typedef struct
{
    gf_window_list_t windows;
    gf_workspace_list_t workspaces;
    time_t last_scan_time;
    time_t last_cleanup_time;
    uint32_t loop_counter;
    gf_window_id_t last_active_window;
    gf_workspace_id_t last_active_workspace;
    bool initialized;
} gf_window_manager_state_t;

typedef struct
{
    gf_window_manager_state_t state;
    gf_platform_interface_t *platform;
    gf_layout_engine_t *layout;
    gf_display_t display;
    gf_config_t *config;
    gf_ipc_handle_t ipc_handle;
} gf_window_manager_t;

gf_error_code_t gf_window_manager_create (gf_window_manager_t **manager,
                                          gf_platform_interface_t *platform,
                                          gf_layout_engine_t *layout);
void gf_window_manager_destroy (gf_window_manager_t *manager);

gf_error_code_t gf_window_manager_init (gf_window_manager_t *manager);
void gf_window_manager_cleanup (gf_window_manager_t *manager);

gf_error_code_t gf_window_manager_run (gf_window_manager_t *manager);
gf_error_code_t gf_window_manager_arrange_workspace (gf_window_manager_t *manager);

// Window management
gf_error_code_t gf_window_manager_update_window_info (gf_window_manager_t *manager,
                                                      gf_native_window_t window,
                                                      gf_workspace_id_t workspace_id);
void gf_window_manager_cleanup_invalid_windows (gf_window_manager_t *manager);
void gf_window_manager_print_stats (const gf_window_manager_t *manager);
gf_error_code_t gf_window_manager_swap (gf_window_manager_t *manager,
                                        const gf_window_info_t *src_copy,
                                        const gf_window_info_t *dst_copy);
void gf_window_manager_load_cfg (gf_window_manager_t *manager);
void gf_window_manager_event (gf_window_manager_t *manager);
static gf_error_code_t gf_window_manager_calculate_layout (gf_window_manager_t *m,
                                                           gf_window_info_t *windows,
                                                           uint32_t window_count,
                                                           gf_rect_t **out_geometries);
static void gf_window_manager_unmaximize_all (gf_window_manager_t *m,
                                              gf_window_info_t *windows,
                                              uint32_t window_count);
static void gf_window_manager_apply_layout (gf_window_manager_t *m,
                                            gf_window_info_t *windows,
                                            gf_rect_t *geometry, uint32_t window_count);
static gf_error_code_t gf_window_manager_arrange_overflow (gf_window_manager_t *m);
static void gf_window_manager_watch (gf_window_manager_t *m);
void gf_window_manager_get_window_name (const gf_window_manager_t *m,
                                        gf_native_window_t handle, char *buffer,
                                        size_t size);
gf_error_code_t gf_window_manager_move_window (gf_window_manager_t *m,
                                               gf_window_id_t window_id,
                                               gf_workspace_id_t target_workspace);

gf_error_code_t gf_window_manager_lock_workspace (gf_window_manager_t *m,
                                                  gf_workspace_id_t workspace_id);

gf_error_code_t gf_window_manager_unlock_workspace (gf_window_manager_t *m,
                                                    gf_workspace_id_t workspace_id);
#endif // GF_CORE_WINDOW_MANAGER_H
