#include "ipc/ipc.h"
#include <assert.h>
#include <stdio.h>
#include <windows.h>

static BOOL WINAPI
trusted_client_image (HANDLE process, DWORD flags, LPWSTR path, PDWORD size)
{
    (void)process;
    (void)flags;
    wchar_t self[MAX_PATH];
    assert (GetModuleFileNameW (NULL, self, MAX_PATH));
    wchar_t *base = wcsrchr (self, L'\\');
    assert (base);
    wcscpy (base + 1, L"gridflux-cli.exe");
    assert (*size > wcslen (self));
    wcscpy (path, self);
    *size = (DWORD)wcslen (self);
    return TRUE;
}

#define QueryFullProcessImageNameW trusted_client_image
#include "../src/platform/windows/ipc.c"
#undef QueryFullProcessImageNameW

static int requests;

void
gf_handle_client_message (const char *message, gf_ipc_response_t *response, void *data)
{
    (void)data;
    assert (strcmp (message, "query test") == 0);
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
    test_pending_reads ();
    test_client_deadlines ();
    test_reply_delivery ();
    puts ("Windows IPC deadlines, pending reads, and reply delivery passed");
    return 0;
}
