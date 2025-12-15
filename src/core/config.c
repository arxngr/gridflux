#include <json-c/json.h>
#include "config.h"
#include "types.h"
#include <stdio.h>
#include <stdlib.h>

static const gf_config_t DEFAULT_CONFIG = {
    .max_windows_per_workspace = GF_MAX_WINDOWS_PER_WORKSPACE,
    .max_workspaces = GF_MAX_WORKSPACES,
    .default_padding = GF_DEFAULT_PADDING,
    .min_window_size = GF_MIN_WINDOW_SIZE 
};

static char *read_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    
    char *data = malloc(len + 1);
    fread(data, 1, len, f);
    data[len] = '\0';
    fclose(f);
    return data;
}

static void write_file(const char *filename, const char *data) {
    FILE *f = fopen(filename, "w");
    if (!f) return;
    fputs(data, f);
    fclose(f);
}

void save_config(const char *filename, const gf_config_t *cfg) {
    struct json_object *json = json_object_new_object();
    
    json_object_object_add(json, "max_windows_per_workspace", 
                          json_object_new_int(cfg->max_windows_per_workspace));
    json_object_object_add(json, "max_workspaces", 
                          json_object_new_int(cfg->max_workspaces));
    json_object_object_add(json, "default_padding", 
                          json_object_new_int(cfg->default_padding));
    json_object_object_add(json, "min_window_size", 
                          json_object_new_int(cfg->min_window_size));
    
    const char *out = json_object_to_json_string_ext(json, JSON_C_TO_STRING_PRETTY);
    write_file(filename, out);
    
    json_object_put(json);
}

bool gf_config_has_changed(const gf_config_t *old_cfg, const gf_config_t *new_cfg) {
    if (!old_cfg || !new_cfg) return false;
    
    return (old_cfg->max_windows_per_workspace != new_cfg->max_windows_per_workspace ||
            old_cfg->max_workspaces != new_cfg->max_workspaces ||
            old_cfg->default_padding != new_cfg->default_padding ||
            old_cfg->min_window_size != new_cfg->min_window_size);
}

gf_config_t load_or_create_config(const char *filename) {
    gf_config_t cfg = DEFAULT_CONFIG;
    char *data = read_file(filename);
    
    if (!data) {
        save_config(filename, &cfg);
        return cfg;
    }
    
    struct json_object *json = json_tokener_parse(data);
    free(data);
    
    if (!json) {
        save_config(filename, &cfg);
        return cfg;
    }
    
    struct json_object *obj;
    
    if (json_object_object_get_ex(json, "max_windows_per_workspace", &obj))
        cfg.max_windows_per_workspace = json_object_get_int(obj);
    
    if (json_object_object_get_ex(json, "max_workspaces", &obj))
        cfg.max_workspaces = json_object_get_int(obj);
    
    if (json_object_object_get_ex(json, "default_padding", &obj))
        cfg.default_padding = json_object_get_int(obj);
    
    if (json_object_object_get_ex(json, "min_window_size", &obj))
        cfg.min_window_size = json_object_get_int(obj);
    
    json_object_put(json);
    return cfg;
}

