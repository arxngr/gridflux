#ifndef GF_GUI_WORKSPACE_CARD_H
#define GF_GUI_WORKSPACE_CARD_H

#include "../app_state.h"

// Build one workspace card (number, window chips, tiling mini-map, lock toggle).
// Chips act as drag sources and the card as a drop target for moving windows.
GtkWidget *gf_gui_workspace_card_new (const gf_ws_info_t *ws,
                                      const gf_win_list_t *windows,
                                      gf_app_state_t *app);

#endif // GF_GUI_WORKSPACE_CARD_H
