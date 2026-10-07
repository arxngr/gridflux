#include "../../ipc/ipc.h"
#ifdef __unix__

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define GF_SOCKET_NAME "gridflux.sock"

static char socket_path[sizeof (((struct sockaddr_un *)0)->sun_path)] = { 0 };
static dev_t socket_device;
static ino_t socket_inode;

static bool
private_directory (const char *path)
{
    struct stat st;
    return lstat (path, &st) == 0 && S_ISDIR (st.st_mode) && st.st_uid == geteuid ()
           && !(st.st_mode & 0077);
}

const char *
gf_ipc_get_socket_path (void)
{
    if (socket_path[0] != '\0')
    {
        return socket_path;
    }

    const char *runtime_dir = getenv ("XDG_RUNTIME_DIR");

    int written;
    if (runtime_dir && private_directory (runtime_dir))
    {
        written = snprintf (socket_path, sizeof (socket_path), "%s/%s", runtime_dir,
                            GF_SOCKET_NAME);
    }
    else
    {
        const char *display = getenv ("DISPLAY");
        if (!display)
            display = ":0";

        for (size_t i = 0; display[i]; i++)
        {
            unsigned char c = (unsigned char)display[i];
            if (i >= 64
                || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                     || (c >= '0' && c <= '9') || c == ':' || c == '.' || c == '-'
                     || c == '_'))
                return NULL;
        }
        char directory[64];
        written = snprintf (directory, sizeof (directory), "/tmp/gridflux-%lu",
                            (unsigned long)geteuid ());
        if (written < 0 || (size_t)written >= sizeof (directory)
            || (mkdir (directory, 0700) < 0 && errno != EEXIST)
            || !private_directory (directory))
            return NULL;
        written = snprintf (socket_path, sizeof (socket_path), "%s/%s.sock", directory,
                            display);
    }
    if (written < 0 || (size_t)written >= sizeof (socket_path))
    {
        socket_path[0] = '\0';
        return NULL;
    }
    return socket_path;
}

gf_ipc_handle_t
gf_ipc_server_create (void)
{
    const char *path = gf_ipc_get_socket_path ();
    if (!path || socket_inode)
        return -1;

    int sock = socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sock < 0)
    {
        perror ("socket");
        return -1;
    }

    struct sockaddr_un addr = { 0 };
    addr.sun_family = AF_UNIX;
    strncpy (addr.sun_path, path, sizeof (addr.sun_path) - 1);
    struct stat st;
    if (lstat (path, &st) == 0)
    {
        if (!S_ISSOCK (st.st_mode) || st.st_uid != geteuid ())
        {
            close (sock);
            return -1;
        }
        int probe = socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (probe < 0)
        {
            close (sock);
            return -1;
        }
        int result = connect (probe, (struct sockaddr *)&addr, sizeof (addr));
        int error = errno;
        close (probe);
        if (result == 0 || error != ECONNREFUSED || unlink (path) < 0)
        {
            close (sock);
            return -1;
        }
    }
    else if (errno != ENOENT)
    {
        close (sock);
        return -1;
    }

    if (bind (sock, (struct sockaddr *)&addr, sizeof (addr)) < 0)
    {
        perror ("bind");
        close (sock);
        return -1;
    }

    if (lstat (path, &st) < 0 || chmod (path, 0600) < 0)
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
    if (flags < 0 || fcntl (sock, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        close (sock);
        unlink (path);
        return -1;
    }
    socket_device = st.st_dev;
    socket_inode = st.st_ino;

    printf ("IPC server listening on: %s\n", path);
    return sock;
}

void
gf_ipc_server_destroy (gf_ipc_handle_t handle)
{
    if (handle >= 0)
    {
        close (handle);
        struct stat st;
        if (socket_path[0] && lstat (socket_path, &st) == 0 && st.st_dev == socket_device
            && st.st_ino == socket_inode)
            unlink (socket_path);
        socket_inode = 0;
    }
}

static bool
verify_peer_credentials (int client_sock)
{
    struct ucred cred = { 0 };
    socklen_t len = sizeof (cred);

    if (getsockopt (client_sock, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0)
    {
        return false;
    }

    return len == sizeof (cred) && cred.uid == geteuid ();
}

static int64_t
milliseconds (void)
{
    struct timespec now;
    if (clock_gettime (CLOCK_MONOTONIC, &now) < 0)
        return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static bool
wait_socket (int fd, short events, int64_t deadline)
{
    for (;;)
    {
        int64_t now = milliseconds ();
        if (now < 0 || now >= deadline)
        {
            errno = ETIMEDOUT;
            return false;
        }
        struct pollfd p = { .fd = fd, .events = events };
        int ready = poll (&p, 1, (int)(deadline - now));
        if (ready > 0)
            return (p.revents & (events | POLLHUP)) != 0;
        if (ready < 0 && errno == EINTR)
            continue;
        if (!ready)
            errno = ETIMEDOUT;
        return false;
    }
}

// Send exactly len bytes, retrying on EINTR and looping over partial writes.
static bool
send_all (int fd, const void *buf, size_t len, int64_t deadline)
{
    const char *p = (const char *)buf;
    size_t sent = 0;

    while (sent < len)
    {
        if (!wait_socket (fd, POLLOUT, deadline))
            return false;
        ssize_t n = send (fd, p + sent, len - sent, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n > 0)
        {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        return false; // error or peer closed
    }
    return true;
}

// Receive exactly len bytes, retrying on EINTR and looping over partial reads.
static bool
recv_all (int fd, void *buf, size_t len, int64_t deadline)
{
    char *p = (char *)buf;
    size_t got = 0;

    while (got < len)
    {
        if (!wait_socket (fd, POLLIN, deadline))
            return false;
        ssize_t n = recv (fd, p + got, len - got, MSG_DONTWAIT);
        if (n > 0)
        {
            got += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        return false; // error or peer closed
    }
    return true;
}

bool
gf_ipc_server_process (gf_ipc_handle_t handle, void *user_data)
{
    int client = accept4 (handle, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (client < 0)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            perror ("accept");
        }
        return false;
    }

    if (!verify_peer_credentials (client))
    {
        close (client);
        return false;
    }

    int64_t deadline = milliseconds () + 100;
    char buffer[GF_IPC_MSG_SIZE];
    size_t length = 0;
    bool complete = false;
    // EOF on the client's write half frames the whole request. A partial stream
    // prefix must never be interpreted as an independently valid command.
    while (length < sizeof (buffer) && wait_socket (client, POLLIN, deadline))
    {
        ssize_t n
            = recv (client, buffer + length, sizeof (buffer) - length, MSG_DONTWAIT);
        if (n > 0)
            length += (size_t)n;
        else if (!n)
        {
            complete = true;
            break;
        }
        else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            break;
    }
    gf_ipc_response_t response = { .status = GF_IPC_ERROR_INVALID_COMMAND };
    if (complete && length && length < sizeof (buffer))
        gf_handle_client_message (buffer, length, &response, user_data);
    send_all (client, &response, sizeof (response), deadline);

    close (client);
    return true;
}

gf_ipc_handle_t
gf_ipc_client_connect (void)
{
    const char *path = gf_ipc_get_socket_path ();
    struct stat st;
    if (!path || lstat (path, &st) < 0 || !S_ISSOCK (st.st_mode)
        || st.st_uid != geteuid () || (st.st_mode & 0077))
        return -1;

    int sock = socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (sock < 0)
    {
        perror ("socket");
        return -1;
    }

    struct sockaddr_un addr = { 0 };
    addr.sun_family = AF_UNIX;
    strncpy (addr.sun_path, path, sizeof (addr.sun_path) - 1);

    if (connect (sock, (struct sockaddr *)&addr, sizeof (addr)) < 0
        || !verify_peer_credentials (sock))
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

    memset (response, 0, sizeof (*response));
    size_t len;
    if (!gf_ipc_command_length (command, &len))
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        return false;
    }
    int64_t deadline = milliseconds () + 1000;
    if (!send_all (handle, command, len, deadline) || shutdown (handle, SHUT_WR) < 0
        || !recv_all (handle, response, sizeof (*response), deadline)
        || !gf_ipc_response_valid (response))
    {
        response->status
            = errno == ETIMEDOUT ? GF_IPC_ERROR_TIMEOUT : GF_IPC_ERROR_CONNECTION;
        memset (response->message, 0, sizeof (response->message));
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

#endif // __unix__
