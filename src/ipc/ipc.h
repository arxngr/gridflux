#ifndef GRIDFLUX_IPC_H
#define GRIDFLUX_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GF_IPC_MSG_SIZE 8192

typedef enum
{
    GF_IPC_SUCCESS = 0,
    GF_IPC_ERROR_CONNECTION,
    GF_IPC_ERROR_INVALID_COMMAND,
    GF_IPC_ERROR_TIMEOUT,
    GF_IPC_ERROR_PERMISSION,
} gf_ipc_status_t;

typedef struct
{
    gf_ipc_status_t status;
    char message[GF_IPC_MSG_SIZE];
} gf_ipc_response_t;

typedef intptr_t gf_ipc_handle_t;

// --- Server Operations ---
gf_ipc_handle_t gf_ipc_server_create (void);
void gf_ipc_server_destroy (gf_ipc_handle_t handle);
bool gf_ipc_server_process (gf_ipc_handle_t handle, void *user_data);

// --- Client Operations ---
gf_ipc_handle_t gf_ipc_client_connect (void);
void gf_ipc_client_disconnect (gf_ipc_handle_t handle);
bool gf_ipc_client_send (gf_ipc_handle_t handle, const char *command,
                         gf_ipc_response_t *response);

// --- Misc Operations ---
void gf_handle_client_message (const char *message, size_t length,
                               gf_ipc_response_t *response, void *user_data);
// Commands are bounded C strings; received frames use their explicit byte length.
bool gf_ipc_command_length (const char *command, size_t *length);
bool gf_ipc_response_valid (const gf_ipc_response_t *response);
const char *gf_ipc_get_socket_path (void);

#endif
