#include "memory.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

void *
gf_malloc (size_t size)
{
    if (size == 0)
        return NULL;

    void *ptr = malloc (size);
    if (!ptr)
    {
        GF_LOG_ERROR ("Memory allocation failed for size %zu", size);
    }
    return ptr;
}

void *
gf_calloc (size_t count, size_t size)
{
    if (count == 0 || size == 0)
        return NULL;

    void *ptr = calloc (count, size);
    if (!ptr)
    {
        GF_LOG_ERROR ("Memory allocation failed for %zu items of size %zu", count, size);
    }
    return ptr;
}

void *
gf_realloc (void *ptr, size_t size)
{
    if (size == 0)
    {
        free (ptr);
        return NULL;
    }

    void *new_ptr = realloc (ptr, size);
    if (!new_ptr)
    {
        GF_LOG_ERROR ("Memory reallocation failed for size %zu", size);
        return ptr; // Return original pointer on failure
    }
    return new_ptr;
}

void
gf_free (void *ptr)
{
    free (ptr);
}

char *
gf_strdup (const char *str)
{
    if (!str)
        return NULL;

    size_t len = strlen (str) + 1;
    char *dup = gf_malloc (len);
    if (dup)
    {
        memcpy (dup, str, len);
    }
    return dup;
}

gf_err_t
gf_safe_strcpy (char *dest, size_t dest_size, const char *src)
{
    if (!dest || !src || dest_size == 0)
    {
        return GF_ERROR_INVALID_PARAMETER;
    }

    strncpy (dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
    return GF_SUCCESS;
}
