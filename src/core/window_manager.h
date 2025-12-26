#ifndef GF_CORE_WINDOW_MANAGER_H
#define GF_CORE_WINDOW_MANAGER_H

#define GLOB_CFG "config.json"

#include "../utils/list.h"
#include "core/config.h"
#include "interfaces.h"
#include "workspace.h"

// Main window manager state
typedef struct
{
    gf_window_list_t windows;
    gf_workspace_manager_t *workspace_manager;
    time_t last_scan_time;
    time_t last_cleanup_time;
    uint32_t loop_counter;
    bool initialized;
} gf_window_manager_state_t;

// Window manager structure
typedef struct
{
    gf_window_manager_state_t state;
    gf_platform_interface_t *platform;
    gf_geometry_calculator_t *geometry_calc;
    gf_window_filter_t *window_filter;
    gf_display_t display;
    gf_config_t *config;
} gf_window_manager_t;

gf_error_code_t gf_window_manager_create (gf_window_manager_t **manager,
                                          gf_platform_interface_t *platform,
                                          gf_geometry_calculator_t *geometry_calc);
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
static gf_error_code_t gf_window_manager_calculate_layout (gf_window_manager_t *manager,
                                                           gf_window_info_t *windows,
                                                           uint32_t window_count,
                                                           gf_rect_t **out_geometries);
static void gf_window_manager_apply_layout (gf_window_manager_t *manager,
                                            gf_window_info_t *windows,
                                            gf_rect_t *geometry, uint32_t window_count);

static void gf_window_manager_unmaximize_all (gf_window_manager_t *manager,
                                              gf_window_info_t *windows,
                                              uint32_t window_count);
static gf_error_code_t gf_window_manager_arrange_overflow (gf_window_manager_t *manager);
static void gf_window_manager_watch (gf_window_manager_t *manager);
gf_error_code_t gf_window_manager_drag (gf_window_manager_t *manager);
void gf_window_manager_load_cfg (gf_window_manager_t *manager);
void gf_window_manager_event (gf_window_manager_t *manager);
#endif // GF_CORE_WINDOW_MANAGER_H
