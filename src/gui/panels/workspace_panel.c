#include "workspace_panel.h"

GtkWidget *
gf_gui_workspace_panel_new (gf_app_state_t *app)
{
    app->workspace_table = gtk_scrolled_window_new ();
    gtk_widget_set_size_request (app->workspace_table, 600, 300);
    return app->workspace_table;
}
