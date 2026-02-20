#ifndef GF_GUI_PLATFORM_ASYNC_H
#define GF_GUI_PLATFORM_ASYNC_H

#include "../app_state.h"

void platform_run_command (gf_app_state_t *app, const char *command, gboolean refresh,
                           gboolean dialog);
void platform_run_refresh (gf_app_state_t *app);

#endif // GF_GUI_PLATFORM_ASYNC_H
