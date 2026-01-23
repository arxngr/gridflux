#include "list.h"
#include "config.h"
#include "logger.h"
#include "memory.h"
#include <stdbool.h>
#include <stdint.h>
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
        return GF_SUCCESS;

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

gf_workspace_info_t *
gf_workspace_list_get_current (gf_workspace_list_t *ws)
{
    if (!ws)
        return NULL;

    return gf_workspace_list_find (ws, ws->active_workspace);
}

uint32_t
gf_workspace_list_calc_required_workspaces (uint32_t total_windows,
                                            uint32_t current_workspaces,
                                            uint32_t max_per_workspace)
{
    uint32_t capacity = current_workspaces * max_per_workspace;

    if (total_windows <= capacity)
        return 0;

    uint32_t overflow = total_windows - capacity;

    return (overflow + max_per_workspace - 1) / max_per_workspace;
}

gf_workspace_id_t
gf_workspace_list_find_free (gf_workspace_list_t *ws, uint32_t max_win_per_ws)
{
    if (!ws)
        return -1;

    for (uint32_t i = 0; i < ws->count; i++)
    {
        gf_workspace_info_t *info = &ws->items[i];
        if (info->available_space > 0 && !info->is_locked)
            return info->id;
    }

    gf_workspace_info_t info = { .id = ws->count + GF_FIRST_WORKSPACE_ID,
                                 .window_count = 0,
                                 .max_windows = max_win_per_ws,
                                 .available_space = max_win_per_ws,
                                 .is_locked = false };

    gf_workspace_list_add (ws, &info);
    return info.id;
}

void
gf_workspace_list_ensure (gf_workspace_list_t *ws, gf_workspace_id_t ws_id,
                          uint32_t max_per_ws)
{
    if (!ws || ws_id < GF_FIRST_WORKSPACE_ID)
        return;

    for (gf_workspace_id_t id = GF_FIRST_WORKSPACE_ID; id <= ws_id; id++)
    {
        if (!gf_workspace_list_find (ws, id))
        {
            gf_workspace_info_t info = {
                .id = id,
                .window_count = 0,
                .max_windows = max_per_ws,
                .available_space = max_per_ws,
                .is_locked = false,
            };

            gf_workspace_list_add (ws, &info);
        }
    }
}
