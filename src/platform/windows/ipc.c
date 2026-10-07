#ifdef _WIN32

#include "../../ipc/ipc.h"
#include <windows.h>

#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GF_PIPE_NAME "\\\\.\\pipe\\gridflux"
#define GF_PIPE_BUFSIZE sizeof (gf_ipc_response_t)
#define GF_PIPE_TIMEOUT 1000
#define MAX_PIPE_INSTANCES 10

typedef struct
{
    HANDLE pipe;
    OVERLAPPED overlapped;
    char buffer[GF_IPC_MSG_SIZE];
    DWORD bytes_read;
    BOOL pending_io;
    BOOL connected;
    BOOL read_pending;
    BOOL replied;
    ULONGLONG connected_at;
} gf_pipe_t;

static char pipe_name[256] = { 0 };
static gf_pipe_t *pipe_instances = NULL;
static int num_instances = 0;

static SECURITY_ATTRIBUTES *
pipe_security_attributes (void)
{
    SECURITY_ATTRIBUTES *sa = malloc (sizeof (*sa));
    if (!sa)
        return NULL;

    // Build an explicit DACL instead of a NULL DACL (which would grant Everyone
    // access). Grant full access (GA) to the pipe owner / current user (OW),
    // SYSTEM (SY) and the Administrators group (BA) — these manage the pipe.
    // Interactive Users (IU) get only read+write (GR|GW), the minimum a client
    // needs: this lets the non-elevated GUI reach the pipe when the server runs
    // elevated (its UAC-filtered token has Administrators disabled, so the BA ACE
    // alone would deny it) without granting WRITE_DAC/WRITE_OWNER/DELETE. The
    // descriptor is allocated by LocalAlloc and released with LocalFree.
    PSECURITY_DESCRIPTOR sd = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA (
            "D:(A;;GA;;;OW)(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)", SDDL_REVISION_1, &sd,
            NULL))
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

    SECURITY_ATTRIBUTES *sa = pipe_security_attributes ();

    // FILE_FLAG_FIRST_PIPE_INSTANCE on the first instance ensures we are the
    // creator of the pipe (a squatter cannot pre-create it). PIPE_REJECT_REMOTE_
    // CLIENTS blocks connections from other machines.
    DWORD open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
    if (first_instance)
        open_mode |= FILE_FLAG_FIRST_PIPE_INSTANCE;

    HANDLE pipe
        = CreateNamedPipeA (pipe_path, open_mode,
                            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT
                                | PIPE_REJECT_REMOTE_CLIENTS,
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
    HANDLE event = instance->overlapped.hEvent;
    memset (&instance->overlapped, 0, sizeof (instance->overlapped));
    instance->overlapped.hEvent = event;
    ResetEvent (event);
    instance->connected = FALSE;
    instance->pending_io = FALSE;
    instance->read_pending = FALSE;
    instance->replied = FALSE;
    instance->connected_at = GetTickCount64 ();
    BOOL connected = ConnectNamedPipe (instance->pipe, &instance->overlapped);
    if (connected || GetLastError () == ERROR_PIPE_CONNECTED)
    {
        instance->connected = TRUE;
        return TRUE;
    }
    if (GetLastError () == ERROR_IO_PENDING)
    {
        instance->pending_io = TRUE;
        return TRUE;
    }
    return FALSE;
}

static void
pipe_reset (gf_pipe_t *instance)
{
    if (instance->pending_io || instance->read_pending)
    {
        DWORD ignored;
        CancelIoEx (instance->pipe, &instance->overlapped);
        GetOverlappedResult (instance->pipe, &instance->overlapped, &ignored, TRUE);
    }
    DisconnectNamedPipe (instance->pipe);
    connect_to_client (instance);
}

// Close the first `count` pipe instances and free the array (used to unwind a
// partially-initialised server). Closes each instance's pipe AND event handle.
// NOTE: the previous CreateEvent-failure path closed only pipes, leaking events.
static void
pipe_destroy_instances (int count)
{
    for (int j = 0; j < count; j++)
    {
        if (pipe_instances[j].pending_io || pipe_instances[j].read_pending)
        {
            DWORD ignored;
            CancelIoEx (pipe_instances[j].pipe, &pipe_instances[j].overlapped);
            GetOverlappedResult (pipe_instances[j].pipe, &pipe_instances[j].overlapped,
                                 &ignored, TRUE);
        }
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
            pipe_destroy_instances (i);
            return -1;
        }

        pipe_instances[i].overlapped.hEvent = CreateEvent (NULL, TRUE, FALSE, NULL);
        if (!pipe_instances[i].overlapped.hEvent)
        {
            fprintf (stderr, "CreateEvent failed: %lu\n", GetLastError ());
            pipe_destroy_instances (i + 1);
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
    (void)handle;
    if (pipe_instances)
    {
        pipe_destroy_instances (num_instances);
        num_instances = 0;
    }
}

// Every operation owns its event until completion or cancellation. A stalled
// peer must not block the UI or leave I/O referring to expired stack buffers.
static BOOL
pipe_transfer (HANDLE pipe, void *data, DWORD length, BOOL reading, DWORD timeout)
{
    OVERLAPPED operation = { 0 };
    operation.hEvent = CreateEvent (NULL, TRUE, FALSE, NULL);
    if (!operation.hEvent)
        return FALSE;
    DWORD transferred = 0;
    BOOL ok = reading ? ReadFile (pipe, data, length, &transferred, &operation)
                      : WriteFile (pipe, data, length, &transferred, &operation);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError ();
    if (!ok && error == ERROR_IO_PENDING)
    {
        DWORD wait = WaitForSingleObject (operation.hEvent, timeout);
        if (wait == WAIT_OBJECT_0)
        {
            ok = GetOverlappedResult (pipe, &operation, &transferred, FALSE);
            error = ok ? ERROR_SUCCESS : GetLastError ();
        }
        else
        {
            error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError ();
            CancelIoEx (pipe, &operation);
            GetOverlappedResult (pipe, &operation, &transferred, TRUE);
        }
    }
    CloseHandle (operation.hEvent);
    if (ok && transferred != length)
    {
        ok = FALSE;
        error = ERROR_INVALID_DATA;
    }
    SetLastError (error);
    return ok;
}

static BOOL
pipe_write_sync (HANDLE pipe, const void *data, DWORD length)
{
    return pipe_transfer (pipe, (void *)data, length, FALSE, 100);
}

// Handle a fully-read client message: dispatch it, write the reply, and reset
// the instance to listen for the next connection.
// True if `client_path` is a GridFlux front-end (gridflux-gui/cli.exe) in the
// same directory as this server. That directory is the install location (under
// Program Files, writable only by administrators), so an unprivileged process
// cannot plant a look-alike binary there.
static bool
client_path_trusted (const wchar_t *client_path)
{
    wchar_t self[MAX_PATH];
    DWORD n = GetModuleFileNameW (NULL, self, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return false;

    wchar_t *self_slash = wcsrchr (self, L'\\');
    const wchar_t *cli_slash = wcsrchr (client_path, L'\\');
    if (!self_slash || !cli_slash)
        return false;

    size_t self_dir_len = (size_t)(self_slash - self);
    if (self_dir_len != (size_t)(cli_slash - client_path)
        || _wcsnicmp (self, client_path, self_dir_len) != 0)
        return false; // different directory

    const wchar_t *base = cli_slash + 1;
    return _wcsicmp (base, L"gridflux-gui.exe") == 0
           || _wcsicmp (base, L"gridflux-cli.exe") == 0;
}

// Authenticate the connected client so an arbitrary local process that obtained
// pipe access via the Interactive-Users ACE cannot feed crafted bytes to the
// (elevated) command parser. Only the trusted GridFlux front-ends are accepted.
static bool
pipe_client_trusted (HANDLE pipe)
{
    DWORD pid = 0;
    if (!GetNamedPipeClientProcessId (pipe, &pid))
        return false;

    HANDLE proc = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc)
        return false;

    wchar_t path[MAX_PATH];
    DWORD sz = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW (proc, 0, path, &sz);
    CloseHandle (proc);

    return ok && client_path_trusted (path);
}

static void
pipe_handle_message (gf_pipe_t *inst, DWORD bytes, void *user_data)
{
    inst->buffer[bytes] = '\0';

    gf_ipc_response_t response = { 0 };
    response.status = GF_IPC_SUCCESS;

    if (!pipe_client_trusted (inst->pipe))
        response.status = GF_IPC_ERROR_PERMISSION; // reject untrusted callers
    else
        gf_handle_client_message (inst->buffer, &response, user_data);

    if (!pipe_write_sync (inst->pipe, &response, sizeof (response)))
    {
        fprintf (stderr, "Pipe reply write failed: %lu\n", GetLastError ());
        pipe_reset (inst);
        return;
    }
    // Keep buffered reply bytes available until the client disconnects. A
    // FlushFileBuffers here waits indefinitely for that client to read them.
    inst->replied = TRUE;
    inst->connected_at = GetTickCount64 ();
}

// Advance one pipe instance: complete a pending connect, then service any
// readable client message. Returns true if a message was processed.
static bool
pipe_poll_instance (gf_pipe_t *inst, void *user_data)
{
    DWORD bytes = 0;
    if (inst->pending_io)
    {
        if (!GetOverlappedResult (inst->pipe, &inst->overlapped, &bytes, FALSE))
        {
            if (GetLastError () == ERROR_IO_INCOMPLETE)
                return false;
            pipe_reset (inst);
            return false;
        }
        inst->pending_io = FALSE;
        inst->connected = TRUE;
        inst->connected_at = GetTickCount64 ();
    }
    if (!inst->connected)
        return false;
    if (GetTickCount64 () - inst->connected_at >= GF_PIPE_TIMEOUT)
    {
        pipe_reset (inst);
        return false;
    }
    if (inst->replied)
    {
        if (!PeekNamedPipe (inst->pipe, NULL, 0, NULL, NULL, NULL))
            pipe_reset (inst);
        return false;
    }
    if (inst->read_pending)
    {
        if (!GetOverlappedResult (inst->pipe, &inst->overlapped, &bytes, FALSE))
        {
            if (GetLastError () == ERROR_IO_INCOMPLETE)
                return false;
            pipe_reset (inst);
            return false;
        }
        inst->read_pending = FALSE;
    }
    else
    {
        ResetEvent (inst->overlapped.hEvent);
        if (!ReadFile (inst->pipe, inst->buffer, sizeof (inst->buffer) - 1, &bytes,
                       &inst->overlapped))
        {
            if (GetLastError () == ERROR_IO_PENDING)
                inst->read_pending = TRUE;
            else
                pipe_reset (inst);
            return false;
        }
    }
    if (!bytes || bytes >= sizeof (inst->buffer))
    {
        pipe_reset (inst);
        return false;
    }
    pipe_handle_message (inst, bytes, user_data);
    return true;
}

bool
gf_ipc_server_process (gf_ipc_handle_t handle, void *user_data)
{
    (void)handle;

    if (!pipe_instances)
        return false;

    bool processed = false;
    for (int i = 0; i < num_instances; i++)
        if (pipe_poll_instance (&pipe_instances[i], user_data))
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
                                   OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

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
        return false;
    response->status = GF_IPC_ERROR_CONNECTION;
    HANDLE pipe = (HANDLE)handle;
    ULONGLONG start = GetTickCount64 ();
    if (!pipe_transfer (pipe, (void *)command, (DWORD)strlen (command), FALSE,
                        GF_PIPE_TIMEOUT))
        goto failed;
    ULONGLONG elapsed = GetTickCount64 () - start;
    if (elapsed >= GF_PIPE_TIMEOUT)
    {
        SetLastError (ERROR_TIMEOUT);
        goto failed;
    }
    if (!pipe_transfer (pipe, response, sizeof (*response), TRUE,
                        GF_PIPE_TIMEOUT - (DWORD)elapsed))
        goto failed;
    return true;
failed:
    response->status = GetLastError () == ERROR_TIMEOUT ? GF_IPC_ERROR_TIMEOUT
                                                        : GF_IPC_ERROR_CONNECTION;
    return false;
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
