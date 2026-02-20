#include "window/main_window.h"
#include "bridge/refresh.h"
#include "bridge/ipc_client.h"

static void
gf_gtk_activate (GtkApplication *app, gpointer user_data)
{
    gf_app_state_t *widgets = g_new0 (gf_app_state_t, 1);
    gf_gui_main_window_init (widgets, app);
    gf_refresh_workspaces (widgets);
}

int
main (int argc, char **argv)
{
    GtkApplication *app = gtk_application_new ("dev.gridflux.gui", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect (app, "activate", G_CALLBACK (gf_gtk_activate), NULL);
    int status = g_application_run (G_APPLICATION (app), argc, argv);
    g_object_unref (app);
    return status;
}
