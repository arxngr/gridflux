#ifndef GF_GUI_RULES_PANEL_H
#define GF_GUI_RULES_PANEL_H

#include "../app_state.h"

// Open the window-rules dialog: add form plus the current rules grouped by
// workspace, rebuilt from config each time it opens.
void on_rules_button_clicked (GtkButton *btn, gpointer data);

#endif // GF_GUI_RULES_PANEL_H
