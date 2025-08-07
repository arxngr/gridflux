#ifndef GF_UTILS_COLLECTIONS_H
#define GF_UTILS_COLLECTIONS_H

#include "../core/types.h"

// Dynamic array for windows
typedef struct {
  gf_window_info_t *items;
  uint32_t count;
  uint32_t capacity;
} gf_window_list_t;

// Dynamic array for workspaces
typedef struct {
  gf_workspace_info_t *items;
  uint32_t count;
  uint32_t capacity;
} gf_workspace_list_t;

// Window list operations
gf_error_code_t gf_window_list_init(gf_window_list_t *list,
                                    uint32_t initial_capacity);
void gf_window_list_cleanup(gf_window_list_t *list);
gf_error_code_t gf_window_list_add(gf_window_list_t *list,
                                   const gf_window_info_t *window);
gf_error_code_t gf_window_list_remove(gf_window_list_t *list,
                                      gf_window_id_t window_id);
gf_error_code_t gf_window_list_update(gf_window_list_t *list,
                                      const gf_window_info_t *window);
gf_window_info_t *gf_window_list_find(const gf_window_list_t *list,
                                      gf_window_id_t window_id);
uint32_t gf_window_list_count_by_workspace(const gf_window_list_t *list,
                                           gf_workspace_id_t workspace_id);
void gf_window_list_clear_update_flags(gf_window_list_t *list,
                                       gf_workspace_id_t workspace_id);
gf_error_code_t gf_window_list_get_by_workspace(const gf_window_list_t *list,
                                                gf_workspace_id_t workspace_id,
                                                gf_window_info_t **windows,
                                                uint32_t *count);

// Workspace list operations
gf_error_code_t gf_workspace_list_init(gf_workspace_list_t *list,
                                       uint32_t initial_capacity);
void gf_workspace_list_cleanup(gf_workspace_list_t *list);
gf_error_code_t gf_workspace_list_add(gf_workspace_list_t *list,
                                      const gf_workspace_info_t *workspace);
gf_workspace_info_t *gf_workspace_list_find(const gf_workspace_list_t *list,
                                            gf_workspace_id_t workspace_id);

#endif // GF_UTILS_COLLECTIONS_H
