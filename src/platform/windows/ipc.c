#ifdef _WIN32

#include "ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define GF_PIPE_NAME "\\\\.\\pipe\\gridflux"
#define GF_PIPE_BUFSIZE 8192

static char pipe_name[256] = { 0 };

const char *
gf_ipc_get_socket_path (void)
{
    if (pipe_name[0] != '\0')
    {
        return pipe_name;
    }

    DWORD session_id = 0;
    ProcessIdToSessionId (GetCurrentProcessId (), &session_id);

    snprintf (pipe_name, sizeof (pipe_name), "\\\\.\\pipe\\gridflux_%lu", session_id);
    return pipe_name;
}

gf_ipc_handle_t
gf_ipc_server_create (void)
{
    const char *pipe_path = gf_ipc_get_socket_path ();

    HANDLE pipe = CreateNamedPipeA (pipe_path, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                    PIPE_UNLIMITED_INSTANCES, GF_PIPE_BUFSIZE,
                                    GF_PIPE_BUFSIZE, 0, NULL);

    if (pipe == INVALID_HANDLE_VALUE)
    {
        fprintf (stderr, "CreateNamedPipe failed: %lu\n", GetLastError ());
        return -1;
    }

    printf ("IPC server listening on: %s\n", pipe_path);
    return (gf_ipc_handle_t)pipe;
}

void
gf_ipc_server_destroy (gf_ipc_handle_t handle)
{
    if (handle != -1)
    {
        CloseHandle ((HANDLE)handle);
    }
}

bool
gf_ipc_server_process (gf_ipc_handle_t handle, void *user_data)
{
    HANDLE pipe = (HANDLE)handle;

    OVERLAPPED overlapped = { 0 };
    overlapped.hEvent = CreateEvent (NULL, TRUE, FALSE, NULL);

    BOOL connected = ConnectNamedPipe (pipe, &overlapped);

    if (!connected)
    {
        DWORD err = GetLastError ();
        if (err == ERROR_IO_PENDING)
        {
            DWORD wait = WaitForSingleObject (overlapped.hEvent, 0);
            if (wait != WAIT_OBJECT_0)
            {
                CloseHandle (overlapped.hEvent);
                return false;
            }
        }
        else if (err != ERROR_PIPE_CONNECTED)
        {
            CloseHandle (overlapped.hEvent);
            return false;
        }
    }

    char buffer[GF_IPC_MSG_SIZE];
    DWORD bytes_read = 0;

    BOOL success = ReadFile (pipe, buffer, sizeof (buffer) - 1, &bytes_read, NULL);

    if (success && bytes_read > 0)
    {
        buffer[bytes_read] = '\0';

        gf_ipc_response_t response = { 0 };
        response.status = GF_IPC_SUCCESS;

        gf_handle_client_message (buffer, &response, user_data);

        DWORD bytes_written;
        WriteFile (pipe, &response, sizeof (response), &bytes_written, NULL);
    }

    DisconnectNamedPipe (pipe);
    CloseHandle (overlapped.hEvent);

    return true;
}

gf_ipc_handle_t
gf_ipc_client_connect (void)
{
    const char *pipe_path = gf_ipc_get_socket_path ();

    HANDLE pipe = CreateFileA (pipe_path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                               OPEN_EXISTING, 0, NULL);

    if (pipe == INVALID_HANDLE_VALUE)
    {
        fprintf (stderr, "Failed to connect to pipe: %lu\n", GetLastError ());
        return -1;
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState (pipe, &mode, NULL, NULL);

    return (gf_ipc_handle_t)pipe;
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
