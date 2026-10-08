#include "../../src/platform/unix/ipc.c"
#include "ipc/ipc.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static unsigned requests;

void
gf_handle_client_message (const char *message, size_t length, gf_ipc_response_t *response,
                          void *data)
{
    (void)data;
    response->status = GF_IPC_ERROR_INVALID_COMMAND;
    if (length == 10 && memcmp (message, "query test", 10) == 0)
    {
        requests++;
        response->status = GF_IPC_SUCCESS;
        strcpy (response->message, "reply");
    }
}

static void *
client_request (void *data)
{
    (void)data;
    gf_ipc_handle_t client = gf_ipc_client_connect ();
    assert (client >= 0);
    gf_ipc_response_t response;
    assert (gf_ipc_client_send (client, "query test", &response));
    assert (response.status == GF_IPC_SUCCESS && strcmp (response.message, "reply") == 0);
    gf_ipc_client_disconnect (client);
    return NULL;
}

static void *
fragmented_request (void *data)
{
    bool suffix = *(bool *)data;
    int client = (int)gf_ipc_client_connect ();
    assert (client >= 0);
    int64_t deadline = milliseconds () + 1000;
    assert (send_all (client, "query ", 6, deadline));
    usleep (10000);
    assert (send_all (client, "test", 4, deadline));
    if (suffix)
        assert (send_all (client, " trailing", 9, deadline));
    assert (shutdown (client, SHUT_WR) == 0);
    gf_ipc_response_t response;
    assert (recv_all (client, &response, sizeof (response), deadline));
    assert (response.status == (suffix ? GF_IPC_ERROR_INVALID_COMMAND : GF_IPC_SUCCESS));
    close (client);
    return NULL;
}

static void
serve_thread (gf_ipc_handle_t server, void *(*work) (void *), void *data)
{
    pthread_t thread;
    assert (pthread_create (&thread, NULL, work, data) == 0);
    int64_t deadline = milliseconds () + 2000;
    while (!gf_ipc_server_process (server, NULL))
    {
        assert (milliseconds () < deadline);
        usleep (1000);
    }
    assert (pthread_join (thread, NULL) == 0);
}

static void
test_deadlines (gf_ipc_handle_t server)
{
    int client = (int)gf_ipc_client_connect ();
    assert (client >= 0);
    int64_t start = milliseconds ();
    assert (send_all (client, "query test", 10, start + 1000));
    // An unframed prefix must time out instead of running a command.
    unsigned before = requests;
    assert (gf_ipc_server_process (server, NULL));
    assert (requests == before && milliseconds () - start < 500);
    close (client);
    client = (int)gf_ipc_client_connect ();
    assert (client >= 0);
    char huge[GF_IPC_MSG_SIZE];
    memset (huge, 'x', sizeof (huge));
    memcpy (huge, "query test", 10);
    assert (send_all (client, huge, sizeof (huge), milliseconds () + 1000));
    assert (shutdown (client, SHUT_WR) == 0);
    assert (gf_ipc_server_process (server, NULL) && requests == before);
    close (client);
    int pair[2];
    assert (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0);
    assert (verify_peer_credentials (pair[0]));
    gf_ipc_response_t response;
    assert (send_all (pair[1], "short reply", 11, milliseconds () + 1000));
    assert (shutdown (pair[1], SHUT_WR) == 0);
    errno = ETIMEDOUT;
    assert (!gf_ipc_client_send (pair[0], "query test", &response));
    assert (response.status == GF_IPC_ERROR_CONNECTION && errno == ECONNRESET);
    close (pair[0]);
    close (pair[1]);
    assert (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0);
    memset (&response, 0, sizeof (response));
    response.status = (gf_ipc_status_t)99;
    assert (send_all (pair[1], &response, sizeof (response), milliseconds () + 1000));
    errno = ETIMEDOUT;
    assert (!gf_ipc_client_send (pair[0], "query test", &response));
    assert (response.status == GF_IPC_ERROR_CONNECTION && errno == EPROTO);
    assert (response.message[0] == '\0');
    close (pair[0]);
    close (pair[1]);
    assert (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0);
    errno = ECONNRESET;
    assert (!gf_ipc_client_send (pair[0], "query test", &response));
    assert (response.status == GF_IPC_ERROR_TIMEOUT && errno == ETIMEDOUT);
    close (pair[0]);
    close (pair[1]);
    assert (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0);
    int invalid = pair[1];
    close (pair[1]);
    errno = ETIMEDOUT;
    assert (!wait_socket (invalid, POLLIN, milliseconds () + 1000) && errno == EBADF);
    errno = ETIMEDOUT;
    assert (!send_all (pair[0], "x", 1, milliseconds () + 1000)); // No SIGPIPE.
    assert (errno != ETIMEDOUT);
    close (pair[0]);
}

int
main (void)
{
    char directory[] = "/tmp/gridflux-ipc-test-XXXXXX";
    assert (mkdtemp (directory) && setenv ("XDG_RUNTIME_DIR", directory, 1) == 0);
    assert (private_directory (directory));
    const char *path = gf_ipc_get_socket_path ();
    assert (path && strlen (path) < sizeof (((struct sockaddr_un *)0)->sun_path));
    FILE *file = fopen (path, "w");
    assert (file && fclose (file) == 0);
    assert (gf_ipc_server_create () == -1 && access (path, F_OK) == 0);
    assert (unlink (path) == 0);
    gf_ipc_handle_t server = gf_ipc_server_create ();
    assert (server >= 0 && gf_ipc_server_create () == -1);
    struct stat st;
    assert (lstat (path, &st) == 0 && (st.st_mode & 0777) == 0600);
    serve_thread (server, client_request, NULL);
    bool suffix = false;
    serve_thread (server, fragmented_request, &suffix);
    suffix = true;
    serve_thread (server, fragmented_request, &suffix);
    assert (requests == 2);
    test_deadlines (server);
    gf_ipc_server_destroy (server);
    assert (access (path, F_OK) < 0 && rmdir (directory) == 0);
    puts ("Unix IPC private endpoint, fragmented requests, deadlines, and peer "
          "credentials passed");
    return 0;
}
