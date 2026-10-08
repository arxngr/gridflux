#ifndef GF_GUI_EXCLUDE_PANEL_H
#define GF_GUI_EXCLUDE_PANEL_H

#include "../app_state.h"

// Open the app-exclusion dialog: pick an open app to exclude from arrangement,
// plus the current excluded list, rebuilt from config each time it opens.
void gf_gui_on_exclude_button_clicked (GtkButton *btn, gpointer data);

#endif // GF_GUI_EXCLUDE_PANEL_H
