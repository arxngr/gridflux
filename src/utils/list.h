#ifndef GF_UTILS_LISTS_H
#define GF_UTILS_LISTS_H

#include "../core/types.h"

typedef struct
{
    gf_win_info_t *items;
    uint32_t count;
    uint32_t capacity;
} gf_win_list_t;

typedef struct
{
    gf_ws_info_t *items;
    uint32_t count;
    uint32_t capacity;
    uint32_t active_workspace;
} gf_ws_list_t;

// --- Window List Operations ---
gf_err_t gf_window_list_add (gf_win_list_t *list, const gf_win_info_t *window);
void gf_window_list_cleanup (gf_win_list_t *list);
void gf_window_list_clear_update_flags (gf_win_list_t *list, gf_ws_id_t workspace_id);
uint32_t gf_window_list_count_by_workspace (const gf_win_list_t *list, gf_ws_id_t workspace_id);
gf_win_info_t *gf_window_list_find_by_window_id (const gf_win_list_t *list, gf_handle_t window_id);
gf_err_t gf_window_list_get_by_workspace (const gf_win_list_t *list, gf_ws_id_t workspace_id, gf_win_info_t **windows, uint32_t *count);
gf_err_t gf_window_list_init (gf_win_list_t *list, uint32_t initial_capacity);
void gf_window_list_mark_all_needs_update (gf_win_list_t *list, const gf_ws_id_t *workspace_id);
gf_err_t gf_window_list_remove (gf_win_list_t *list, gf_handle_t window_id);
gf_err_t gf_window_list_update (gf_win_list_t *list, const gf_win_info_t *window);

// --- Workspace List Operations ---
gf_ws_id_t gf_workspace_create (gf_ws_list_t *ws, uint32_t max_win_per_ws, bool maximized_state, bool is_locked);
bool gf_workspace_list_add_window (gf_ws_info_t *ws, gf_win_list_t *windows, gf_handle_t win_id);
gf_err_t gf_workspace_list_add (gf_ws_list_t *list, const gf_ws_info_t *workspace);
uint32_t gf_workspace_list_calc_required_workspaces (uint32_t total_windows, uint32_t current_workspaces, uint32_t max_per_workspace);
void gf_workspace_list_cleanup (gf_ws_list_t *list);
void gf_workspace_list_ensure (gf_ws_list_t *ws, gf_ws_id_t ws_id, uint32_t max_per_ws);
gf_ws_id_t gf_workspace_list_find_free (gf_ws_list_t *ws);
gf_ws_info_t *gf_workspace_list_find_by_id (const gf_ws_list_t *list, gf_ws_id_t workspace_id);
gf_ws_info_t *gf_workspace_list_get_current (gf_ws_list_t *ws);
gf_err_t gf_workspace_list_init (gf_ws_list_t *list, uint32_t initial_capacity);
void gf_workspace_list_rebuild_stats (gf_ws_list_t *ws, const gf_win_list_t *windows);
bool gf_workspace_list_remove_window (gf_ws_info_t *ws, gf_win_list_t *windows, gf_handle_t win_id);
#endif
