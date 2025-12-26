#include "list.h"
#include "../core/logger.h"
#include "memory.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void
gf_window_list_cleanup (gf_window_list_t *list)
{
    if (!list)
        return;

    gf_free (list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static gf_error_code_t
gf_window_list_ensure_capacity (gf_window_list_t *list, uint32_t required_capacity)
{
    if (list->capacity >= required_capacity)
        return GF_SUCCESS;

    uint32_t new_capacity = list->capacity;
    while (new_capacity < required_capacity)
    {
        new_capacity *= 2;
    }

    gf_window_info_t *new_items
        = gf_realloc (list->items, new_capacity * sizeof (gf_window_info_t));
    if (!new_items)
        return GF_ERROR_MEMORY_ALLOCATION;

    list->items = new_items;
    list->capacity = new_capacity;
    return GF_SUCCESS;
}

void
gf_window_list_mark_all_needs_update (gf_window_list_t *list,
                                      const gf_workspace_id_t *workspace_id)
{
    if (!list)
        return;

    for (uint32_t i = 0; i < list->count; i++)
    {
        gf_window_info_t *win = &list->items[i];

        if ((!workspace_id || win->workspace_id == *workspace_id) && win->is_valid)
        {
            win->needs_update = true;
        }
    }
}

gf_error_code_t
gf_window_list_add (gf_window_list_t *list, const gf_window_info_t *window)
{
    if (!list || !window)
        return GF_ERROR_INVALID_PARAMETER;

    // Check if window already exists
    if (gf_window_list_find_by_window_id (list, window->id))
    {
        return gf_window_list_update (list, window);
    }

    gf_error_code_t result = gf_window_list_ensure_capacity (list, list->count + 1);
    if (result != GF_SUCCESS)
        return result;

    list->items[list->count] = *window;
    list->count++;
    gf_window_list_mark_all_needs_update (list, &window->workspace_id);

    GF_LOG_DEBUG ("Added window %llu to workspace %d (total: %u)",
                  (unsigned long long)window->id, window->workspace_id, list->count);
    return GF_SUCCESS;
}

gf_error_code_t
gf_window_list_remove (gf_window_list_t *list, gf_window_id_t window_id)
{
    if (!list)
        return GF_ERROR_INVALID_PARAMETER;

    for (uint32_t i = 0; i < list->count; i++)
    {
        if (list->items[i].id == window_id)
        {
            gf_workspace_id_t workspace_id = list->items[i].workspace_id;

            // Move last item to this position
            if (i < list->count - 1)
            {
                list->items[i] = list->items[list->count - 1];
            }
            list->count--;

            memset (&list->items[list->count], 0, sizeof (list->items[0]));
            gf_window_list_mark_all_needs_update (list, &workspace_id);
            GF_LOG_DEBUG (
                "Removed window %llu with status %d from workspace %d (total: %u)",
                (unsigned long long)window_id, list->items[i].is_valid, workspace_id,
                list->count);
            return GF_SUCCESS;
        }
    }
    gf_window_list_mark_all_needs_update (list, NULL);

    return GF_ERROR_WINDOW_NOT_FOUND;
}

gf_error_code_t
gf_window_list_update (gf_window_list_t *list, const gf_window_info_t *window)
{
    if (!list || !window)
        return GF_ERROR_INVALID_PARAMETER;

    gf_window_info_t *existing = gf_window_list_find_by_window_id (list, window->id);
    if (!existing)
        return GF_ERROR_WINDOW_NOT_FOUND;

    bool changed = (existing->geometry.x != window->geometry.x
                    || existing->geometry.y != window->geometry.y
                    || existing->geometry.width != window->geometry.width
                    || existing->geometry.height != window->geometry.height
                    || existing->workspace_id != window->workspace_id);

    *existing = *window;

    if (changed)
    {
        existing->last_modified = time (NULL);
        existing->needs_update = true;
    }

    return GF_SUCCESS;
}

gf_window_info_t *
gf_window_list_find_by_window_id (const gf_window_list_t *list, gf_window_id_t window_id)
{
    if (!list)
        return NULL;

    for (uint32_t i = 0; i < list->count; i++)
    {
        if (list->items[i].id == window_id)
        {
            return &list->items[i];
        }
    }

    return NULL;
}

uint32_t
gf_window_list_count_by_workspace (const gf_window_list_t *list,
                                   gf_workspace_id_t workspace_id)
{
    if (!list)
        return 0;

    uint32_t count = 0;
    for (uint32_t i = 0; i < list->count; i++)
    {
        if (list->items[i].workspace_id == workspace_id)
        {
            count++;
        }
    }

    return count;
}

void
gf_window_list_clear_update_flags (gf_window_list_t *list, gf_workspace_id_t workspace_id)
{
    if (!list)
        return;

    for (uint32_t i = 0; i < list->count; i++)
    {
        if (workspace_id < 0 || list->items[i].workspace_id == workspace_id)
        {
            list->items[i].needs_update = false;
        }
    }
}

gf_error_code_t
gf_window_list_get_by_workspace (const gf_window_list_t *list,
                                 gf_workspace_id_t workspace_id,
                                 gf_window_info_t **windows, uint32_t *count)
{
    if (!list || !windows)
        return GF_ERROR_INVALID_PARAMETER;

    if (count)
    {
        *count = gf_window_list_count_by_workspace (list, workspace_id);
        if (*count == 0)
        {
            *windows = NULL;
            return GF_SUCCESS;
        }
    }

    *windows = gf_malloc (*count * sizeof (gf_window_info_t));
    if (!*windows)
    {
        return GF_ERROR_MEMORY_ALLOCATION;
    }

    uint32_t idx = 0;
    for (uint32_t i = list->count; i-- > 0 && idx < *count;)
    {
        if (list->items[i].workspace_id == workspace_id)
        {
            (*windows)[idx++] = list->items[i];
        }
    }

    return GF_SUCCESS;
}
