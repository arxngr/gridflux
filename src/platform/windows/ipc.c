#ifdef _WIN32

#include "../../ipc/ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define GF_PIPE_NAME "\\\\.\\pipe\\gridflux"
#define GF_PIPE_BUFSIZE 8192
#define GF_PIPE_TIMEOUT 100
#define MAX_PIPE_INSTANCES 10

typedef struct
{
    HANDLE pipe;
    OVERLAPPED overlapped;
    char buffer[GF_IPC_MSG_SIZE];
    DWORD bytes_read;
    BOOL pending_io;
    BOOL connected;
} gf_pipe_t;

static char pipe_name[256] = { 0 };
static gf_pipe_t *pipe_instances = NULL;
static int num_instances = 0;

static SECURITY_ATTRIBUTES *
gf_pipe_security_attributes (void)
{
    SECURITY_ATTRIBUTES *sa = malloc (sizeof (*sa));
    if (!sa)
        return NULL;

    PSECURITY_DESCRIPTOR sd = malloc (SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (!sd)
    {
        free (sa);
        return NULL;
    }

    InitializeSecurityDescriptor (sd, SECURITY_DESCRIPTOR_REVISION);

    // NULL DACL = allow same-user access (Unix chmod 0600 equivalent)
    SetSecurityDescriptorDacl (sd, TRUE, NULL, FALSE);

    sa->nLength = sizeof (*sa);
    sa->lpSecurityDescriptor = sd;
    sa->bInheritHandle = FALSE;
    return sa;
}

const char *
gf_ipc_get_socket_path (void)
{
    if (pipe_name[0] != '\0')
    {
        return pipe_name;
    }

    snprintf (pipe_name, sizeof (pipe_name), "\\\\.\\pipe\\gridflux");

    return pipe_name;
}

static HANDLE
create_pipe_instance (void)
{
    const char *pipe_path = gf_ipc_get_socket_path ();

    SECURITY_ATTRIBUTES *sa = gf_pipe_security_attributes ();

    HANDLE pipe
        = CreateNamedPipeA (pipe_path, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                            MAX_PIPE_INSTANCES, GF_PIPE_BUFSIZE, GF_PIPE_BUFSIZE, 0, sa);

    if (sa)
    {
        free (sa->lpSecurityDescriptor);
        free (sa);
    }

    return pipe;
}

static BOOL
connect_to_client (gf_pipe_t *instance)
{
    BOOL connected = ConnectNamedPipe (instance->pipe, &instance->overlapped);

    if (connected)
    {
        // Should never happen with async pipes
        return FALSE;
    }

    DWORD err = GetLastError ();
    switch (err)
    {
    case ERROR_IO_PENDING:
        instance->pending_io = TRUE;
        return TRUE;

    case ERROR_PIPE_CONNECTED:
        // Client connected before we called ConnectNamedPipe
        if (SetEvent (instance->overlapped.hEvent))
        {
            instance->pending_io = FALSE;
            instance->connected = TRUE;
            return TRUE;
        }
        break;
    }

    return FALSE;
}

gf_ipc_handle_t
gf_ipc_server_create (void)
{
    const char *pipe_path = gf_ipc_get_socket_path ();

    // Allocate pipe instances array
    pipe_instances = calloc (MAX_PIPE_INSTANCES, sizeof (gf_pipe_t));
    if (!pipe_instances)
    {
        fprintf (stderr, "Failed to allocate pipe instances\n");
        return -1;
    }

    // Create multiple pipe instances for concurrent connections
    for (int i = 0; i < MAX_PIPE_INSTANCES; i++)
    {
        pipe_instances[i].pipe = create_pipe_instance ();

        if (pipe_instances[i].pipe == INVALID_HANDLE_VALUE)
        {
            fprintf (stderr, "CreateNamedPipe failed: %lu\n", GetLastError ());
            // Clean up previous instances
            for (int j = 0; j < i; j++)
            {
                CloseHandle (pipe_instances[j].pipe);
                if (pipe_instances[j].overlapped.hEvent)
                    CloseHandle (pipe_instances[j].overlapped.hEvent);
            }
            free (pipe_instances);
            return -1;
        }

        pipe_instances[i].overlapped.hEvent = CreateEvent (NULL, TRUE, FALSE, NULL);
        if (!pipe_instances[i].overlapped.hEvent)
        {
            fprintf (stderr, "CreateEvent failed: %lu\n", GetLastError ());
            for (int j = 0; j <= i; j++)
            {
                CloseHandle (pipe_instances[j].pipe);
            }
            free (pipe_instances);
            return -1;
        }

        // Start listening for connections
        connect_to_client (&pipe_instances[i]);
        num_instances++;
    }

    printf ("IPC server listening on: %s (with %d instances)\n", pipe_path,
            num_instances);
    return 0; // Return success, actual handles are in pipe_instances
}

void
gf_ipc_server_destroy (gf_ipc_handle_t handle)
{
    if (pipe_instances)
    {
        for (int i = 0; i < num_instances; i++)
        {
            if (pipe_instances[i].pipe != INVALID_HANDLE_VALUE)
            {
                DisconnectNamedPipe (pipe_instances[i].pipe);
                CloseHandle (pipe_instances[i].pipe);
            }
            if (pipe_instances[i].overlapped.hEvent)
            {
                CloseHandle (pipe_instances[i].overlapped.hEvent);
            }
        }
        free (pipe_instances);
        pipe_instances = NULL;
        num_instances = 0;
    }
}

bool
gf_ipc_server_process (gf_ipc_handle_t handle, void *user_data)
{
    if (!pipe_instances)
        return false;

    BOOL processed = FALSE;

    for (int i = 0; i < num_instances; i++)
    {
        gf_pipe_t *inst = &pipe_instances[i];
        DWORD bytes_transferred;
        BOOL success;

        // Check if there's a pending connection
        if (inst->pending_io)
        {
            success = GetOverlappedResult (inst->pipe, &inst->overlapped,
                                           &bytes_transferred, FALSE);

            if (!success)
            {
                DWORD err = GetLastError ();
                if (err == ERROR_IO_INCOMPLETE)
                {
                    // Still waiting for client
                    continue;
                }
                // Error occurred, reset this instance
                DisconnectNamedPipe (inst->pipe);
                connect_to_client (inst);
                continue;
            }

            inst->pending_io = FALSE;
            inst->connected = TRUE;
        }

        // If connected, try to read data
        if (inst->connected)
        {
            success = ReadFile (inst->pipe, inst->buffer, sizeof (inst->buffer) - 1,
                                &bytes_transferred, &inst->overlapped);

            if (success)
            {
                // Read completed immediately
                inst->buffer[bytes_transferred] = '\0';

                gf_ipc_response_t response = { 0 };
                response.status = GF_IPC_SUCCESS;

                gf_handle_client_message (inst->buffer, &response, user_data);

                DWORD bytes_written;
                WriteFile (inst->pipe, &response, sizeof (response), &bytes_written,
                           NULL);

                FlushFileBuffers (inst->pipe);
                DisconnectNamedPipe (inst->pipe);

                inst->connected = FALSE;
                connect_to_client (inst);
                processed = TRUE;
            }
            else
            {
                DWORD err = GetLastError ();
                if (err == ERROR_IO_PENDING)
                {
                    // Wait a bit for the read to complete
                    DWORD wait = WaitForSingleObject (inst->overlapped.hEvent, 0);
                    if (wait == WAIT_OBJECT_0)
                    {
                        // Read completed
                        GetOverlappedResult (inst->pipe, &inst->overlapped,
                                             &bytes_transferred, FALSE);
                        inst->buffer[bytes_transferred] = '\0';

                        gf_ipc_response_t response = { 0 };
                        response.status = GF_IPC_SUCCESS;

                        gf_handle_client_message (inst->buffer, &response, user_data);

                        DWORD bytes_written;
                        WriteFile (inst->pipe, &response, sizeof (response),
                                   &bytes_written, NULL);

                        FlushFileBuffers (inst->pipe);
                        DisconnectNamedPipe (inst->pipe);

                        inst->connected = FALSE;
                        connect_to_client (inst);
                        processed = TRUE;
                    }
                }
                else
                {
                    // Error occurred
                    DisconnectNamedPipe (inst->pipe);
                    inst->connected = FALSE;
                    connect_to_client (inst);
                }
            }
        }
    }

    return processed;
}

gf_ipc_handle_t
gf_ipc_client_connect (void)
{
    const char *pipe_path = gf_ipc_get_socket_path ();

    // Try multiple times with short waits
    for (int retry = 0; retry < 10; retry++)
    {
        HANDLE pipe = CreateFileA (pipe_path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                   OPEN_EXISTING, 0, NULL);

        if (pipe != INVALID_HANDLE_VALUE)
        {
            DWORD mode = PIPE_READMODE_MESSAGE;
            if (!SetNamedPipeHandleState (pipe, &mode, NULL, NULL))
            {
                fprintf (stderr, "SetNamedPipeHandleState failed: %lu\n",
                         GetLastError ());
                CloseHandle (pipe);
                return -1;
            }
            return (gf_ipc_handle_t)pipe;
        }

        DWORD error = GetLastError ();

        if (error == ERROR_PIPE_BUSY)
        {
            // Wait briefly for a pipe instance to become available
            if (!WaitNamedPipeA (pipe_path, 50))
            {
                Sleep (10); // Brief sleep before retry
                continue;
            }
        }
        else
        {
            fprintf (stderr, "Failed to connect to pipe: %lu\n", error);
            return -1;
        }
    }

    fprintf (stderr, "Pipe not available after retries\n");
    return -1;
}

bool
gf_ipc_client_send (gf_ipc_handle_t handle, const char *command,
                    gf_ipc_response_t *response)
{
    if (handle == -1 || !command || !response)
    {
        return false;
    }

    HANDLE pipe = (HANDLE)handle;
    DWORD bytes_written;

    BOOL success
        = WriteFile (pipe, command, (DWORD)strlen (command), &bytes_written, NULL);

    if (!success)
    {
        fprintf (stderr, "WriteFile failed: %lu\n", GetLastError ());
        return false;
    }

    DWORD bytes_read;
    success = ReadFile (pipe, response, sizeof (*response), &bytes_read, NULL);

    if (!success || bytes_read != sizeof (*response))
    {
        fprintf (stderr, "ReadFile failed: %lu\n", GetLastError ());
        return false;
    }

    return true;
}

void
gf_ipc_client_disconnect (gf_ipc_handle_t handle)
{
    if (handle != -1)
    {
        CloseHandle ((HANDLE)handle);
    }
}

#endif /* _WIN32 */