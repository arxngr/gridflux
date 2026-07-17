#ifdef _WIN32

#include "../../ipc/ipc.h"
#include <windows.h>

#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    // Build an explicit DACL instead of a NULL DACL (which would grant Everyone
    // access). Grant full access (GA) only to the pipe owner / current user
    // (OW), SYSTEM (SY) and the Administrators group (BA). The descriptor is
    // allocated by LocalAlloc and must be released with LocalFree by the caller.
    PSECURITY_DESCRIPTOR sd = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA (
            "D:(A;;GA;;;OW)(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1, &sd, NULL))
    {
        free (sa);
        return NULL;
    }

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
create_pipe_instance (BOOL first_instance)
{
    const char *pipe_path = gf_ipc_get_socket_path ();

    SECURITY_ATTRIBUTES *sa = gf_pipe_security_attributes ();

    // FILE_FLAG_FIRST_PIPE_INSTANCE on the first instance ensures we are the
    // creator of the pipe (a squatter cannot pre-create it). PIPE_REJECT_REMOTE_
    // CLIENTS blocks connections from other machines.
    DWORD open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
    if (first_instance)
        open_mode |= FILE_FLAG_FIRST_PIPE_INSTANCE;

    HANDLE pipe = CreateNamedPipeA (
        pipe_path, open_mode,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        MAX_PIPE_INSTANCES, GF_PIPE_BUFSIZE, GF_PIPE_BUFSIZE, 0, sa);

    if (sa)
    {
        LocalFree (sa->lpSecurityDescriptor);
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

// Close the first `count` pipe instances and free the array (used to unwind a
// partially-initialised server). Closes each instance's pipe AND event handle.
// NOTE: the previous CreateEvent-failure path closed only pipes, leaking events.
static void
_pipe_destroy_instances (int count)
{
    for (int j = 0; j < count; j++)
    {
        if (pipe_instances[j].pipe != INVALID_HANDLE_VALUE)
            CloseHandle (pipe_instances[j].pipe);
        if (pipe_instances[j].overlapped.hEvent)
            CloseHandle (pipe_instances[j].overlapped.hEvent);
    }
    free (pipe_instances);
    pipe_instances = NULL;
}

gf_ipc_handle_t
gf_ipc_server_create (void)
{
    const char *pipe_path = gf_ipc_get_socket_path ();

    pipe_instances = calloc (MAX_PIPE_INSTANCES, sizeof (gf_pipe_t));
    if (!pipe_instances)
    {
        fprintf (stderr, "Failed to allocate pipe instances\n");
        return -1;
    }

    // Create multiple pipe instances for concurrent connections
    for (int i = 0; i < MAX_PIPE_INSTANCES; i++)
    {
        pipe_instances[i].pipe = create_pipe_instance (i == 0);
        if (pipe_instances[i].pipe == INVALID_HANDLE_VALUE)
        {
            fprintf (stderr, "CreateNamedPipe failed: %lu\n", GetLastError ());
            _pipe_destroy_instances (i);
            return -1;
        }

        pipe_instances[i].overlapped.hEvent = CreateEvent (NULL, TRUE, FALSE, NULL);
        if (!pipe_instances[i].overlapped.hEvent)
        {
            fprintf (stderr, "CreateEvent failed: %lu\n", GetLastError ());
            _pipe_destroy_instances (i + 1);
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

// Synchronously write `len` bytes to a FILE_FLAG_OVERLAPPED pipe handle.
// A blocking WriteFile with a NULL lpOverlapped is invalid on an overlapped
// handle, so we drive the async write to completion via a temporary
// manual-reset event and GetOverlappedResult. Returns TRUE on success.
static BOOL
_pipe_write_sync (HANDLE pipe, const void *data, DWORD len)
{
    OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEvent (NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent)
        return FALSE;

    DWORD written = 0;
    BOOL ok = WriteFile (pipe, data, len, &written, &ov);
    if (!ok && GetLastError () == ERROR_IO_PENDING)
        ok = GetOverlappedResult (pipe, &ov, &written, TRUE);

    CloseHandle (ov.hEvent);
    return ok && written == len;
}

// Handle a fully-read client message: dispatch it, write the reply, and reset
// the instance to listen for the next connection.
static void
_pipe_handle_message (gf_pipe_t *inst, DWORD bytes, void *user_data)
{
    inst->buffer[bytes] = '\0';

    gf_ipc_response_t response = { 0 };
    response.status = GF_IPC_SUCCESS;
    gf_handle_client_message (inst->buffer, &response, user_data);

    if (!_pipe_write_sync (inst->pipe, &response, sizeof (response)))
        fprintf (stderr, "Pipe reply write failed: %lu\n", GetLastError ());

    FlushFileBuffers (inst->pipe);
    DisconnectNamedPipe (inst->pipe);

    inst->connected = FALSE;
    connect_to_client (inst);
}

// Advance one pipe instance: complete a pending connect, then service any
// readable client message. Returns true if a message was processed.
static bool
_pipe_poll_instance (gf_pipe_t *inst, void *user_data)
{
    DWORD bytes = 0;

    if (inst->pending_io)
    {
        if (!GetOverlappedResult (inst->pipe, &inst->overlapped, &bytes, FALSE))
        {
            if (GetLastError () == ERROR_IO_INCOMPLETE)
                return false; // still waiting for a client
            // Error occurred, reset this instance
            DisconnectNamedPipe (inst->pipe);
            connect_to_client (inst);
            return false;
        }
        inst->pending_io = FALSE;
        inst->connected = TRUE;
    }

    if (!inst->connected)
        return false;

    if (ReadFile (inst->pipe, inst->buffer, sizeof (inst->buffer) - 1, &bytes,
                  &inst->overlapped))
    {
        _pipe_handle_message (inst, bytes, user_data);
        return true;
    }

    if (GetLastError () == ERROR_IO_PENDING)
    {
        if (WaitForSingleObject (inst->overlapped.hEvent, 0) == WAIT_OBJECT_0)
        {
            // Never index the buffer with an unvalidated byte count: bail out and
            // re-arm the instance if the overlapped read did not complete cleanly.
            if (!GetOverlappedResult (inst->pipe, &inst->overlapped, &bytes, FALSE))
            {
                DisconnectNamedPipe (inst->pipe);
                inst->connected = FALSE;
                connect_to_client (inst);
                return false;
            }
            _pipe_handle_message (inst, bytes, user_data);
            return true;
        }
        return false;
    }

    // Error occurred
    DisconnectNamedPipe (inst->pipe);
    inst->connected = FALSE;
    connect_to_client (inst);
    return false;
}

bool
gf_ipc_server_process (gf_ipc_handle_t handle, void *user_data)
{
    (void)handle;

    if (!pipe_instances)
        return false;

    bool processed = false;
    for (int i = 0; i < num_instances; i++)
        if (_pipe_poll_instance (&pipe_instances[i], user_data))
            processed = true;

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

#endif // _WIN32