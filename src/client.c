#include "ipc.h"
#include "ipc_command.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
print_usage (const char *prog)
{
    printf ("Usage: %s <command> [arguments]\n\n", prog);
    printf ("Commands:\n");
    printf ("  query windows [WORKSPACE_ID]    List windows\n");
    printf ("  query workspaces                List workspaces\n");
    printf ("  query count [WORKSPACE_ID]      Count windows\n");
    printf ("  move <WINDOW_ID> <WORKSPACE_ID> Move window to workspace\n");
    printf ("  lock <WORKSPACE_ID>             Lock workspace (prevent new windows)\n");
    printf ("  unlock <WORKSPACE_ID>           Unlock workspace\n");
    printf ("\nExamples:\n");
    printf ("  %s query windows              # List all windows\n", prog);
    printf ("  %s query windows workspace 1  # List windows in workspace 1\n", prog);
    printf ("  %s query workspaces           # List all workspaces\n", prog);
    printf ("  %s move 12345 2               # Move window 12345 to workspace 2\n", prog);
    printf ("  %s lock 3                     # Lock workspace 3\n", prog);
    printf ("  %s unlock 3                   # Unlock workspace 3\n", prog);
}

int
main (int argc, char **argv)
{
    if (argc < 2)
    {
        print_usage (argv[0]);
        return 1;
    }

    char command[GF_IPC_MSG_SIZE] = { 0 };
    size_t pos = 0;

    for (int i = 1; i < argc && pos < sizeof (command) - 2; i++)
    {
        if (i > 1)
            command[pos++] = ' ';
        size_t len = strlen (argv[i]);
        if (pos + len < sizeof (command) - 1)
        {
            strcpy (command + pos, argv[i]);
            pos += len;
        }
    }

    gf_ipc_handle_t handle = gf_ipc_client_connect ();
    if (handle < 0)
    {
        fprintf (stderr, "Error: Cannot connect to GridFlux. Is it running?\n");
        return 1;
    }

    gf_ipc_response_t response;
    if (!gf_ipc_client_send (handle, command, &response))
    {
        fprintf (stderr, "Error: Failed to send command\n");
        gf_ipc_client_disconnect (handle);
        return 1;
    }

    gf_ipc_client_disconnect (handle);

    if (response.status != GF_IPC_SUCCESS)
    {
        fprintf (stderr, "Error: %s\n", response.message);
        return 1;
    }

    if (strncmp (command, "query workspaces", 16) == 0)
    {
        gf_workspace_list_t *workspaces = gf_parse_workspace_list (response.message);
        if (!workspaces)
        {
            fprintf (stderr, "Error: Failed to parse workspace data\n");
            return 1;
        }

        printf ("Workspaces:\n");
        printf ("%-5s %-12s %-12s %-8s %-6s\n", "ID", "Windows", "Max", "Avail",
                "Locked");
        printf ("%-5s %-12s %-12s %-8s %-6s\n", "----", "------", "---", "-----",
                "------");

        for (uint32_t i = 0; i < workspaces->count; i++)
        {
            gf_workspace_info_t *ws = &workspaces->items[i];
            printf ("%-5d %-12u %-12u %-8d %-6s\n", ws->id, ws->window_count,
                    ws->max_windows, ws->available_space, ws->is_locked ? "Yes" : "No");
        }

        gf_workspace_list_cleanup (workspaces);
    }
    else if (strncmp (command, "query windows", 13) == 0)
    {
        int ws_filter = -1;
        char *ws_ptr = strstr (command, "workspace");
        if (ws_ptr)
        {
            sscanf (ws_ptr, "workspace %d", &ws_filter);
        }

        gf_window_list_t *windows = gf_parse_window_list (response.message);
        if (!windows)
        {
            fprintf (stderr, "Error: Failed to parse window data\n");
            return 1;
        }

        printf ("Windows:\n");
        printf ("%-10s %-20s %-10s %-6s\n", "ID", "Name", "Workspace", "State");
        printf ("%-10s %-20s %-10s %-6s\n", "----------", "--------------------",
                "----------", "------");

        for (uint32_t i = 0; i < windows->count; i++)
        {
            gf_window_info_t *win = &windows->items[i];
            if (ws_filter != -1 && win->workspace_id != (uint32_t)ws_filter)
                continue;
            const char *state
                = win->is_minimized ? "Min" : (win->is_maximized ? "Max" : "Norm");
            printf ("%-10lu %-20s %-10d %-6s\n", (unsigned long)win->id, win->name,
                    win->workspace_id, state);
        }

        gf_window_list_cleanup (windows);
    }
    else
    {
        gf_command_response_t *resp = (gf_command_response_t *)response.message;
        if (resp->message[0])
        {
            printf ("%s", resp->message);
            if (resp->message[strlen (resp->message) - 1] != '\n')
                printf ("\n");
        }
    }

    return 0;
}
