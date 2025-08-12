#ifndef GF_UTILS_COLLECTIONS_H
#define GF_UTILS_COLLECTIONS_H

#include "../core/types.h"

typedef struct
{
    gf_workspace_info_t *items;
    uint32_t count;
    uint32_t capacity;
} gf_workspace_list_t;

// Workspace list operations
gf_error_code_t gf_workspace_list_init (gf_workspace_list_t *list,
                                        uint32_t initial_capacity);
void gf_workspace_list_cleanup (gf_workspace_list_t *list);
gf_error_code_t gf_workspace_list_add (gf_workspace_list_t *list,
                                       const gf_workspace_info_t *workspace);
gf_workspace_info_t *gf_workspace_list_find (const gf_workspace_list_t *list,
                                             gf_workspace_id_t workspace_id);

#endif // GF_UTILS_COLLECTIONS_H
