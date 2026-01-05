#ifndef GF_UTILS_LISTS_H
#define GF_UTILS_LISTS_H

#include "types.h"

typedef struct
{
    gf_window_info_t *items;
    uint32_t count;
    uint32_t capacity;
} gf_window_list_t;

typedef struct
{
    gf_workspace_info_t *items;
    uint32_t count;
    uint32_t capacity;
    uint32_t active_workspace;
} gf_workspace_list_t;

gf_error_code_t gf_window_list_init (gf_window_list_t *list, uint32_t initial_capacity);
void gf_window_list_cleanup (gf_window_list_t *list);
gf_error_code_t gf_window_list_add (gf_window_list_t *list,
                                    const gf_window_info_t *window);
gf_error_code_t gf_window_list_remove (gf_window_list_t *list, gf_window_id_t window_id);
gf_error_code_t gf_window_list_update (gf_window_list_t *list,
                                       const gf_window_info_t *window);
gf_window_info_t *gf_window_list_find_by_window_id (const gf_window_list_t *list,
                                                    gf_window_id_t window_id);
uint32_t gf_window_list_count_by_workspace (const gf_window_list_t *list,
                                            gf_workspace_id_t workspace_id);
void gf_window_list_clear_update_flags (gf_window_list_t *list,
                                        gf_workspace_id_t workspace_id);
gf_error_code_t gf_window_list_get_by_workspace (const gf_window_list_t *list,
                                                 gf_workspace_id_t workspace_id,
                                                 gf_window_info_t **windows,
                                                 uint32_t *count);
void gf_window_list_mark_all_needs_update (gf_window_list_t *list,
                                           const gf_workspace_id_t *workspace_id);

gf_error_code_t gf_workspace_list_init (gf_workspace_list_t *list,
                                        uint32_t initial_capacity);
void gf_workspace_list_cleanup (gf_workspace_list_t *list);
gf_error_code_t gf_workspace_list_add (gf_workspace_list_t *list,
                                       const gf_workspace_info_t *workspace);
gf_workspace_info_t *gf_workspace_list_find (const gf_workspace_list_t *list,
                                             gf_workspace_id_t workspace_id);

gf_workspace_info_t *gf_workspace_list_get_current (gf_workspace_list_t *ws);

uint32_t gf_workspace_list_calc_required_workspaces (uint32_t total_windows,
                                                     uint32_t current_workspaces,
                                                     uint32_t max_per_workspace);

gf_workspace_id_t gf_workspace_list_find_free (gf_workspace_list_t *ws,
                                               uint32_t max_win_per_ws);

void gf_workspace_list_ensure (gf_workspace_list_t *ws, gf_workspace_id_t ws_id,
                               uint32_t max_per_ws);

void gf_workspace_list_rebuild_stats (gf_workspace_list_t *ws,
                                      const gf_window_list_t *windows);
#endif
