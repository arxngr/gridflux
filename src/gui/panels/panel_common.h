#ifndef GF_GUI_PANEL_COMMON_H
#define GF_GUI_PANEL_COMMON_H

#include "../app_state.h"
#include <gtk/gtk.h>

void gf_panel_clear_box (GtkWidget *box);
void gf_panel_extract_wm_class (char *buf);
GdkPaintable *gf_panel_app_icon (gf_app_state_t *app, const char *wm_class);
const char *gf_panel_friendly_name (gf_app_state_t *app, const char *wm_class);

// A searchable dropdown of the currently open apps. Returns the widget and,
// via out_model, the backing string list.
GtkWidget *gf_panel_build_app_dropdown (gf_app_state_t *app, GtkStringList **out_model);

#endif // GF_GUI_PANEL_COMMON_H
