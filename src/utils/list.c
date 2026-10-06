#include "list.h"
#include "../config/config.h"
#include "logger.h"
#include "memory.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

void
gf_window_list_cleanup (gf_win_list_t *list)
{
    if (!list)
        return;

    gf_free (list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static gf_err_t
window_list_ensure_capacity (gf_win_list_t *list, uint32_t required_capacity)
{
    if (list->capacity >= required_capacity)
        return GF_SUCCESS;

    uint32_t new_capacity = list->capacity;
    while (new_capacity < required_capacity)
    {
        new_capacity *= 2;
    }

    gf_win_info_t *new_items
        = gf_realloc (list->items, new_capacity * sizeof (gf_win_info_t));
    if (!new_items)
        return GF_ERROR_MEMORY_ALLOCATION;

    list->items = new_items;
    list->capacity = new_capacity;
    return GF_SUCCESS;
}

void
gf_window_list_mark_all_needs_update (gf_win_list_t *list, const gf_ws_id_t *workspace_id)
{
    if (!list)
        return;

    for (uint32_t i = 0; i < list->count; i++)
    {
        gf_win_info_t *win = &list->items[i];

        if ((!workspace_id || win->workspace_id == *workspace_id) && win->is_valid)
        {
            win->needs_update = true;
            win->arrange_failures = 0;
            win->maximize_fill_failures = 0;
        }
    }
}

gf_err_t
gf_window_list_add (gf_win_list_t *list, const gf_win_info_t *window)
{
    if (!list || !window)
        return GF_ERROR_INVALID_PARAMETER;

    // Check if window already exists
    if (gf_window_list_find_by_window_id (list, window->id))
    {
        return gf_window_list_update (list, window);
    }

    gf_err_t result = window_list_ensure_capacity (list, list->count + 1);
    if (result != GF_SUCCESS)
        return result;

    list->items[list->count] = *window;
    list->count++;
    gf_window_list_mark_all_needs_update (list, &window->workspace_id);

    GF_LOG_DEBUG ("Added window %p to workspace %d (total: %u)", (void *)window->id,
                  window->workspace_id, list->count);
    return GF_SUCCESS;
}

gf_err_t
gf_window_list_remove (gf_win_list_t *list, gf_handle_t window_id)
{
    if (!list)
        return GF_ERROR_INVALID_PARAMETER;

    for (uint32_t i = 0; i < list->count; i++)
    {
        if (list->items[i].id == window_id)
        {
            gf_ws_id_t workspace_id = list->items[i].workspace_id;

            // Shift the tail down to keep windows in stable insertion order, so
            // surviving tiles keep their layout cells instead of reshuffling.
            if (i < list->count - 1)
            {
                memmove (&list->items[i], &list->items[i + 1],
                         (list->count - i - 1) * sizeof (list->items[0]));
            }
            list->count--;

            memset (&list->items[list->count], 0, sizeof (list->items[0]));
            gf_window_list_mark_all_needs_update (list, &workspace_id);
            GF_LOG_DEBUG ("Removed window %p from workspace %d (total: %u)",
                          (void *)window_id, workspace_id, list->count);
            return GF_SUCCESS;
        }
    }
    gf_window_list_mark_all_needs_update (list, NULL);

    return GF_ERROR_WINDOW_NOT_FOUND;
}

gf_err_t
gf_window_list_update (gf_win_list_t *list, const gf_win_info_t *window)
{
    if (!list || !window)
        return GF_ERROR_INVALID_PARAMETER;

    gf_win_info_t *existing = gf_window_list_find_by_window_id (list, window->id);
    if (!existing)
        return GF_ERROR_WINDOW_NOT_FOUND;

    bool changed = (existing->geometry.x != window->geometry.x
                    || existing->geometry.y != window->geometry.y
                    || existing->geometry.width != window->geometry.width
                    || existing->geometry.height != window->geometry.height
                    || existing->workspace_id != window->workspace_id
                    || existing->is_minimized != window->is_minimized
                    || existing->is_maximized != window->is_maximized);

    bool membership_changed = existing->monitor_id != window->monitor_id
                              || existing->workspace_id != window->workspace_id;
    gf_ws_id_t old_workspace = existing->workspace_id;

    // Save the needs_update flag before the struct copy overwrites it.
    // The platform-enumerated window data has needs_update = false,
    // but we may have set it to true (e.g. when a new window was added).
    bool was_pending = existing->needs_update;
    uint8_t failures = existing->arrange_failures;
    uint8_t fill_failures = existing->maximize_fill_failures;
    bool mode_changed = existing->is_maximized != window->is_maximized
                        || existing->is_minimized != window->is_minimized;

    *existing = *window;

    // Restore: keep true if it was already pending, or if geometry changed
    existing->needs_update = was_pending || changed || membership_changed;
    existing->arrange_failures = changed || membership_changed ? 0 : failures;
    existing->maximize_fill_failures
        = mode_changed || membership_changed ? 0 : fill_failures;

    if (membership_changed)
    {
        gf_window_list_mark_all_needs_update (list, &old_workspace);
        gf_window_list_mark_all_needs_update (list, &window->workspace_id);
    }

    if (changed)
    {
        existing->last_modified = time (NULL);
    }

    return GF_SUCCESS;
}

gf_win_info_t *
gf_window_list_find_by_window_id (const gf_win_list_t *list, gf_handle_t window_id)
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
gf_window_list_count_by_workspace (const gf_win_list_t *list, gf_ws_id_t workspace_id)
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

uint32_t
gf_window_list_count_by_workspace_monitor (const gf_win_list_t *list,
                                           gf_ws_id_t workspace_id,
                                           gf_monitor_id_t monitor_id)
{
    if (!list)
        return 0;

    uint32_t count = 0;
    for (uint32_t i = 0; i < list->count; i++)
    {
        const gf_win_info_t *win = &list->items[i];
        if (win->is_valid && win->workspace_id == workspace_id
            && win->monitor_id == monitor_id)
            count++;
    }

    return count;
}

void
gf_window_list_clear_update_flags (gf_win_list_t *list, gf_ws_id_t workspace_id)
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

gf_err_t
gf_window_list_get_by_workspace (const gf_win_list_t *list, gf_ws_id_t workspace_id,
                                 gf_win_info_t **windows, uint32_t *count)
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

    *windows = gf_malloc (*count * sizeof (gf_win_info_t));
    if (!*windows)
    {
        return GF_ERROR_MEMORY_ALLOCATION;
    }

    uint32_t idx = 0;
    // Collect in forward (insertion) order so the layout engine assigns stable
    // cells; iterating backwards made tiles jump whenever the list changed.
    for (uint32_t i = 0; i < list->count && idx < *count; i++)
    {
        if (list->items[i].workspace_id == workspace_id)
        {
            (*windows)[idx++] = list->items[i];
        }
    }

    return GF_SUCCESS;
}

gf_err_t
gf_window_list_init (gf_win_list_t *list, uint32_t initial_capacity)
{
    if (!list)
        return GF_ERROR_INVALID_PARAMETER;

    list->items = gf_calloc (initial_capacity, sizeof (gf_win_info_t));
    if (!list->items)
        return GF_ERROR_MEMORY_ALLOCATION;

    list->count = 0;
    list->capacity = initial_capacity;
    return GF_SUCCESS;
}

void
gf_workspace_list_cleanup (gf_ws_list_t *list)
{
    if (!list)
        return;

    gf_free (list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

gf_err_t
gf_workspace_list_add (gf_ws_list_t *list, const gf_ws_info_t *workspace)
{
    if (!list || !workspace)
        return GF_ERROR_INVALID_PARAMETER;

    // Check if workspace already exists
    if (gf_workspace_list_find_by_id (list, workspace->id))
        return GF_SUCCESS;

    if (list->count >= list->capacity)
    {
        uint32_t new_capacity = list->capacity * 2;
        gf_ws_info_t *new_items
            = gf_realloc (list->items, new_capacity * sizeof (gf_ws_info_t));
        if (!new_items)
            return GF_ERROR_MEMORY_ALLOCATION;

        list->items = new_items;
        list->capacity = new_capacity;
    }

    list->items[list->count] = *workspace;
    list->count++;

    return GF_SUCCESS;
}

gf_ws_info_t *
gf_workspace_list_find_by_id (const gf_ws_list_t *list, gf_ws_id_t workspace_id)
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

gf_ws_info_t *
gf_workspace_list_find_by_monitor_local (const gf_ws_list_t *list,
                                         gf_monitor_id_t monitor_id, gf_ws_id_t local_id)
{
    if (!list)
        return NULL;

    for (uint32_t i = 0; i < list->count; i++)
    {
        gf_ws_info_t *info = &list->items[i];
        if (info->monitor_id == monitor_id && info->local_id == local_id)
            return info;
    }

    return NULL;
}

gf_ws_id_t
gf_workspace_id_for_monitor_local (gf_monitor_id_t monitor_id, gf_ws_id_t local_id)
{
    if (monitor_id >= GF_MAX_MONITORS || local_id < GF_FIRST_WORKSPACE_ID
        || local_id > GF_MAX_WORKSPACES)
        return -1;

    return (gf_ws_id_t)(monitor_id * GF_MAX_WORKSPACES + local_id);
}

gf_err_t
gf_workspace_list_init (gf_ws_list_t *list, uint32_t initial_capacity)
{
    if (!list || initial_capacity == 0)
        return GF_ERROR_INVALID_PARAMETER;

    list->items = gf_calloc (initial_capacity, sizeof (gf_ws_info_t));
    if (!list->items)
        return GF_ERROR_MEMORY_ALLOCATION;

    list->count = 0;
    list->capacity = initial_capacity;

    return GF_SUCCESS;
}

gf_ws_info_t *
gf_workspace_list_get_current (gf_ws_list_t *ws, gf_monitor_id_t monitor_id)
{
    if (!ws || monitor_id >= GF_MAX_MONITORS)
        return NULL;

    gf_ws_info_t *current
        = gf_workspace_list_find_by_id (ws, ws->active_workspace[monitor_id]);
    return current && current->monitor_id == monitor_id ? current : NULL;
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

gf_ws_id_t
gf_workspace_list_find_free (gf_ws_list_t *ws, gf_monitor_id_t monitor_id)
{
    if (!ws)
        return -1;

    for (uint32_t i = 0; i < ws->count; i++)
    {
        gf_ws_info_t *info = &ws->items[i];
        if (info->monitor_id == monitor_id && info->available_space > 0
            && !info->is_locked && !info->has_maximized_state && !info->has_rule
            && !info->is_excluded_ws)
            return info->id;
    }

    return -1;
}

gf_ws_id_t
gf_workspace_create (gf_ws_list_t *ws, uint32_t max_win_per_ws, bool maximized_state,
                     bool is_locked, gf_monitor_id_t monitor_id, gf_ws_id_t local_id)
{
    if (!ws || monitor_id >= GF_MAX_MONITORS)
        return -1;

    if (local_id > 0)
    {
        gf_ws_info_t *existing
            = gf_workspace_list_find_by_monitor_local (ws, monitor_id, local_id);
        if (existing)
            return existing->id;
    }

    if (local_id == -1)
    {
        local_id = GF_FIRST_WORKSPACE_ID;
        for (gf_ws_id_t candidate = GF_FIRST_WORKSPACE_ID; candidate <= GF_MAX_WORKSPACES;
             candidate++)
        {
            if (!gf_workspace_list_find_by_monitor_local (ws, monitor_id, candidate))
            {
                local_id = candidate;
                break;
            }
            local_id = candidate + 1;
        }
        while (gf_workspace_list_find_by_monitor_local (ws, monitor_id, local_id))
            local_id++;
    }

    gf_ws_id_t next_id = local_id > 0 && local_id <= GF_MAX_WORKSPACES
                             ? gf_workspace_id_for_monitor_local (monitor_id, local_id)
                             : -1;

    // Special workspaces (maximized/excluded) need their own global ID without
    // occupying a user-visible local slot. IDs beyond the fixed local ranges
    // keep those states separate from normal workspaces on every monitor.
    if (next_id < GF_FIRST_WORKSPACE_ID || gf_workspace_list_find_by_id (ws, next_id))
    {
        next_id = GF_MAX_WORKSPACES_TOTAL + 1;
        for (uint32_t i = 0; i < ws->count; i++)
            if (ws->items[i].id >= next_id)
                next_id = ws->items[i].id + 1;
    }

    gf_ws_info_t info
        = { .id = next_id,
            .local_id = local_id,
            .monitor_id = monitor_id,
            .window_count = 0,
            .max_windows = maximized_state ? UINT32_MAX : max_win_per_ws,
            .available_space = maximized_state ? UINT32_MAX : max_win_per_ws,
            .has_maximized_state = maximized_state,
            .is_locked = is_locked };

    gf_workspace_list_add (ws, &info);

    if (maximized_state)
    {
        GF_LOG_INFO ("Created MAXIMIZED workspace %d (capacity=%u)", info.id,
                     max_win_per_ws);
    }
    else
    {
        GF_LOG_INFO ("Created NORMAL workspace %d (capacity=%u)", info.id,
                     max_win_per_ws);
    }

    return info.id;
}

void
gf_workspace_list_ensure (gf_ws_list_t *ws, gf_ws_id_t ws_id, uint32_t max_per_ws,
                          gf_monitor_id_t monitor_id, gf_ws_id_t local_id)
{
    if (!ws || ws_id < GF_FIRST_WORKSPACE_ID || monitor_id >= GF_MAX_MONITORS)
        return;

    gf_ws_info_t *existing = gf_workspace_list_find_by_id (ws, ws_id);
    if (existing)
        return;

    gf_ws_info_t *local
        = gf_workspace_list_find_by_monitor_local (ws, monitor_id, local_id);
    if (local)
        return;

    gf_ws_info_t info = { .id = ws_id,
                          .local_id = local_id,
                          .monitor_id = monitor_id,
                          .window_count = 0,
                          .max_windows = max_per_ws,
                          .available_space = max_per_ws,
                          .is_locked = false };

    gf_workspace_list_add (ws, &info);
}

bool
gf_workspace_list_remove_window (gf_ws_info_t *ws, gf_win_list_t *windows,
                                 gf_handle_t win_id)
{
    if (!ws)
        return false;

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *w = &windows->items[i];

        if (!w->is_valid)
            continue;

        if (w->id == win_id && w->workspace_id == ws->id
            && w->monitor_id == ws->monitor_id)
        {
            ws->window_count--;
            ws->available_space++;
            return true;
        }
    }

    return false;
}

bool
gf_workspace_list_add_window (gf_ws_info_t *ws, gf_win_list_t *windows,
                              gf_handle_t win_id)
{
    if (!ws || ws->available_space <= 0)
        return false;

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *w = &windows->items[i];

        if (!w->is_valid)
            continue;

        if (w->id == win_id && w->monitor_id == ws->monitor_id)
        {
            w->workspace_id = ws->id;

            ws->window_count++;
            ws->available_space--;
            return true;
        }
    }

    return false;
}
