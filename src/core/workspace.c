#include "workspace.h"
#include "../utils/list.h"
#include "../utils/memory.h"
#include "../utils/workspace.h"
#include "logger.h"
#include <stdint.h>

gf_error_code_t
gf_workspace_manager_create (gf_workspace_manager_t **manager)
{
    if (!manager)
        return GF_ERROR_INVALID_PARAMETER;

    *manager = gf_calloc (1, sizeof (gf_workspace_manager_t));
    if (!*manager)
        return GF_ERROR_MEMORY_ALLOCATION;

    gf_error_code_t result = gf_workspace_list_init (&(*manager)->workspaces, 8);
    if (result != GF_SUCCESS)
    {
        gf_free (*manager);
        *manager = NULL;
        return result;
    }

    (*manager)->active_workspace = -1;

    return GF_SUCCESS;
}

void
gf_workspace_manager_destroy (gf_workspace_manager_t *manager)
{
    if (!manager)
        return;

    gf_workspace_list_cleanup (&manager->workspaces);
    gf_free (manager);
}

gf_workspace_info_t *
gf_workspace_manager_get_current (gf_workspace_manager_t *manager)
{
    if (!manager)
        return NULL;
    return gf_workspace_list_find (&manager->workspaces, manager->active_workspace);
}

uint32_t
gf_workspace_manager_calc_required_workspaces (uint32_t total_windows,
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
gf_workspace_manager_find_free (gf_workspace_manager_t *manager, uint32_t max_win_per_ws)
{
    for (uint32_t i = 0; i < manager->workspaces.count; i++)
    {
        gf_workspace_info_t *ws = &manager->workspaces.items[i];
        if (ws->available_space > 0)
            return ws->id;
    }

    gf_workspace_info_t ws = { .id = manager->workspaces.count,
                               .window_count = 0,
                               .max_windows = max_win_per_ws,
                               .available_space = max_win_per_ws };

    gf_workspace_list_add (&manager->workspaces, &ws);
    return ws.id;
}

void
gf_workspace_manager_ensure (gf_workspace_manager_t *wmgr, gf_workspace_id_t ws_id,
                             uint32_t max_per_ws)
{
    if (!wmgr)
        return;

    while (wmgr->workspaces.count <= ws_id)
    {
        gf_workspace_info_t ws = { .id = wmgr->workspaces.count,
                                   .window_count = 0,
                                   .max_windows = max_per_ws,
                                   .available_space = max_per_ws };

        gf_workspace_list_add (&wmgr->workspaces, &ws);
    }
}

void
gf_workspace_manager_rebuild_stats (gf_workspace_manager_t *wmgr,
                                    const gf_window_list_t *windows)
{
    if (!wmgr || !windows)
        return;

    for (uint32_t i = 0; i < wmgr->workspaces.count; i++)
    {
        gf_workspace_info_t *ws = &wmgr->workspaces.items[i];

        ws->window_count = gf_window_list_count_by_workspace (windows, ws->id);

        int32_t avail = (int32_t)ws->max_windows - (int32_t)ws->window_count;

        ws->available_space = (avail < 0) ? 0 : avail;
    }
}
