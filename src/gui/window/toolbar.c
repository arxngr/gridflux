#include "toolbar.h"
#include "../bridge/process_manager.h"
#include "../bridge/refresh.h"
#include "../panels/rules_panel.h"
#include "../panels/settings_panel.h"
#include "../platform/async.h"

#define GF_SERVER_POLL_MS 2000

static void
update_server_button (gf_app_state_t *app)
{
    gboolean running = gf_server_is_running ();
    gtk_button_set_label (GTK_BUTTON (app->server_btn),
                          running ? "● Running" : "● Stopped");
    if (running)
    {
        gtk_widget_remove_css_class (app->server_btn, "off");
        gtk_widget_add_css_class (app->server_btn, "on");
    }
    else
    {
        gtk_widget_remove_css_class (app->server_btn, "on");
        gtk_widget_add_css_class (app->server_btn, "off");
    }
}

static gboolean
refresh_once (gpointer user_data)
{
    gf_refresh_workspaces ((gf_app_state_t *)user_data);
    return G_SOURCE_REMOVE;
}

static gboolean
poll_server_status (gpointer user_data)
{
    gf_app_state_t *app = user_data;
    if (!app->server_btn || !gtk_widget_get_root (app->server_btn))
        return G_SOURCE_REMOVE;
    update_server_button (app);
    return G_SOURCE_CONTINUE;
}

static void
on_server_clicked (GtkButton *btn, gpointer user_data)
{
    (void)btn;
    gf_app_state_t *app = user_data;
    if (gf_server_is_running ())
    {
        gf_server_stop ();
        g_timeout_add (500, refresh_once, app);
    }
    else
    {
        gf_server_start ();
        g_timeout_add (1200, refresh_once, app);
    }
    update_server_button (app);
}

static void
on_refresh_clicked (GtkButton *btn, gpointer user_data)
{
    (void)btn;
    platform_run_refresh ((gf_app_state_t *)user_data);
}

// A flat header button that runs a click handler (opens a dialog).
static GtkWidget *
build_action_button (const char *icon, const char *label, GCallback cb,
                     gf_app_state_t *app)
{
    GtkWidget *btn = label ? gtk_button_new_with_label (label)
                           : gtk_button_new_from_icon_name (icon);
    gtk_widget_add_css_class (btn, "gf-hbtn");
    g_signal_connect (btn, "clicked", cb, app);
    return btn;
}

GtkWidget *
gf_gui_toolbar_new (gf_app_state_t *app)
{
    GtkWidget *bar = gtk_header_bar_new ();
    gtk_header_bar_set_show_title_buttons (GTK_HEADER_BAR (bar), TRUE);

    GtkWidget *brand = gtk_label_new ("GridFlux");
    gtk_widget_add_css_class (brand, "gf-brand");
    gtk_header_bar_set_title_widget (GTK_HEADER_BAR (bar), brand);

    app->server_btn = gtk_button_new ();
    gtk_widget_add_css_class (app->server_btn, "gf-server");
    g_signal_connect (app->server_btn, "clicked", G_CALLBACK (on_server_clicked), app);
    gtk_header_bar_pack_start (GTK_HEADER_BAR (bar), app->server_btn);
    update_server_button (app);
    g_timeout_add (GF_SERVER_POLL_MS, poll_server_status, app);

    GtkWidget *settings = build_action_button (
        "emblem-system-symbolic", NULL, G_CALLBACK (on_config_button_clicked), app);
    GtkWidget *rules
        = build_action_button (NULL, "Rules", G_CALLBACK (on_rules_button_clicked), app);
    GtkWidget *refresh = build_action_button ("view-refresh-symbolic", NULL,
                                              G_CALLBACK (on_refresh_clicked), app);

    gtk_header_bar_pack_end (GTK_HEADER_BAR (bar), settings);
    gtk_header_bar_pack_end (GTK_HEADER_BAR (bar), refresh);
    gtk_header_bar_pack_end (GTK_HEADER_BAR (bar), rules);
    return bar;
}
