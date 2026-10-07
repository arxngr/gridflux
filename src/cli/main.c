#include "../ipc/ipc.h"
#include "../ipc/ipc_command.h"
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
    printf ("  query monitors                  List connected monitor IDs\n");
    printf ("  query count [WORKSPACE_ID]      Count windows\n");
    printf ("  query apps                      List running application classes\n");
    printf ("  move <WINDOW_ID> <WORKSPACE_ID> Move window to workspace\n");
    printf ("  lock <WORKSPACE_ID>             Lock workspace (prevent new windows)\n");
    printf ("  unlock <WORKSPACE_ID>           Unlock workspace\n");
    printf ("  rule add <WM_CLASS> <WS_ID> [MONITOR_ID]  Add a window rule\n");
    printf ("  rule remove <WM_CLASS>          Remove a window rule\n");
    printf ("  rule list                       List all window rules\n");
    printf ("  exclude add <WM_CLASS>          Exclude an app from arrangement\n");
    printf ("  exclude remove <WM_CLASS>       Remove an app exclusion\n");
    printf ("  exclude list                    List excluded apps\n");
    printf ("\nExamples:\n");
    printf ("  %s query windows              # List all windows\n", prog);
    printf ("  %s query workspaces           # List all workspaces\n", prog);
    printf ("  %s query apps                 # List running app classes\n", prog);
    printf ("  %s move 0x12345 2               # Move window 0x12345 to workspace 2\n",
            prog);
    printf ("  %s lock 3                     # Lock workspace 3\n", prog);
    printf ("  %s unlock 3                   # Unlock workspace 3\n", prog);
    printf ("  %s rule add firefox 3         # Assign Firefox to workspace 3\n", prog);
    printf ("  %s rule remove firefox        # Remove Firefox rule\n", prog);
    printf ("  %s rule list                  # Show all rules\n", prog);
    printf ("  %s exclude add Spotify        # Exclude Spotify from arrangement\n", prog);
    printf ("  %s exclude remove Spotify     # Remove Spotify exclusion\n", prog);
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

    for (int i = 1; i < argc; i++)
    {
        size_t len = strlen (argv[i]);
        size_t separator = i > 1 ? 1 : 0;
        if (separator >= sizeof (command) - pos
            || len >= sizeof (command) - pos - separator)
        {
            fprintf (stderr, "Error: Command exceeds IPC capacity\n");
            return 1;
        }
        if (i > 1)
        {
            command[pos++] = ' ';
        }
        memcpy (command + pos, argv[i], len + 1);
        pos += len;
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
        gf_command_response_t result;
        if (gf_parse_command_response (response.message, sizeof (response.message),
                                       &result))
            fprintf (stderr, "Error: %s\n", result.message);
        else
            fprintf (stderr, "Error: IPC request rejected (%d)\n", response.status);
        return 1;
    }

    if (strncmp (command, "query workspaces", 16) == 0)
    {
        gf_ws_list_t *workspaces
            = gf_parse_workspace_list (response.message, sizeof (response.message));
        if (!workspaces)
        {
            fprintf (stderr, "Error: Failed to parse workspace data\n");
            return 1;
        }

        printf ("Workspaces:\n");
        printf ("%-5s %-8s %-8s %-12s %-8s %-6s %-8s\n", "ID", "Monitor", "Local",
                "Windows", "Avail", "Locked", "State");
        printf ("%-5s %-8s %-8s %-12s %-8s %-6s %-8s\n", "----", "-------", "-----",
                "-------", "-----", "------", "-----");

        for (uint32_t i = 0; i < workspaces->count; i++)
        {
            gf_ws_info_t *ws = &workspaces->items[i];
            const char *state = ws->has_maximized_state ? "Maximized"
                                : ws->is_excluded_ws    ? "Excluded"
                                : ws->has_rule          ? "Rule"
                                                        : "Normal";
            printf ("%-5d %-8u %-8d %-12u %-8d %-6s %-8s%s\n", ws->id, ws->monitor_id,
                    ws->local_id, ws->window_count, ws->available_space,
                    ws->is_locked ? "Yes" : "No", state,
                    ws->monitor_id < GF_MAX_MONITORS
                            && workspaces->active_workspace[ws->monitor_id] == ws->id
                        ? " *"
                        : "");
        }

        gf_free_workspace_list (workspaces);
    }
    else if (strncmp (command, "query windows", 13) == 0)
    {
        gf_win_list_t *windows
            = gf_parse_window_list (response.message, sizeof (response.message));
        if (!windows)
        {
            fprintf (stderr, "Error: Failed to parse window data\n");
            return 1;
        }

        printf ("Windows:\n");
        printf ("%-18s %-20s %-8s %-10s %-6s\n", "ID", "Name", "Monitor", "Workspace",
                "State");
        printf ("%-18s %-20s %-8s %-10s %-6s\n", "------------------",
                "--------------------", "-------", "----------", "------");

        for (uint32_t i = 0; i < windows->count; i++)
        {
            gf_win_info_t *win = &windows->items[i];
            const char *state
                = win->is_minimized ? "Min" : (win->is_maximized ? "Max" : "Norm");
            printf ("%-18p %-20s %-8u %-10d %-6s\n", (void *)win->id, win->name,
                    win->monitor_id, win->workspace_id, state);
        }

        gf_free_window_list (windows);
    }
    else if (strncmp (command, "query monitors", 14) == 0)
    {
        uint32_t count = GF_MAX_MONITORS;
        gf_monitor_t monitors[GF_MAX_MONITORS];
        if (!gf_parse_monitor_list (response.message, sizeof (response.message), monitors,
                                    GF_MAX_MONITORS, &count))
        {
            fprintf (stderr, "Error: Invalid monitor list\n");
            return 1;
        }
        printf ("Connected monitors:\n");
        for (uint32_t i = 0; i < count; i++)
        {
            gf_monitor_t monitor = monitors[i];
            printf ("M%u%s\n", monitor.id, monitor.is_primary ? " (primary)" : "");
        }
    }
    else
    {
        gf_command_response_t result;
        if (!gf_parse_command_response (response.message, sizeof (response.message),
                                        &result))
        {
            fprintf (stderr, "Error: Invalid command reply\n");
            return 1;
        }
        gf_command_response_t *resp = &result;
        if (resp->message[0])
        {
            printf ("%s", resp->message);
            if (resp->message[strlen (resp->message) - 1] != '\n')
            {
                printf ("\n");
            }
        }
    }

    return 0;
}
