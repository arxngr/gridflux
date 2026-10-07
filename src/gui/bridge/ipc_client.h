#ifndef GF_GUI_IPC_CLIENT_H
#define GF_GUI_IPC_CLIENT_H

#include "../app_state.h"

gf_ipc_response_t gf_run_client_command (const char *command);
bool gf_gui_get_monitors (gf_monitor_t *monitors, uint32_t *count);

#endif // GF_GUI_IPC_CLIENT_H
