#include "toolbar.h"
#include "../bridge/ipc_client.h"
#include "../bridge/process_manager.h"
#include "../bridge/refresh.h"
#include "../panels/settings_panel.h"
#include "../platform/async.h"
#include <stdio.h>

#define GF_TOOLBAR_POLL_MS 2000

static void
update_button_visibility (gf_app_state_t *app)
{
    gboolean running = gf_server_is_running ();
    gtk_widget_set_visible (app->start_btn, !running);
    gtk_widget_set_visible (app->stop_btn, running);
}

static gboolean
toolbar_poll_status (gpointer data)
{
    gf_app_state_t *app = (gf_app_state_t *)data;
    if (!app->window || !gtk_widget_get_root (app->start_btn))
        return G_SOURCE_REMOVE;

    update_button_visibility (app);
    return G_SOURCE_CONTINUE;
}

static void
on_start_clicked (GtkButton *btn, gpointer data)
{
    (void)btn;
    gf_app_state_t *app = (gf_app_state_t *)data;
    gf_server_start ();
    // give the server a moment to initialize before refreshing
    g_timeout_add (1500, (GSourceFunc)gf_refresh_workspaces, app);
    // update button state immediately
    g_timeout_add (2000, (GSourceFunc)toolbar_poll_status, app);
}

static void
on_stop_clicked (GtkButton *btn, gpointer data)
{
    (void)btn;
    gf_app_state_t *app = (gf_app_state_t *)data;
    gf_server_stop ();
    update_button_visibility (app);
    g_timeout_add (500, (GSourceFunc)gf_refresh_workspaces, app);
}

static void
on_lock_clicked (GtkButton *btn, gpointer data)
{
    gf_app_state_t *app = data;
    GtkStringObject *item = GTK_STRING_OBJECT (
        gtk_drop_down_get_selected_item (GTK_DROP_DOWN (app->ws_dropdown)));
    if (!item)
        return;

    char command[64];
    snprintf (command, sizeof (command), "lock %s", gtk_string_object_get_string (item));
    platform_run_command (app, command, TRUE, FALSE);
}

static void
on_unlock_clicked (GtkButton *btn, gpointer data)
{
    gf_app_state_t *app = data;
    GtkStringObject *item = GTK_STRING_OBJECT (
        gtk_drop_down_get_selected_item (GTK_DROP_DOWN (app->ws_dropdown)));
    if (!item)
        return;

    char command[64];
    snprintf (command, sizeof (command), "unlock %s",
              gtk_string_object_get_string (item));
    platform_run_command (app, command, TRUE, FALSE);
}

static void
on_refresh_clicked (GtkButton *btn, gpointer data)
{
    platform_run_refresh ((gf_app_state_t *)data);
}

GtkWidget *
gf_gui_toolbar_new (gf_app_state_t *app)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);

    // server start/stop controls — only one visible at a time
    app->start_btn = gtk_button_new_with_label ("\u25B6 Start");
    app->stop_btn = gtk_button_new_with_label ("\u23F9 Stop");

    gtk_box_append (GTK_BOX (box), app->start_btn);
    gtk_box_append (GTK_BOX (box), app->stop_btn);

    g_signal_connect (app->start_btn, "clicked", G_CALLBACK (on_start_clicked), app);
    g_signal_connect (app->stop_btn, "clicked", G_CALLBACK (on_stop_clicked), app);

    // set initial visibility based on current server state
    update_button_visibility (app);

    // poll server status to keep buttons in sync
    g_timeout_add (GF_TOOLBAR_POLL_MS, toolbar_poll_status, app);

    // separator
    GtkWidget *sep = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
    gtk_box_append (GTK_BOX (box), sep);

    GtkWidget *ws_label = gtk_label_new ("Workspaces:");
    gtk_widget_set_valign (ws_label, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (box), ws_label);

    app->ws_model = gtk_string_list_new (NULL);
    app->ws_dropdown = gtk_drop_down_new (G_LIST_MODEL (app->ws_model), NULL);
    gtk_widget_set_size_request (app->ws_dropdown, 140, -1);
    gtk_box_append (GTK_BOX (box), app->ws_dropdown);

    GtkWidget *lock = gtk_button_new_with_label ("\U0001F512 Lock");
    GtkWidget *unlock = gtk_button_new_with_label ("\U0001F513 Unlock");
    GtkWidget *refresh = gtk_button_new_with_label ("\U0001F504 Refresh");
    GtkWidget *config = gtk_button_new_with_label ("\u2699\uFE0F Config");

    gtk_box_append (GTK_BOX (box), lock);
    gtk_box_append (GTK_BOX (box), unlock);
    gtk_box_append (GTK_BOX (box), refresh);
    gtk_box_append (GTK_BOX (box), config);

    g_signal_connect (lock, "clicked", G_CALLBACK (on_lock_clicked), app);
    g_signal_connect (unlock, "clicked", G_CALLBACK (on_unlock_clicked), app);
    g_signal_connect (refresh, "clicked", G_CALLBACK (on_refresh_clicked), app);
    g_signal_connect (config, "clicked", G_CALLBACK (on_config_button_clicked), app);

    return box;
}

