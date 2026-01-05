#include "ipc.h"
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
    printf ("\nExamples:\n");
    printf ("  %s query windows              # List all windows\n", prog);
    printf ("  %s query windows 0            # List windows in workspace 0\n", prog);
    printf ("  %s query workspaces           # List all workspaces\n", prog);
    printf ("  %s query count                # Total window count\n", prog);
    printf ("  %s query count 1              # Window count in workspace 1\n", prog);
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
        {
            command[pos++] = ' ';
        }
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

    if (response.message[0])
    {
        printf ("%s", response.message);
        if (response.message[strlen (response.message) - 1] != '\n')
        {
            printf ("\n");
        }
    }

    return 0;
}
