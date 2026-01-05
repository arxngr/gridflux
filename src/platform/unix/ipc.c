#include "ipc.h"
#ifdef __unix__

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define GF_SOCKET_NAME "gridflux.sock"

static char socket_path[256] = { 0 };

const char *
gf_ipc_get_socket_path (void)
{
    if (socket_path[0] != '\0')
    {
        return socket_path;
    }

    const char *runtime_dir = getenv ("XDG_RUNTIME_DIR");

    if (runtime_dir && access (runtime_dir, W_OK) == 0)
    {
        snprintf (socket_path, sizeof (socket_path), "%s/%s", runtime_dir,
                  GF_SOCKET_NAME);
    }
    else
    {
        const char *display = getenv ("DISPLAY");
        if (!display)
            display = ":0";

        snprintf (socket_path, sizeof (socket_path), "/tmp/gridflux_%d%s-socket",
                  getuid (), display);
    }

    return socket_path;
}

gf_ipc_handle_t
gf_ipc_server_create (void)
{
    const char *path = gf_ipc_get_socket_path ();

    int sock = socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sock < 0)
    {
        perror ("socket");
        return -1;
    }

    unlink (path);

    struct sockaddr_un addr = { 0 };
    addr.sun_family = AF_UNIX;
    strncpy (addr.sun_path, path, sizeof (addr.sun_path) - 1);

    if (bind (sock, (struct sockaddr *)&addr, sizeof (addr)) < 0)
    {
        perror ("bind");
        close (sock);
        return -1;
    }

    if (chmod (path, 0600) < 0)
    {
        perror ("chmod");
        close (sock);
        unlink (path);
        return -1;
    }

    if (listen (sock, 10) < 0)
    {
        perror ("listen");
        close (sock);
        unlink (path);
        return -1;
    }

    int flags = fcntl (sock, F_GETFL, 0);
    fcntl (sock, F_SETFL, flags | O_NONBLOCK);

    printf ("IPC server listening on: %s\n", path);
    return sock;
}

void
gf_ipc_server_destroy (gf_ipc_handle_t handle)
{
    if (handle >= 0)
    {
        close (handle);
        unlink (gf_ipc_get_socket_path ());
    }
}

static bool
gf_verify_peer_credentials (int client_sock)
{
    struct ucred cred;
    socklen_t len = sizeof (cred);

    if (getsockopt (client_sock, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0)
    {
        return false;
    }

    return cred.uid == getuid ();
}

bool
gf_ipc_server_process (gf_ipc_handle_t handle, void *user_data)
{
    int client = accept (handle, NULL, NULL);
    if (client < 0)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            perror ("accept");
        }
        return false;
    }

    if (!gf_verify_peer_credentials (client))
    {
        close (client);
        return false;
    }

    struct timeval timeout = { 5, 0 };
    setsockopt (client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout));
    setsockopt (client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof (timeout));

    char buffer[GF_IPC_MSG_SIZE];
    ssize_t n = recv (client, buffer, sizeof (buffer) - 1, 0);

    if (n > 0)
    {
        buffer[n] = '\0';

        gf_ipc_response_t response = { 0 };
        response.status = GF_IPC_SUCCESS;

        gf_handle_client_message (buffer, &response, user_data);

        send (client, &response, sizeof (response), 0);
    }

    close (client);
    return true;
}

gf_ipc_handle_t
gf_ipc_client_connect (void)
{
    const char *path = gf_ipc_get_socket_path ();

    int sock = socket (AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror ("socket");
        return -1;
    }

    struct sockaddr_un addr = { 0 };
    addr.sun_family = AF_UNIX;
    strncpy (addr.sun_path, path, sizeof (addr.sun_path) - 1);

    if (connect (sock, (struct sockaddr *)&addr, sizeof (addr)) < 0)
    {
        perror ("connect");
        close (sock);
        return -1;
    }

    return sock;
}

bool
gf_ipc_client_send (gf_ipc_handle_t handle, const char *command,
                    gf_ipc_response_t *response)
{
    if (handle < 0 || !command || !response)
    {
        return false;
    }

    struct timeval timeout = { 5, 0 };
    setsockopt (handle, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout));
    setsockopt (handle, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof (timeout));

    size_t len = strlen (command);
    if (send (handle, command, len, 0) != (ssize_t)len)
    {
        perror ("send");
        return false;
    }

    ssize_t n = recv (handle, response, sizeof (*response), 0);
    if (n != sizeof (*response))
    {
        perror ("recv");
        return false;
    }

    return true;
}

void
gf_ipc_client_disconnect (gf_ipc_handle_t handle)
{
    if (handle >= 0)
    {
        close (handle);
    }
}

#endif /* __unix__ */
