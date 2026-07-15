#include "workspace_panel.h"

GtkWidget *
gf_gui_workspace_panel_new (gf_app_state_t *app)
{
    app->workspace_table = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (app->workspace_table),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request (app->workspace_table, 360, 200);
    gtk_widget_set_hexpand (app->workspace_table, TRUE);
    gtk_widget_set_vexpand (app->workspace_table, TRUE);
    return app->workspace_table;
}
