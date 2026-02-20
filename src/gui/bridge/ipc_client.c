#include "ipc_client.h"
#include <stdio.h>
#include <string.h>

gf_ipc_response_t
gf_run_client_command (const char *command)
{
    gf_ipc_handle_t handle = gf_ipc_client_connect ();
    if (handle < 0)
    {
        gf_ipc_response_t err = { .status = GF_IPC_ERROR_CONNECTION };
        gf_command_response_t resp = { .type = 1 };
        snprintf (resp.message, sizeof (resp.message), "Cannot connect to GridFlux");
        memcpy (err.message, &resp, sizeof (resp));
        return err;
    }

    gf_ipc_response_t response;
    if (!gf_ipc_client_send (handle, command, &response))
    {
        gf_ipc_client_disconnect (handle);
        gf_ipc_response_t err = { .status = GF_IPC_ERROR_INVALID_COMMAND };
        gf_command_response_t resp = { .type = 1 };
        snprintf (resp.message, sizeof (resp.message), "IPC send failed");
        memcpy (err.message, &resp, sizeof (resp));
        return err;
    }

    gf_ipc_client_disconnect (handle);

    return response;
}
