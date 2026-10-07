#include "ipc/ipc.h"
#include <windows.h>

#include <assert.h>
#include <sddl.h>
#include <stdio.h>

static DWORD identity_tls;
static bool reject_peer;
static bool fail_security;

static BOOL WINAPI
test_server_pid (HANDLE pipe, PULONG pid)
{
    TlsSetValue (identity_tls, (LPVOID)1);
    return GetNamedPipeServerProcessId (pipe, pid);
}

static BOOL WINAPI
test_client_pid (HANDLE pipe, PULONG pid)
{
    TlsSetValue (identity_tls, NULL);
    return GetNamedPipeClientProcessId (pipe, pid);
}

static BOOL WINAPI
test_security_descriptor (LPCSTR text, DWORD revision, PSECURITY_DESCRIPTOR *descriptor,
                          PULONG size)
{
    if (fail_security)
    {
        SetLastError (ERROR_INVALID_SECURITY_DESCR);
        return FALSE;
    }
    return ConvertStringSecurityDescriptorToSecurityDescriptorA (text, revision,
                                                                 descriptor, size);
}

static BOOL WINAPI
trusted_client_image (HANDLE process, DWORD flags, LPWSTR path, PDWORD size)
{
    (void)process;
    (void)flags;
    wchar_t self[MAX_PATH];
    DWORD length = GetModuleFileNameW (NULL, self, MAX_PATH);
    assert (length > 0 && length < MAX_PATH);
    wchar_t *base = wcsrchr (self, L'\\');
    assert (base);
    const wchar_t *name = reject_peer                  ? L"untrusted.exe"
                          : TlsGetValue (identity_tls) ? L"gridflux.exe"
                                                       : L"gridflux-cli.exe";
    assert ((size_t)(base + 1 - self) + wcslen (name) < MAX_PATH);
    wcscpy (base + 1, name);
    assert (*size > wcslen (self));
    wcscpy (path, self);
    *size = (DWORD)wcslen (self);
    return TRUE;
}

#define QueryFullProcessImageNameW trusted_client_image
#define GetNamedPipeServerProcessId test_server_pid
#define GetNamedPipeClientProcessId test_client_pid
#define ConvertStringSecurityDescriptorToSecurityDescriptorA test_security_descriptor
#include "../../src/platform/windows/ipc.c"
#undef QueryFullProcessImageNameW
#undef GetNamedPipeServerProcessId
#undef GetNamedPipeClientProcessId
#undef ConvertStringSecurityDescriptorToSecurityDescriptorA

static int requests;

void
gf_handle_client_message (const char *message, size_t length, gf_ipc_response_t *response,
                          void *data)
{
    (void)data;
    assert (strcmp (message, "query test") == 0);
    assert (length == strlen ("query test"));
    requests++;
    response->status = GF_IPC_SUCCESS;
    strcpy (response->message, "reply");
}

static void
select_test_pipe (unsigned sequence)
{
    snprintf (pipe_name, sizeof (pipe_name), "\\\\.\\pipe\\GridFluxIpcTest%lu-%u",
              GetCurrentProcessId (), sequence);
}

static DWORD WINAPI
client_requests (LPVOID data)
{
    unsigned rounds = (unsigned)(UINT_PTR)data;
    for (unsigned i = 0; i < rounds; i++)
    {
        gf_ipc_handle_t client = gf_ipc_client_connect ();
        assert (client != -1);
        // The server must retain its one pending read while this client waits.
        Sleep (20);
        gf_ipc_response_t response;
        assert (gf_ipc_client_send (client, "query test", &response));
        assert (response.status == GF_IPC_SUCCESS
                && strcmp (response.message, "reply") == 0);
        gf_ipc_client_disconnect (client);
    }
    return 0;
}

static void
test_pending_reads (void)
{
    select_test_pipe (1);
    assert (gf_ipc_server_create () >= 0);
    HANDLE threads[4];
    for (unsigned i = 0; i < 4; i++)
        threads[i]
            = CreateThread (NULL, 0, client_requests, (LPVOID)(UINT_PTR)8, 0, NULL);
    ULONGLONG start = GetTickCount64 ();
    unsigned pending_polls = 0;
    while (WaitForMultipleObjects (4, threads, TRUE, 0) != WAIT_OBJECT_0)
    {
        assert (GetTickCount64 () - start < 5000);
        gf_ipc_server_process (0, NULL);
        for (int i = 0; i < num_instances; i++)
            if (pipe_instances[i].read_pending)
                pending_polls++;
        Sleep (1);
    }
    assert (pending_polls > 32 && requests == 32);
    for (unsigned i = 0; i < 4; i++)
        CloseHandle (threads[i]);
    // A status probe connects and closes without sending a command.
    gf_ipc_handle_t probe = gf_ipc_client_connect ();
    assert (probe != -1);
    gf_ipc_server_process (0, NULL);
    gf_ipc_client_disconnect (probe);
    gf_ipc_server_process (0, NULL);
    gf_ipc_server_destroy (0);
}

static gf_ipc_handle_t
connect_fixture (gf_pipe_t *server, unsigned sequence, DWORD buffer_size)
{
    select_test_pipe (sequence);
    memset (server, 0, sizeof (*server));
    server->pipe = CreateNamedPipeA (pipe_name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                     PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT
                                         | PIPE_REJECT_REMOTE_CLIENTS,
                                     1, buffer_size, buffer_size, 0, NULL);
    assert (server->pipe != INVALID_HANDLE_VALUE);
    server->overlapped.hEvent = CreateEvent (NULL, TRUE, FALSE, NULL);
    assert (server->overlapped.hEvent && connect_to_client (server));
    gf_ipc_handle_t client = gf_ipc_client_connect ();
    assert (client != -1);
    DWORD ignored;
    assert (GetOverlappedResult (server->pipe, &server->overlapped, &ignored, TRUE));
    server->pending_io = FALSE;
    server->connected = TRUE;
    server->connected_at = GetTickCount64 ();
    return client;
}

static void
close_fixture (gf_pipe_t *server)
{
    if (server->pending_io || server->read_pending)
    {
        DWORD ignored;
        CancelIoEx (server->pipe, &server->overlapped);
        GetOverlappedResult (server->pipe, &server->overlapped, &ignored, TRUE);
    }
    CloseHandle (server->pipe);
    CloseHandle (server->overlapped.hEvent);
}

static void
test_client_deadlines (void)
{
    gf_pipe_t server;
    gf_ipc_handle_t client = connect_fixture (&server, 2, 16384);
    gf_ipc_response_t response;
    ULONGLONG start = GetTickCount64 ();
    assert (!gf_ipc_client_send (client, "query test", &response));
    assert (response.status == GF_IPC_ERROR_TIMEOUT);
    assert (GetTickCount64 () - start < GF_PIPE_TIMEOUT + 500);
    gf_ipc_client_disconnect (client);
    close_fixture (&server);

    client = connect_fixture (&server, 3, 1);
    char command[GF_IPC_MSG_SIZE];
    memset (command, 'x', sizeof (command) - 1);
    command[sizeof (command) - 1] = '\0';
    start = GetTickCount64 ();
    assert (!gf_ipc_client_send (client, command, &response));
    assert (response.status == GF_IPC_ERROR_TIMEOUT);
    assert (GetTickCount64 () - start < GF_PIPE_TIMEOUT + 500);
    gf_ipc_client_disconnect (client);
    close_fixture (&server);

    client = connect_fixture (&server, 4, 16384);
    char partial[] = "short reply";
    assert (pipe_write_sync (server.pipe, partial, sizeof (partial)));
    assert (!gf_ipc_client_send (client, "query test", &response));
    assert (response.status == GF_IPC_ERROR_CONNECTION);
    gf_ipc_client_disconnect (client);
    close_fixture (&server);
}

static void
test_permissions (void)
{
    select_test_pipe (8);
    fail_security = true;
    assert (gf_ipc_server_create () == -1);
    assert (!pipe_instances && num_instances == 0);
    fail_security = false;
    assert (gf_ipc_server_create () == 0);
    assert (gf_ipc_server_create () == -1); // No leaked replacement server.
    reject_peer = true;
    assert (gf_ipc_client_connect () == -1);
    reject_peer = false;
    gf_ipc_server_destroy (0);
    gf_pipe_t server;
    gf_ipc_handle_t client = connect_fixture (&server, 9, 16384);
    assert (pipe_transfer ((HANDLE)client, "query test", 10, FALSE, 100));
    int before = requests;
    reject_peer = true;
    assert (pipe_poll_instance (&server, NULL));
    reject_peer = false;
    gf_ipc_response_t response;
    assert (pipe_transfer ((HANDLE)client, &response, sizeof (response), TRUE, 100));
    assert (response.status == GF_IPC_ERROR_PERMISSION && requests == before);
    gf_ipc_client_disconnect (client);
    close_fixture (&server);
    client = connect_fixture (&server, 10, 16384);
    char too_long[GF_IPC_MSG_SIZE];
    memset (too_long, 'x', sizeof (too_long));
    assert (!gf_ipc_client_send (client, too_long, &response));
    assert (response.status == GF_IPC_ERROR_INVALID_COMMAND);
    DWORD available;
    assert (PeekNamedPipe (server.pipe, NULL, 0, NULL, &available, NULL) && !available);
    gf_ipc_response_t invalid = { 0 };
    memset (&invalid.status, 0xff, sizeof (invalid.status));
    assert (pipe_write_sync (server.pipe, &invalid, sizeof (invalid)));
    assert (!gf_ipc_client_send (client, "query test", &response));
    assert (response.status == GF_IPC_ERROR_CONNECTION);
    gf_ipc_client_disconnect (client);
    close_fixture (&server);
    client = connect_fixture (&server, 11, 16384);
    char oversized[GF_IPC_MSG_SIZE];
    memset (oversized, 'x', sizeof (oversized));
    before = requests;
    assert (pipe_transfer ((HANDLE)client, oversized, sizeof (oversized), FALSE, 100));
    pipe_poll_instance (&server, NULL);
    assert (requests == before && !server.connected);
    gf_ipc_client_disconnect (client);
    close_fixture (&server);
    // Inspect the interactive ACE: clients cannot create a new server instance.
    SECURITY_ATTRIBUTES *sa = pipe_security_attributes ();
    PACL acl;
    BOOL present, defaulted;
    assert (sa
            && GetSecurityDescriptorDacl (sa->lpSecurityDescriptor, &present, &acl,
                                          &defaulted));
    assert (present && acl && acl->AceCount == 4);
    ACCESS_ALLOWED_ACE *ace;
    assert (GetAce (acl, 3, (void **)&ace));
    assert (!(ace->Mask & FILE_CREATE_PIPE_INSTANCE));
    assert ((ace->Mask & (FILE_READ_DATA | FILE_WRITE_DATA))
            == (FILE_READ_DATA | FILE_WRITE_DATA));
    LocalFree (sa->lpSecurityDescriptor);
    free (sa);
}

static void
test_paths (void)
{
    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    TlsSetValue (identity_tls, NULL);
    assert (trusted_client_image (NULL, 0, path, &size));
    assert (peer_path_trusted (path, false) && !peer_path_trusted (path, true));
    TlsSetValue (identity_tls, (LPVOID)1);
    size = MAX_PATH;
    assert (trusted_client_image (NULL, 0, path, &size));
    assert (peer_path_trusted (path, true) && !peer_path_trusted (path, false));
    assert (!peer_path_trusted (L"C:\\untrusted\\gridflux-cli.exe", false));
}

static void
test_reply_delivery (void)
{
    gf_pipe_t server;
    gf_ipc_handle_t client = connect_fixture (&server, 5, 16384);
    assert (pipe_transfer ((HANDLE)client, "query test", 10, FALSE, 100));
    ULONGLONG start = GetTickCount64 ();
    assert (pipe_poll_instance (&server, NULL) && server.replied);
    for (int i = 0; i < 10; i++)
        assert (!pipe_poll_instance (&server, NULL));
    assert (GetTickCount64 () - start < 250);
    DWORD available;
    assert (PeekNamedPipe ((HANDLE)client, NULL, 0, NULL, &available, NULL));
    assert (available == sizeof (gf_ipc_response_t));
    gf_ipc_response_t response;
    assert (pipe_transfer ((HANDLE)client, &response, sizeof (response), TRUE, 100));
    assert (response.status == GF_IPC_SUCCESS && strcmp (response.message, "reply") == 0);
    gf_ipc_client_disconnect (client);
    pipe_poll_instance (&server, NULL);
    assert (!server.connected);
    close_fixture (&server);

    client = connect_fixture (&server, 6, 1);
    pipe_poll_instance (&server, NULL);
    assert (server.read_pending);
    assert (pipe_transfer ((HANDLE)client, "query test", 10, FALSE, 100));
    start = GetTickCount64 ();
    pipe_poll_instance (&server, NULL);
    assert (GetTickCount64 () - start < 350 && !server.connected);
    gf_ipc_client_disconnect (client);
    close_fixture (&server);

    client = connect_fixture (&server, 7, 16384);
    pipe_poll_instance (&server, NULL);
    assert (server.read_pending);
    server.connected_at -= GF_PIPE_TIMEOUT;
    pipe_poll_instance (&server, NULL);
    assert (!server.connected && !server.read_pending);
    gf_ipc_client_disconnect (client);
    close_fixture (&server);
}

int
main (void)
{
    identity_tls = TlsAlloc ();
    assert (identity_tls != TLS_OUT_OF_INDEXES);
    test_pending_reads ();
    test_client_deadlines ();
    test_reply_delivery ();
    test_permissions ();
    test_paths ();
    TlsFree (identity_tls);
    puts ("Windows IPC deadlines, pending reads, and reply delivery passed");
    return 0;
}
