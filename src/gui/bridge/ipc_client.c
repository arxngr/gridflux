#include "ipc_client.h"
#include <stdio.h>
#include <string.h>

bool
gf_gui_get_monitors (gf_monitor_t *monitors, uint32_t *count)
{
    if (!monitors || !count)
        return false;
    gf_ipc_response_t response = gf_run_client_command ("query monitors");
    if (response.status != GF_IPC_SUCCESS)
        return false;
    return gf_parse_monitor_list (response.message, sizeof (response.message), monitors,
                                  *count, count);
}

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

    gf_ipc_response_t response = { .status = GF_IPC_ERROR_CONNECTION };
    if (!gf_ipc_client_send (handle, command, &response))
    {
        gf_ipc_client_disconnect (handle);
        gf_ipc_response_t err = { .status = response.status };
        gf_command_response_t resp = { .type = 1 };
        snprintf (resp.message, sizeof (resp.message), "IPC send failed");
        memcpy (err.message, &resp, sizeof (resp));
        return err;
    }

    gf_ipc_client_disconnect (handle);

    return response;
}
