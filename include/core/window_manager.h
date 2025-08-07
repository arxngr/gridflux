#ifndef GF_CORE_WINDOW_MANAGER_H
#define GF_CORE_WINDOW_MANAGER_H

#include "../utils/collections.h"
#include "interfaces.h"
#include "workspace.h"

// Main window manager state
typedef struct {
  gf_window_list_t windows;
  gf_workspace_manager_t *workspace_manager;
  time_t last_scan_time;
  time_t last_cleanup_time;
  uint32_t loop_counter;
  bool initialized;
} gf_window_manager_state_t;

// Window manager structure
typedef struct {
  gf_window_manager_state_t state;
  gf_platform_interface_t *platform;
  gf_geometry_calculator_t *geometry_calc;
  gf_window_filter_t *window_filter;
  gf_display_t display;
} gf_window_manager_t;

gf_error_code_t
gf_window_manager_create(gf_window_manager_t **manager,
                         gf_platform_interface_t *platform,
                         gf_geometry_calculator_t *geometry_calc);
void gf_window_manager_destroy(gf_window_manager_t *manager);

gf_error_code_t gf_window_manager_init(gf_window_manager_t *manager);
void gf_window_manager_cleanup(gf_window_manager_t *manager);

gf_error_code_t gf_window_manager_run(gf_window_manager_t *manager);
gf_error_code_t
gf_window_manager_arrange_workspace(gf_window_manager_t *manager,
                                    gf_workspace_id_t workspace_id);

// Window management
gf_error_code_t
gf_window_manager_update_window_info(gf_window_manager_t *manager,
                                     gf_native_window_t window,
                                     gf_workspace_id_t workspace_id);
void gf_window_manager_cleanup_invalid_windows(gf_window_manager_t *manager);
void gf_window_manager_print_stats(const gf_window_manager_t *manager);

#endif // GF_CORE_WINDOW_MANAGER_H
