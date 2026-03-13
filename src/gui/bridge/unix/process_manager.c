#include "../process_manager.h"

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define GF_SERVER_EXE "gridflux"

static pid_t
find_server_pid (void)
{
    DIR *proc_dir = opendir ("/proc");
    if (!proc_dir)
        return 0;

    pid_t my_pid = getpid ();
    pid_t found_pid = 0;
    struct dirent *entry;

    while ((entry = readdir (proc_dir)) != NULL)
    {
        // only look at numeric directories (PIDs)
        char *endptr;
        long pid = strtol (entry->d_name, &endptr, 10);
        if (*endptr != '\0' || pid <= 0)
            continue;

        // skip our own process
        if ((pid_t)pid == my_pid)
            continue;

        // read /proc/<pid>/comm to get process name
        char comm_path[64];
        snprintf (comm_path, sizeof (comm_path), "/proc/%ld/comm", pid);

        FILE *f = fopen (comm_path, "r");
        if (!f)
            continue;

        char comm[256] = { 0 };
        if (fgets (comm, sizeof (comm), f))
        {
            // remove trailing newline
            size_t len = strlen (comm);
            if (len > 0 && comm[len - 1] == '\n')
                comm[len - 1] = '\0';

            if (strcmp (comm, GF_SERVER_EXE) == 0)
            {
                found_pid = (pid_t)pid;
                fclose (f);
                break;
            }
        }
        fclose (f);
    }

    closedir (proc_dir);
    return found_pid;
}

static void
get_exe_dir (char *buf, size_t buf_len)
{
    ssize_t len = readlink ("/proc/self/exe", buf, buf_len - 1);
    if (len <= 0)
    {
        buf[0] = '\0';
        return;
    }
    buf[len] = '\0';

    char *last_sep = strrchr (buf, '/');
    if (last_sep)
        *(last_sep + 1) = '\0';
}

bool
gf_server_is_running (void)
{
    return find_server_pid () != 0;
}

bool
gf_server_start (void)
{
    if (gf_server_is_running ())
        return true;

    char dir[1024] = { 0 };
    char exe_path[1024] = { 0 };

    get_exe_dir (dir, sizeof (dir));
    snprintf (exe_path, sizeof (exe_path), "%s" GF_SERVER_EXE, dir);

    // check executable exists
    if (access (exe_path, X_OK) != 0)
        return false;

    pid_t pid = fork ();
    if (pid < 0)
        return false;

    if (pid == 0)
    {
        // child: detach and exec
        setsid ();

        // redirect stdout/stderr to /dev/null
        freopen ("/dev/null", "w", stdout);
        freopen ("/dev/null", "w", stderr);

        execl (exe_path, GF_SERVER_EXE, (char *)NULL);
        _exit (1); // exec failed
    }

    // parent: success
    return true;
}

bool
gf_server_stop (void)
{
    pid_t pid = find_server_pid ();
    if (pid == 0)
        return false;

    return kill (pid, SIGTERM) == 0;
}
