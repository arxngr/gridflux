
#include "internal.h"
#include "ipc.h"
#include "memory.h"
#include "window_manager.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
gf_parse_command (const char *input, char *command, char *args, size_t args_size)
{
    while (isspace (*input))
        input++;

    size_t i = 0;
    while (*input && !isspace (*input) && i < 63)
    {
        command[i++] = *input++;
    }
    command[i] = '\0';

    while (isspace (*input))
        input++;

    strncpy (args, input, args_size - 1);
    args[args_size - 1] = '\0';
}

static void
gf_cmd_query_windows (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_window_manager_t *m = (gf_window_manager_t *)user_data;
    gf_window_list_t *windows = wm_windows (m);

    int workspace_id = -1;
    if (args && *args)
    {
        workspace_id = atoi (args);
    }

    char *ptr = response->message;
    size_t remaining = sizeof (response->message) - 1;

    if (workspace_id >= 0)
    {
        gf_window_info_t *ws_windows = NULL;
        uint32_t count = 0;

        if (gf_window_list_get_by_workspace (windows, workspace_id, &ws_windows, &count)
            == GF_SUCCESS)
        {
            int n = snprintf (ptr, remaining, "Windows in workspace %d (%u total):\n",
                              workspace_id, count);
            if (n > 0 && (size_t)n < remaining)
            {
                ptr += n;
                remaining -= n;
            }

            for (uint32_t i = 0; i < count && remaining > 100; i++)
            {
                char win_name[256];
                gf_window_manager_get_window_name (m, ws_windows[i].native_handle,
                                                   win_name, sizeof (win_name));

                n = snprintf (ptr, remaining, "  [%lu] %s\n", ws_windows[i].id, win_name);
                if (n > 0 && (size_t)n < remaining)
                {
                    ptr += n;
                    remaining -= n;
                }
            }
            gf_free (ws_windows);
        }
    }
    else
    {
        int n = snprintf (ptr, remaining, "All windows (%u total):\n", windows->count);
        if (n > 0 && (size_t)n < remaining)
        {
            ptr += n;
            remaining -= n;
        }

        for (uint32_t i = 0; i < windows->count && remaining > 100; i++)
        {
            char win_name[256];
            gf_window_manager_get_window_name (m, windows->items[i].native_handle,
                                               win_name, sizeof (win_name));

            n = snprintf (ptr, remaining, "  [%lu] Workspace %d: %s\n",
                          windows->items[i].id, windows->items[i].workspace_id, win_name);
            if (n > 0 && (size_t)n < remaining)
            {
                ptr += n;
                remaining -= n;
            }
        }
    }
}

static void
gf_cmd_query_workspaces (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_window_manager_t *m = (gf_window_manager_t *)user_data;
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    char *ptr = response->message;
    size_t remaining = sizeof (response->message) - 1;

    int n = snprintf (ptr, remaining, "Workspaces (%u total):\n", workspaces->count);
    if (n > 0 && (size_t)n < remaining)
    {
        ptr += n;
        remaining -= n;
    }

    for (uint32_t i = 0; i < workspaces->count && remaining > 100; i++)
    {
        gf_workspace_info_t *ws = &workspaces->items[i];

        const char *status = ws->is_locked ? "locked" : "unlocked";

        n = snprintf (ptr, remaining,
                      "  Workspace %d: %u windows, %d available slots (%s)\n", ws->id,
                      ws->window_count, ws->available_space, status);

        if (n > 0 && (size_t)n < remaining)
        {
            ptr += n;
            remaining -= n;
        }
    }
}

static void
gf_cmd_query_count (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_window_manager_t *m = (gf_window_manager_t *)user_data;
    gf_window_list_t *windows = wm_windows (m);

    if (args && *args)
    {
        int workspace_id = atoi (args);
        uint32_t count = gf_window_list_count_by_workspace (windows, workspace_id);
        snprintf (response->message, sizeof (response->message),
                  "Workspace %d has %u windows", workspace_id, count);
    }
    else
    {
        snprintf (response->message, sizeof (response->message), "Total windows: %u",
                  windows->count);
    }
}

static void
gf_cmd_move_window (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_window_manager_t *m = (gf_window_manager_t *)user_data;
    gf_window_list_t *windows = wm_windows (m);
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    uint32_t window_id = 0;
    int target_workspace = -1;

    if (!args || sscanf (args, "%u %d", &window_id, &target_workspace) != 2)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        snprintf (response->message, sizeof (response->message),
                  "Usage: move <window_id> <workspace_id>");
        return;
    }

    if (target_workspace < 0 || target_workspace >= (int)m->config->max_workspaces)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        snprintf (response->message, sizeof (response->message),
                  "Invalid workspace ID: %d (must be 0-%u)", target_workspace,
                  m->config->max_workspaces - 1);
        return;
    }

    gf_window_info_t *win = NULL;
    for (uint32_t i = 0; i < windows->count; i++)
    {
        if (windows->items[i].id == window_id)
        {
            win = &windows->items[i];
            break;
        }
    }

    if (!win)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        snprintf (response->message, sizeof (response->message), "Window %u not found",
                  window_id);
        return;
    }

    gf_workspace_id_t old_workspace = win->workspace_id;

    if (old_workspace == target_workspace)
    {
        snprintf (response->message, sizeof (response->message),
                  "Window %u is already in workspace %d", window_id, target_workspace);
        return;
    }

    gf_workspace_list_ensure (workspaces, target_workspace,
                              m->config->max_windows_per_workspace);

    gf_workspace_info_t *target_ws = &workspaces->items[target_workspace];

    if (target_ws->is_locked)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        snprintf (response->message, sizeof (response->message), "Workspace %d is locked",
                  target_workspace);
        return;
    }

    if (target_ws->window_count >= m->config->max_windows_per_workspace)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        snprintf (response->message, sizeof (response->message),
                  "Workspace %d is full (%u/%u windows)", target_workspace,
                  target_ws->window_count, m->config->max_windows_per_workspace);
        return;
    }

    win->workspace_id = target_workspace;

    if (old_workspace >= 0 && old_workspace < (int)workspaces->count)
    {
        workspaces->items[old_workspace].window_count--;
        workspaces->items[old_workspace].available_space++;
    }

    target_ws->window_count++;
    target_ws->available_space--;

    snprintf (response->message, sizeof (response->message),
              "Moved window %u from workspace %d to workspace %d", window_id,
              old_workspace, target_workspace);
}

static void
gf_cmd_lock_workspace (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_window_manager_t *m = (gf_window_manager_t *)user_data;
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    int workspace_id = -1;

    if (!args || sscanf (args, "%d", &workspace_id) != 1)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        snprintf (response->message, sizeof (response->message),
                  "Usage: lock <workspace_id>");
        return;
    }

    if (workspace_id < 0 || workspace_id >= (int)m->config->max_workspaces)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        snprintf (response->message, sizeof (response->message),
                  "Invalid workspace ID: %d (must be 0-%u)", workspace_id,
                  m->config->max_workspaces - 1);
        return;
    }

    gf_workspace_list_ensure (workspaces, workspace_id,
                              m->config->max_windows_per_workspace);

    gf_workspace_info_t *ws = &workspaces->items[workspace_id];

    if (ws->is_locked)
    {
        snprintf (response->message, sizeof (response->message),
                  "Workspace %d is already locked", workspace_id);
        return;
    }

    ws->is_locked = true;
    ws->available_space = 0;

    if (gf_config_add_locked_workspace (m->config, workspace_id) == GF_SUCCESS)
    {
        snprintf (response->message, sizeof (response->message),
                  "Locked workspace %d (%u windows will remain)", workspace_id,
                  ws->window_count);
    }
    else
    {
        snprintf (response->message, sizeof (response->message),
                  "Locked workspace %d but failed to save to config", workspace_id);
    }
}

static void
gf_cmd_unlock_workspace (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_window_manager_t *m = (gf_window_manager_t *)user_data;
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    int workspace_id = -1;

    if (!args || sscanf (args, "%d", &workspace_id) != 1)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        snprintf (response->message, sizeof (response->message),
                  "Usage: unlock <workspace_id>");
        return;
    }

    if (workspace_id < 0 || workspace_id >= (int)m->config->max_workspaces)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        snprintf (response->message, sizeof (response->message),
                  "Invalid workspace ID: %d (must be 0-%u)", workspace_id,
                  m->config->max_workspaces - 1);
        return;
    }

    gf_workspace_list_ensure (workspaces, workspace_id,
                              m->config->max_windows_per_workspace);

    gf_workspace_info_t *ws = &workspaces->items[workspace_id];

    if (!ws->is_locked)
    {
        snprintf (response->message, sizeof (response->message),
                  "Workspace %d is already unlocked", workspace_id);
        return;
    }

    ws->is_locked = false;
    ws->available_space = m->config->max_windows_per_workspace - ws->window_count;

    if (gf_config_remove_locked_workspace (m->config, workspace_id) == GF_SUCCESS)
    {
        snprintf (response->message, sizeof (response->message),
                  "Unlocked workspace %d (%d slots available)", workspace_id,
                  ws->available_space);
    }
    else
    {
        snprintf (response->message, sizeof (response->message),
                  "Unlocked workspace %d but failed to save to config", workspace_id);
    }
}

void
gf_handle_client_message (const char *message, gf_ipc_response_t *response,
                          void *user_data)
{
    char command[64] = { 0 };
    char args[256] = { 0 };

    gf_parse_command (message, command, args, sizeof (args));

    if (strcmp (command, "query") == 0)
    {
        char subcommand[64] = { 0 };
        char subargs[256] = { 0 };
        gf_parse_command (args, subcommand, subargs, sizeof (subargs));

        if (strcmp (subcommand, "windows") == 0 || strcmp (subcommand, "W") == 0)
        {
            gf_cmd_query_windows (subargs, response, user_data);
        }
        else if (strcmp (subcommand, "workspaces") == 0 || strcmp (subcommand, "D") == 0)
        {
            gf_cmd_query_workspaces (subargs, response, user_data);
        }
        else if (strcmp (subcommand, "count") == 0 || strcmp (subcommand, "T") == 0)
        {
            gf_cmd_query_count (subargs, response, user_data);
        }
        else
        {
            response->status = GF_IPC_ERROR_INVALID_COMMAND;
            snprintf (response->message, sizeof (response->message), "Unknown query: %s",
                      subcommand);
        }
    }
    else if (strcmp (command, "move") == 0)
    {
        gf_cmd_move_window (args, response, user_data);
    }
    else if (strcmp (command, "lock") == 0)
    {
        gf_cmd_lock_workspace (args, response, user_data);
    }
    else if (strcmp (command, "unlock") == 0)
    {
        gf_cmd_unlock_workspace (args, response, user_data);
    }
    else
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        snprintf (response->message, sizeof (response->message), "Unknown command: %s",
                  command);
    }
}
