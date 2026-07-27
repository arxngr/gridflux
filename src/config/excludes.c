#include "excludes.h"
#include "../utils/logger.h"
#include "../utils/memory.h"
#include "config.h"
#include <stdint.h>
#include <string.h>

static int
exclude_index_of (const gf_exclude_list_t *list, const char *wm_class)
{
    for (uint32_t i = 0; i < list->count; i++)
        if (gf_class_matches (list->items[i].wm_class, wm_class))
            return (int)i;
    return -1;
}

static gf_err_t
exclude_list_reserve (gf_exclude_list_t *list, uint32_t needed)
{
    if (list->capacity >= needed)
        return GF_SUCCESS;

    uint32_t cap = list->capacity ? list->capacity : 4;
    while (cap < needed)
    {
        if (cap > UINT32_MAX / 2)
            return GF_ERROR_MEMORY_ALLOCATION;
        cap *= 2;
    }

    gf_exclude_t *grown = gf_realloc (list->items, cap * sizeof (*grown));
    if (!grown)
        return GF_ERROR_MEMORY_ALLOCATION;

    list->items = grown;
    list->capacity = cap;
    return GF_SUCCESS;
}

static void
config_persist (const gf_config_t *cfg)
{
    const char *path = gf_config_get_path ();
    if (path)
        gf_config_save (path, cfg);
}

void
gf_exclude_list_free (gf_exclude_list_t *list)
{
    if (!list)
        return;
    gf_free (list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

gf_err_t
gf_exclude_list_copy (gf_exclude_list_t *dst, const gf_exclude_list_t *src)
{
    if (!dst || !src)
        return GF_ERROR_INVALID_PARAMETER;

    dst->items = NULL;
    dst->count = 0;
    dst->capacity = 0;

    if (src->count == 0)
        return GF_SUCCESS;

    gf_err_t err = exclude_list_reserve (dst, src->count);
    if (err != GF_SUCCESS)
        return err;

    memcpy (dst->items, src->items, src->count * sizeof (*dst->items));
    dst->count = src->count;
    return GF_SUCCESS;
}

gf_err_t
gf_exclude_list_push (gf_exclude_list_t *list, const char *wm_class)
{
    if (!list || !wm_class || wm_class[0] == '\0')
        return GF_ERROR_INVALID_PARAMETER;

    if (exclude_index_of (list, wm_class) >= 0)
        return GF_SUCCESS;

    gf_err_t err = exclude_list_reserve (list, list->count + 1);
    if (err != GF_SUCCESS)
        return err;

    gf_safe_strcpy (list->items[list->count].wm_class, GF_RULE_CLASS_MAX, wm_class);
    list->count++;
    return GF_SUCCESS;
}

bool
gf_exclude_list_contains (const gf_exclude_list_t *list, const char *window_class)
{
    if (!list || !window_class)
        return false;

    for (uint32_t i = 0; i < list->count; i++)
        if (gf_class_matches (list->items[i].wm_class, window_class))
            return true;
    return false;
}

gf_err_t
gf_excludes_add (gf_config_t *cfg, const char *wm_class)
{
    if (!cfg)
        return GF_ERROR_INVALID_PARAMETER;

    gf_err_t err = gf_exclude_list_push (&cfg->excluded_apps, wm_class);
    if (err == GF_SUCCESS)
    {
        GF_LOG_INFO ("Excluded app: %s", wm_class);
        config_persist (cfg);
    }
    return err;
}

gf_err_t
gf_excludes_remove (gf_config_t *cfg, const char *pattern)
{
    if (!cfg || !pattern)
        return GF_ERROR_INVALID_PARAMETER;

    gf_exclude_list_t *list = &cfg->excluded_apps;
    bool removed = false;

    for (uint32_t i = 0; i < list->count;)
    {
        if (gf_class_matches (list->items[i].wm_class, pattern))
        {
            memmove (&list->items[i], &list->items[i + 1],
                     (list->count - i - 1) * sizeof (*list->items));
            list->count--;
            removed = true;
        }
        else
            i++;
    }

    if (!removed)
        return GF_ERROR_WINDOW_NOT_FOUND;

    GF_LOG_INFO ("Removed exclusion: %s", pattern);
    config_persist (cfg);
    return GF_SUCCESS;
}

uint32_t
gf_excludes_count (const gf_config_t *cfg)
{
    return cfg ? cfg->excluded_apps.count : 0;
}
