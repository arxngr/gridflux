#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int
gf_copy_file (const char *src, const char *dst)
{
    int in = -1, out = -1;
    char buf[8192];
    ssize_t r;

    in = open (src, O_RDONLY);
    if (in < 0)
        goto error;

    out = open (dst, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (out < 0)
        goto error;

    while ((r = read (in, buf, sizeof (buf))) > 0)
    {
        ssize_t w = write (out, buf, r);
        if (w != r)
            goto error;
    }

    if (r < 0)
        goto error;

    close (in);
    close (out);
    return 0;

error:
    if (in >= 0)
        close (in);
    if (out >= 0)
        close (out);
    return -1;
}
