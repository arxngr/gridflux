#ifndef GF_CONFIG_EXCLUDES_H
#define GF_CONFIG_EXCLUDES_H

#include "../core/types.h"
#include "rules.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    char wm_class[GF_RULE_CLASS_MAX];
} gf_exclude_t;

typedef struct
{
    gf_exclude_t *items;
    uint32_t count;
    uint32_t capacity;
} gf_exclude_list_t;

struct gf_config;

// --- List lifecycle ---
void gf_exclude_list_free (gf_exclude_list_t *list);
gf_err_t gf_exclude_list_copy (gf_exclude_list_t *dst, const gf_exclude_list_t *src);
gf_err_t gf_exclude_list_push (gf_exclude_list_t *list, const char *wm_class);
bool gf_exclude_list_contains (const gf_exclude_list_t *list, const char *window_class);

// --- Config CRUD (persists on change) ---
gf_err_t gf_excludes_add (struct gf_config *cfg, const char *wm_class);
// Remove every entry that matches `pattern` (exact class|exe or exe component),
// so a window restores whether pattern is a typed class or a resolved identity.
gf_err_t gf_excludes_remove (struct gf_config *cfg, const char *pattern);
uint32_t gf_excludes_count (const struct gf_config *cfg);

#endif // GF_CONFIG_EXCLUDES_H
