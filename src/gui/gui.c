#include "bridge/ipc_client.h"
#include "bridge/refresh.h"
#include "platform/gui_platform.h"
#include "window/main_window.h"
#ifdef _WIN32
#include "window/tray.h"
#endif

static gf_app_state_t *g_widgets = NULL;

static void
gf_gtk_activate (GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    g_widgets = g_new0 (gf_app_state_t, 1);
    g_widgets->platform = gf_gui_platform_create ();
    if (g_widgets->platform->init)
        g_widgets->platform->init (g_widgets->platform);

    gf_gui_main_window_init (g_widgets, app);
    gf_refresh_workspaces (g_widgets);

#ifdef _WIN32
    gf_gui_tray_init (g_widgets);
#endif
}

#ifdef _WIN32
static void
gf_gtk_shutdown (GtkApplication *app, gpointer user_data)
{
    (void)app;
    (void)user_data;
    if (g_widgets)
        gf_gui_tray_destroy (g_widgets);
}
#endif

int
main (int argc, char **argv)
{
    GtkApplication *app
        = gtk_application_new ("dev.gridflux.gui", G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect (app, "activate", G_CALLBACK (gf_gtk_activate), NULL);
#ifdef _WIN32
    g_signal_connect (app, "shutdown", G_CALLBACK (gf_gtk_shutdown), NULL);
#endif

    int status = g_application_run (G_APPLICATION (app), argc, argv);
    g_object_unref (app);
    return status;
}
