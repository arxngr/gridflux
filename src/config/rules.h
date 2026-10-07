#ifndef GF_CONFIG_RULES_H
#define GF_CONFIG_RULES_H

#include "../core/types.h"
#include <stdbool.h>
#include <stdint.h>

#define GF_MAX_RULES 64
#define GF_RULE_CLASS_MAX 128

typedef struct
{
    char wm_class[GF_RULE_CLASS_MAX];
    gf_ws_id_t workspace_id;
    gf_monitor_id_t monitor_id;
    bool has_monitor_id;
} gf_window_rule_t;

// Forward declaration
struct gf_config;

bool gf_class_matches (const char *pattern, const char *window_class);

// --- Rule CRUD ---
gf_err_t gf_rules_add (struct gf_config *cfg, const char *wm_class, gf_ws_id_t ws_id,
                       int32_t monitor_id);
gf_err_t gf_rules_remove (struct gf_config *cfg, const char *wm_class);
const gf_window_rule_t *gf_rules_find (const struct gf_config *cfg, const char *wm_class);
uint32_t gf_rules_count (const struct gf_config *cfg);

#endif // GF_CONFIG_RULES_H
