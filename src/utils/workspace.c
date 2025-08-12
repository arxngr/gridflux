
#include "../../include/utils/list.h"
#include "../../include/utils/memory.h"
#include "core/types.h"
#include <stdint.h>
#include <string.h>

gf_error_code_t
gf_window_list_init (gf_window_list_t *list, uint32_t initial_capacity)
{
    if (!list)
        return GF_ERROR_INVALID_PARAMETER;

    list->items = gf_calloc (initial_capacity, sizeof (gf_window_info_t));
    if (!list->items)
        return GF_ERROR_MEMORY_ALLOCATION;

    list->count = 0;
    list->capacity = initial_capacity;
    return GF_SUCCESS;
}

void
gf_workspace_list_cleanup (gf_workspace_list_t *list)
{
    if (!list)
        return;

    gf_free (list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

gf_error_code_t
gf_workspace_list_add (gf_workspace_list_t *list, const gf_workspace_info_t *workspace)
{
    if (!list || !workspace)
        return GF_ERROR_INVALID_PARAMETER;

    // Check if workspace already exists
    if (gf_workspace_list_find (list, workspace->id))
    {
        return GF_SUCCESS;
    }

    if (list->count >= list->capacity)
    {
        uint32_t new_capacity = list->capacity * 2;
        gf_workspace_info_t *new_items
            = gf_realloc (list->items, new_capacity * sizeof (gf_workspace_info_t));
        if (!new_items)
            return GF_ERROR_MEMORY_ALLOCATION;

        list->items = new_items;
        list->capacity = new_capacity;
    }

    list->items[list->count] = *workspace;
    list->count++;

    return GF_SUCCESS;
}

gf_workspace_info_t *
gf_workspace_list_find (const gf_workspace_list_t *list, gf_workspace_id_t workspace_id)
{
    if (!list)
        return NULL;

    for (uint32_t i = 0; i < list->count; i++)
    {
        if (list->items[i].id == workspace_id)
        {
            return &list->items[i];
        }
    }

    return NULL;
}

gf_error_code_t
gf_workspace_list_init (gf_workspace_list_t *list, uint32_t initial_capacity)
{
    if (!list || initial_capacity == 0)
        return GF_ERROR_INVALID_PARAMETER;

    list->items = gf_calloc (initial_capacity, sizeof (gf_workspace_info_t));
    if (!list->items)
        return GF_ERROR_MEMORY_ALLOCATION;

    list->count = 0;
    list->capacity = initial_capacity;

    return GF_SUCCESS;
}
