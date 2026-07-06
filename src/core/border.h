#ifndef GF_CORE_BORDER_H
#define GF_CORE_BORDER_H

#include "../config/config.h"
#include "core/wm.h"
#include "types.h"

void gf_border_enable_all (gf_wm_t *m);
void gf_border_disable_all (gf_wm_t *m);
void gf_border_handle_toggle (gf_wm_t *m, const gf_config_t *old, const gf_config_t *new);

#endif /* GF_BORDER_H */
