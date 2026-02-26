#include "config.h"
#include "rules.h"
#include "../utils/logger.h"
#include <ctype.h>
#include <string.h>

static bool
class_matches (const char *rule_class, const char *window_class)
{
    if (!rule_class || !window_class)
        return false;

    // Case-insensitive comparison
    size_t rule_len = strlen (rule_class);
    size_t win_len = strlen (window_class);

    if (rule_len == 0 || win_len == 0)
        return false;

    if (rule_len != win_len)
        return false;

    for (size_t i = 0; i < rule_len; i++)
    {
        if (tolower ((unsigned char)rule_class[i]) != tolower ((unsigned char)window_class[i]))
            return false;
    }

    return true;
}

gf_err_t
gf_rules_add (gf_config_t *cfg, const char *wm_class, gf_ws_id_t ws_id)
{
    if (!cfg || !wm_class || wm_class[0] == '\0')
        return GF_ERROR_INVALID_PARAMETER;

    if (ws_id < GF_FIRST_WORKSPACE_ID)
        return GF_ERROR_INVALID_PARAMETER;

    // Check if rule already exists for this class — update it
    for (uint32_t i = 0; i < cfg->window_rules_count; i++)
    {
        if (class_matches (cfg->window_rules[i].wm_class, wm_class))
        {
            cfg->window_rules[i].workspace_id = ws_id;
            GF_LOG_INFO ("Updated rule: %s → workspace %d", wm_class, ws_id);

            const char *path = gf_config_get_path ();
            if (path)
                gf_config_save (path, cfg);

            return GF_SUCCESS;
        }
    }

    if (cfg->window_rules_count >= GF_MAX_RULES)
        return GF_ERROR_INVALID_PARAMETER;

    gf_window_rule_t *rule = &cfg->window_rules[cfg->window_rules_count];
    strncpy (rule->wm_class, wm_class, GF_RULE_CLASS_MAX - 1);
    rule->wm_class[GF_RULE_CLASS_MAX - 1] = '\0';
    rule->workspace_id = ws_id;
    cfg->window_rules_count++;

    GF_LOG_INFO ("Added rule: %s → workspace %d", wm_class, ws_id);

    const char *path = gf_config_get_path ();
    if (path)
        gf_config_save (path, cfg);

    return GF_SUCCESS;
}

gf_err_t
gf_rules_remove (gf_config_t *cfg, const char *wm_class)
{
    if (!cfg || !wm_class)
        return GF_ERROR_INVALID_PARAMETER;

    for (uint32_t i = 0; i < cfg->window_rules_count; i++)
    {
        if (class_matches (cfg->window_rules[i].wm_class, wm_class))
        {
            // Shift remaining rules down
            for (uint32_t j = i; j < cfg->window_rules_count - 1; j++)
            {
                cfg->window_rules[j] = cfg->window_rules[j + 1];
            }
            cfg->window_rules_count--;

            GF_LOG_INFO ("Removed rule for: %s", wm_class);

            const char *path = gf_config_get_path ();
            if (path)
                gf_config_save (path, cfg);

            return GF_SUCCESS;
        }
    }

    return GF_ERROR_WINDOW_NOT_FOUND;
}

const gf_window_rule_t *
gf_rules_find (const gf_config_t *cfg, const char *wm_class)
{
    if (!cfg || !wm_class)
        return NULL;

    for (uint32_t i = 0; i < cfg->window_rules_count; i++)
    {
        if (class_matches (cfg->window_rules[i].wm_class, wm_class))
            return &cfg->window_rules[i];
    }

    return NULL;
}

uint32_t
gf_rules_count (const gf_config_t *cfg)
{
    return cfg ? cfg->window_rules_count : 0;
}
