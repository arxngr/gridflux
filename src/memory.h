
#ifndef GF_UTILS_MEMORY_H
#define GF_UTILS_MEMORY_H

#include "types.h"

// Safe memory operations
void *gf_malloc (size_t size);
void *gf_calloc (size_t count, size_t size);
void *gf_realloc (void *ptr, size_t size);
void gf_free (void *ptr);

// String operations
char *gf_strdup (const char *str);
gf_error_code_t gf_safe_strcpy (char *dest, size_t dest_size, const char *src);

#endif // GF_UTILS_MEMORY_H
