#include "config.h"
#include "core/logger.h"
#include "types.h"
#include <json-c/json.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const gf_config_t DEFAULT_CONFIG
    = { .max_windows_per_workspace = GF_MAX_WINDOWS_PER_WORKSPACE,
        .max_workspaces = GF_MAX_WORKSPACES,
        .default_padding = GF_DEFAULT_PADDING,
        .min_window_size = GF_MIN_WINDOW_SIZE,
        .locked_workspace_count = 0 };

const char *
gf_config_get_path (void)
{
    static char config_path[PATH_MAX];

#ifdef GF_DEV_MODE
    strncpy (config_path, "config.json", sizeof (config_path) - 1);
    config_path[sizeof (config_path) - 1] = '\0';
    return config_path;
#else

    // Try XDG_CONFIG_HOME first (follows XDG Base Directory spec)
    const char *xdg_config = getenv ("XDG_CONFIG_HOME");
    if (xdg_config && xdg_config[0] != '\0')
    {
        int ret = snprintf (config_path, sizeof (config_path), "%s/gridflux/config.json",
                            xdg_config);
        GF_LOG_DEBUG ("Installation mode, XDG Path: ", config_path);
        if (ret > 0 && (size_t)ret < sizeof (config_path))
        {
            return config_path;
        }
    }

    // Fall back to HOME/.config
    const char *home = getenv ("HOME");
    if (!home || home[0] == '\0')
    {
        fprintf (stderr, "Error: HOME environment variable not set\n");
        return NULL;
    }

    // Build the final path
    int ret = snprintf (config_path, sizeof (config_path),
                        "%s/.config/gridflux/config.json", home);

    if (ret < 0 || (size_t)ret >= sizeof (config_path))
    {
        fprintf (stderr, "Error: Config path too long\n");
        return NULL;
    }

    return config_path;
#endif
}

static char *
read_file (const char *filename)
{
    FILE *f = fopen (filename, "r");
    if (!f)
        return NULL;

    fseek (f, 0, SEEK_END);
    long len = ftell (f);
    rewind (f);

    char *data = malloc (len + 1);
    fread (data, 1, len, f);
    data[len] = '\0';
    fclose (f);
    return data;
}

static void
write_file (const char *filename, const char *data)
{
    FILE *f = fopen (filename, "w");
    if (!f)
        return;
    fputs (data, f);
    fclose (f);
}

void
save_config (const char *filename, const gf_config_t *cfg)
{
    struct json_object *json = json_object_new_object ();

    json_object_object_add (json, "max_windows_per_workspace",
                            json_object_new_int (cfg->max_windows_per_workspace));
    json_object_object_add (json, "max_workspaces",
                            json_object_new_int (cfg->max_workspaces));
    json_object_object_add (json, "default_padding",
                            json_object_new_int (cfg->default_padding));
    json_object_object_add (json, "min_window_size",
                            json_object_new_int (cfg->min_window_size));

    struct json_object *arr = json_object_new_array ();
    for (uint32_t i = 0; i < cfg->locked_workspace_count; i++)
    {
        json_object_array_add (arr, json_object_new_int (cfg->locked_workspaces[i]));
    }
    json_object_object_add (json, "locked_workspaces", arr);

    const char *out = json_object_to_json_string_ext (json, JSON_C_TO_STRING_PRETTY);
    write_file (filename, out);

    json_object_put (json);
}

bool
gf_config_has_changed (const gf_config_t *old_cfg, const gf_config_t *new_cfg)
{
    if (!old_cfg || !new_cfg)
        return false;

    return (old_cfg->max_windows_per_workspace != new_cfg->max_windows_per_workspace
            || old_cfg->max_workspaces != new_cfg->max_workspaces
            || old_cfg->default_padding != new_cfg->default_padding
            || old_cfg->min_window_size != new_cfg->min_window_size
            || old_cfg->locked_workspace_count != new_cfg->locked_workspace_count);
}

static void
set_if_missing_int (struct json_object *json, const char *key, uint32_t *target,
                    int default_val, bool *changed)
{
    struct json_object *obj = NULL;
    if (!json_object_object_get_ex (json, key, &obj))
    {
        *target = default_val;
        *changed = true;
    }
    else
    {
        *target = json_object_get_int (obj);
    }
}

gf_config_t
load_or_create_config (const char *filename)
{
    gf_config_t cfg = DEFAULT_CONFIG;
    bool changed = false;

    char *data = read_file (filename);
    if (!data)
    {
        save_config (filename, &cfg);
        return cfg;
    }

    struct json_object *json = json_tokener_parse (data);
    free (data);

    if (!json)
    {
        save_config (filename, &cfg);
        return cfg;
    }

    set_if_missing_int (json, "max_windows_per_workspace", &cfg.max_windows_per_workspace,
                        DEFAULT_CONFIG.max_windows_per_workspace, &changed);

    set_if_missing_int (json, "max_workspaces", &cfg.max_workspaces,
                        DEFAULT_CONFIG.max_workspaces, &changed);

    set_if_missing_int (json, "default_padding", &cfg.default_padding,
                        DEFAULT_CONFIG.default_padding, &changed);

    set_if_missing_int (json, "min_window_size", &cfg.min_window_size,
                        DEFAULT_CONFIG.min_window_size, &changed);

    struct json_object *arr_obj = NULL;
    if (json_object_object_get_ex (json, "locked_workspaces", &arr_obj)
        && json_object_is_type (arr_obj, json_type_array))
    {
        size_t len = json_object_array_length (arr_obj);
        cfg.locked_workspace_count = 0;

        if (len > cfg.max_workspaces)
            changed = true; // truncated

        for (size_t i = 0; i < len && cfg.locked_workspace_count < cfg.max_workspaces;
             i++)
        {
            int ws = json_object_get_int (json_object_array_get_idx (arr_obj, i));

            if (ws < 0 || ws >= cfg.max_workspaces)
            {
                changed = true;
                continue;
            }

            cfg.locked_workspaces[cfg.locked_workspace_count++] = ws;
        }
    }
    else
    {
        cfg.locked_workspace_count = 0;
        changed = true;
    }

    json_object_put (json);

    if (changed)
    {
        GF_LOG_INFO ("Config upgraded with missing fields");
        save_config (filename, &cfg);
    }

    return cfg;
}

bool
gf_config_is_workspace_locked (const gf_config_t *cfg, gf_workspace_id_t ws)
{
    if (!cfg || ws < 0 || ws >= (gf_workspace_id_t)cfg->max_workspaces)
        return false;

    for (uint32_t i = 0; i < cfg->locked_workspace_count; i++)
    {
        if (cfg->locked_workspaces[i] == (uint32_t)ws)
            return true;
    }
    return false;
}
