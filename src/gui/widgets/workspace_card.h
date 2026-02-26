#ifndef GF_GUI_WORKSPACE_CARD_H
#define GF_GUI_WORKSPACE_CARD_H

#include "../app_state.h"

void gf_gui_workspace_card_add_to_grid (GtkGrid *grid, gf_ws_info_t *ws,
                                         const gf_win_list_t *windows, int row);

#endif // GF_GUI_WORKSPACE_CARD_H
