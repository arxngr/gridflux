#include "workspace.h"
#include "../utils/list.h"
#include "../utils/memory.h"
#include "../utils/workspace.h"
#include "logger.h"

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
