#ifndef GF_GUI_STATUSBAR_H
#define GF_GUI_STATUSBAR_H

#include "../app_state.h"

GtkWidget *gf_gui_statusbar_new (void);
void gf_gui_statusbar_set_text (GtkWidget *statusbar, const char *text);

#endif // GF_GUI_STATUSBAR_H
