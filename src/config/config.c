#include "config.h"
#include "../core/types.h"
#include "../platform/platform_compat.h"
#include "../utils/file.h"
#include "../utils/logger.h"
#include "../utils/memory.h"
#include <json-c/json.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const gf_config_t DEFAULT_CONFIG
    = { .max_windows_per_workspace = GF_MAX_WINDOWS_PER_WORKSPACE,
        .max_workspaces = GF_MAX_WORKSPACES,
        .default_padding = GF_DEFAULT_PADDING,
        .min_window_size = GF_MIN_WINDOW_SIZE,
        .border_color = 0x00F49D2A,
        .enable_borders = true,
        .locked_workspaces_count = 0,
        .window_rules_count = 0 };


const char *
gf_config_get_path (void)
{
    static char config_path[PATH_MAX];

#ifdef GF_DEV_MODE
    strncpy (config_path, "config.json", sizeof (config_path) - 1);
    config_path[sizeof (config_path) - 1] = '\0';
    return config_path;
#else
#ifdef _WIN32
    const char *appdata = getenv ("APPDATA");
    if (!appdata || appdata[0] == '\0')
    {
        fprintf (stderr, "Error: APPDATA environment variable not set or empty\n");
        return NULL;
    }

    snprintf (config_path, sizeof (config_path), "%s\\gridflux\\config.json", appdata);

    // Ensure the directory exists
    char gridflux_dir[PATH_MAX];
    snprintf (gridflux_dir, sizeof (gridflux_dir), "%s\\gridflux", appdata);

    return config_path;
#else
    // Unix-like systems
    const char *xdg_config = getenv ("XDG_CONFIG_HOME");
    if (xdg_config && xdg_config[0] != '\0')
    {
        snprintf (config_path, sizeof (config_path), "%s/gridflux/config.json",
                  xdg_config);

        // Ensure the directory exists
        char gridflux_dir[PATH_MAX];
        snprintf (gridflux_dir, sizeof (gridflux_dir), "%s/gridflux", xdg_config);
        mkdir (gridflux_dir, 0755);

        return config_path;
    }

    const char *home = getenv ("HOME");
    if (!home || home[0] == '\0')
    {
        fprintf (stderr, "Error: HOME environment variable not set\n");
        return NULL;
    }

    snprintf (config_path, sizeof (config_path), "%s/.config/gridflux/config.json", home);

    // Ensure the directory exists
    char config_dir[PATH_MAX];
    snprintf (config_dir, sizeof (config_dir), "%s/.config", home);
    mkdir (config_dir, 0755);

    char gridflux_dir[PATH_MAX];
    snprintf (gridflux_dir, sizeof (gridflux_dir), "%s/.config/gridflux", home);
    mkdir (gridflux_dir, 0755);

    return config_path;
#endif
#endif
}

static char *
read_file (const char *filename)
{
    FILE *f = fopen (filename, "r");
    if (!f)
    {
        fprintf (stderr,
                 "Error: Unable to open file '%s' for reading. Check permissions.\n",
                 filename);
        return NULL;
    }

    fseek (f, 0, SEEK_END);
    long len = ftell (f);
    rewind (f);

    char *data = malloc (len + 1);
    if (!data)
    {
        fprintf (stderr, "Error: Memory allocation failed while reading file '%s'.\n",
                 filename);
        fclose (f);
        return NULL;
    }

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
    {
        fprintf (stderr,
                 "Error: Unable to open file '%s' for writing. Check permissions.\n",
                 filename);
        return;
    }
    fputs (data, f);
    fclose (f);
}

void
gf_config_save (const char *filename, const gf_config_t *cfg)
{
    struct json_object *json = json_object_new_object ();

    json_object_object_add (json, "max_windows_per_workspace",
                            json_object_new_int (cfg->max_windows_per_workspace));
    json_object_object_add (json, "max_workspaces",
                            json_object_new_int (cfg->max_workspaces));
    json_object_object_add (json, "min_window_size",
                            json_object_new_int (cfg->min_window_size));
    json_object_object_add (json, "border_color",
                            json_object_new_int64 (cfg->border_color));
    json_object_object_add (json, "enable_borders",
                            json_object_new_boolean (cfg->enable_borders));

    struct json_object *arr = json_object_new_array ();
    for (uint32_t i = 0; i < cfg->locked_workspaces_count; i++)
    {
        json_object_array_add (arr, json_object_new_int (cfg->locked_workspaces[i]));
    }
    json_object_object_add (json, "locked_workspaces", arr);

    // Serialize window rules
    struct json_object *rules_arr = json_object_new_array ();
    for (uint32_t i = 0; i < cfg->window_rules_count; i++)
    {
        struct json_object *rule_obj = json_object_new_object ();
        json_object_object_add (rule_obj, "wm_class",
                                json_object_new_string (cfg->window_rules[i].wm_class));
        json_object_object_add (rule_obj, "workspace_id",
                                json_object_new_int (cfg->window_rules[i].workspace_id));
        json_object_array_add (rules_arr, rule_obj);
    }
    json_object_object_add (json, "window_rules", rules_arr);

    const char *out = json_object_to_json_string_ext (json, JSON_C_TO_STRING_PRETTY);
    write_file (filename, out);

    json_object_put (json);
}

bool
gf_config_changed (const gf_config_t *old_cfg, const gf_config_t *new_cfg)
{
    if (!old_cfg || !new_cfg)
        return false;

    return (old_cfg->max_windows_per_workspace != new_cfg->max_windows_per_workspace
            || old_cfg->max_workspaces != new_cfg->max_workspaces
            || old_cfg->min_window_size != new_cfg->min_window_size
            || old_cfg->border_color != new_cfg->border_color
            || old_cfg->enable_borders != new_cfg->enable_borders
            || old_cfg->locked_workspaces_count != new_cfg->locked_workspaces_count
            || old_cfg->window_rules_count != new_cfg->window_rules_count);
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

        gf_config_save (filename, &cfg);
        return cfg;
    }

    struct json_object *json = json_tokener_parse (data);
    free (data);

    if (!json)
    {
        gf_config_save (filename, &cfg);
        return cfg;
    }

    set_if_missing_int (json, "max_windows_per_workspace", &cfg.max_windows_per_workspace,
                        DEFAULT_CONFIG.max_windows_per_workspace, &changed);

    set_if_missing_int (json, "max_workspaces", &cfg.max_workspaces,
                        DEFAULT_CONFIG.max_workspaces, &changed);

    set_if_missing_int (json, "min_window_size", &cfg.min_window_size,
                        DEFAULT_CONFIG.min_window_size, &changed);

    set_if_missing_int (json, "border_color", &cfg.border_color,
                        DEFAULT_CONFIG.border_color, &changed);

    struct json_object *border_obj = NULL;
    if (!json_object_object_get_ex (json, "enable_borders", &border_obj))
    {
        cfg.enable_borders = DEFAULT_CONFIG.enable_borders;
        changed = true;
    }
    else
    {
        cfg.enable_borders = json_object_get_boolean (border_obj);
    }

    struct json_object *arr_obj = NULL;
    if (json_object_object_get_ex (json, "locked_workspaces", &arr_obj)
        && json_object_is_type (arr_obj, json_type_array))
    {
        size_t len = json_object_array_length (arr_obj);
        cfg.locked_workspaces_count = 0;

        if (len > cfg.max_workspaces)
            changed = true; // truncated

        for (size_t i = 0; i < len && cfg.locked_workspaces_count < cfg.max_workspaces;
             i++)
        {
            int ws = json_object_get_int (json_object_array_get_idx (arr_obj, i));

            if (ws < 0 || ws >= cfg.max_workspaces)
            {
                changed = true;
                continue;
            }

            cfg.locked_workspaces[cfg.locked_workspaces_count++] = ws;
        }
    }
    else
    {
        cfg.locked_workspaces_count = 0;
        changed = true;
    }

    // Parse window rules
    struct json_object *rules_obj = NULL;
    if (json_object_object_get_ex (json, "window_rules", &rules_obj)
        && json_object_is_type (rules_obj, json_type_array))
    {
        size_t rules_len = json_object_array_length (rules_obj);
        cfg.window_rules_count = 0;

        for (size_t i = 0; i < rules_len && cfg.window_rules_count < GF_MAX_RULES; i++)
        {
            struct json_object *rule_item = json_object_array_get_idx (rules_obj, i);
            struct json_object *class_obj = NULL;
            struct json_object *ws_obj = NULL;

            if (json_object_object_get_ex (rule_item, "wm_class", &class_obj)
                && json_object_object_get_ex (rule_item, "workspace_id", &ws_obj))
            {
                const char *cls = json_object_get_string (class_obj);
                int ws = json_object_get_int (ws_obj);

                if (cls && cls[0] != '\0' && ws >= GF_FIRST_WORKSPACE_ID)
                {
                    strncpy (cfg.window_rules[cfg.window_rules_count].wm_class, cls,
                             GF_RULE_CLASS_MAX - 1);
                    cfg.window_rules[cfg.window_rules_count]
                        .wm_class[GF_RULE_CLASS_MAX - 1]
                        = '\0';
                    cfg.window_rules[cfg.window_rules_count].workspace_id = ws;
                    cfg.window_rules_count++;
                }
            }
        }
    }

    json_object_put (json);

    if (changed)
    {
        GF_LOG_INFO ("Config upgraded with missing fields");
        gf_config_save (filename, &cfg);
    }

    return cfg;
}

bool
gf_config_workspace_is_locked (const gf_config_t *cfg, gf_ws_id_t ws)
{
    if (!cfg || ws < GF_FIRST_WORKSPACE_ID
        || ws >= (gf_ws_id_t)cfg->max_workspaces + GF_FIRST_WORKSPACE_ID)
        return false;

    for (uint32_t i = 0; i < cfg->locked_workspaces_count; i++)
    {
        if (cfg->locked_workspaces[i] == (uint32_t)ws)
        {
            return true;
        }
    }
    return false;
}

gf_err_t
gf_config_workspace_lock (gf_config_t *config, gf_ws_id_t ws_id)
{
    if (!config || ws_id < 0)
        return GF_ERROR_INVALID_PARAMETER;

    for (uint32_t i = 0; i < config->locked_workspaces_count; i++)
    {
        if (config->locked_workspaces[i] == ws_id)
        {
            return GF_SUCCESS;
        }
    }

    if (config->locked_workspaces_count >= config->max_workspaces)
    {
        return GF_ERROR_INVALID_PARAMETER;
    }

    config->locked_workspaces[config->locked_workspaces_count++] = ws_id;

    const char *config_path = gf_config_get_path ();
    if (config_path)
    {
        gf_config_save (config_path, config);
    }

    return GF_SUCCESS;
}

gf_err_t
gf_config_workspace_unlock (gf_config_t *config, gf_ws_id_t ws_id)
{
    if (!config || ws_id < 0)
        return GF_ERROR_INVALID_PARAMETER;

    bool found = false;
    for (uint32_t i = 0; i < config->locked_workspaces_count; i++)
    {
        if (config->locked_workspaces[i] == ws_id)
        {
            found = true;
            for (uint32_t j = i; j < config->locked_workspaces_count - 1; j++)
            {
                config->locked_workspaces[j] = config->locked_workspaces[j + 1];
            }
            config->locked_workspaces_count--;
            break;
        }
    }

    if (!found)
    {
        return GF_SUCCESS;
    }

    const char *config_path = gf_config_get_path ();
    if (config_path)
    {
        gf_config_save (config_path, config);
    }

    return GF_SUCCESS;
}
