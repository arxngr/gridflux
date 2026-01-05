
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
    else
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        snprintf (response->message, sizeof (response->message), "Unknown command: %s",
                  command);
    }
}
