#include "bridge/ipc_client.h"
#include "bridge/refresh.h"
#include "platform/gui_platform.h"
#include "window/main_window.h"
#include "window/tray.h"
#include <string.h>

static gf_app_state_t *g_widgets = NULL;
static gboolean g_start_minimized = FALSE;

static void
gf_gtk_activate (GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    if (g_widgets)
    {
        gtk_window_present (GTK_WINDOW (g_widgets->window));
        return;
    }

    g_object_set_data (G_OBJECT (app), "start-minimized",
                       GINT_TO_POINTER (g_start_minimized));

    g_widgets = g_new0 (gf_app_state_t, 1);
    g_widgets->platform = gf_gui_platform_create ();
    if (g_widgets->platform->init)
        g_widgets->platform->init (g_widgets->platform);

    gf_gui_main_window_init (g_widgets, app);
    gf_refresh_workspaces (g_widgets);

    gf_gui_tray_init (g_widgets);
    if (g_widgets->tray_data)
        g_application_hold (G_APPLICATION (app));
    else
        gtk_window_present (GTK_WINDOW (g_widgets->window));

    // if started with --minimized, hide window and keep only tray icon
    if (g_start_minimized && g_widgets->tray_data)
        gtk_widget_set_visible (g_widgets->window, FALSE);
}

static void
gf_gtk_shutdown (GtkApplication *app, gpointer user_data)
{
    (void)app;
    (void)user_data;
    if (g_widgets)
        gf_gui_tray_destroy (g_widgets);
}

int
main (int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (strcmp (argv[i], "--minimized") == 0 || strcmp (argv[i], "-m") == 0)
            g_start_minimized = TRUE;

#ifdef _WIN32
    // Windows GTK deployments may have no session bus for GApplication's
    // single-instance support. Reuse the existing tray window in that case.
    HWND tray = FindWindowW (L"GridFluxTrayClass", L"GridFluxTray");
    if (tray)
    {
        if (!g_start_minimized)
            PostMessageW (tray, GF_TRAY_SHOW_MESSAGE, 0, 0);
        return 0;
    }
#endif

    GtkApplication *app
        = gtk_application_new ("dev.gridflux.gui", G_APPLICATION_DEFAULT_FLAGS);

    g_application_add_main_option (G_APPLICATION (app), "minimized", 'm',
                                   G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
                                   "Start in the system tray", NULL);

    g_signal_connect (app, "activate", G_CALLBACK (gf_gtk_activate), NULL);
    g_signal_connect (app, "shutdown", G_CALLBACK (gf_gtk_shutdown), NULL);

    int status = g_application_run (G_APPLICATION (app), argc, argv);
    g_object_unref (app);
    return status;
}
