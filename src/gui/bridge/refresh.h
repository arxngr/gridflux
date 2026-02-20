#ifndef GF_GUI_REFRESH_H
#define GF_GUI_REFRESH_H

#include "../app_state.h"

void gf_refresh_workspaces (gf_app_state_t *app);
void gf_build_workspace_grid (GtkGrid *grid, gf_ws_list_t *workspaces);

#endif // GF_GUI_REFRESH_H
