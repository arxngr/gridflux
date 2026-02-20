#include "toolbar.h"
#include "../bridge/ipc_client.h"
#include "../bridge/refresh.h"
#include "../panels/settings_panel.h"
#include "../platform/async.h"
#include <stdio.h>

static void
on_lock_clicked (GtkButton *btn, gpointer data)
{
    gf_app_state_t *app = data;
    GtkStringObject *item = GTK_STRING_OBJECT (gtk_drop_down_get_selected_item (GTK_DROP_DOWN (app->ws_dropdown)));
    if (!item) return;

    char command[64];
    snprintf (command, sizeof (command), "lock %s", gtk_string_object_get_string (item));
    platform_run_command (app, command, TRUE, FALSE);
}

static void
on_unlock_clicked (GtkButton *btn, gpointer data)
{
    gf_app_state_t *app = data;
    GtkStringObject *item = GTK_STRING_OBJECT (gtk_drop_down_get_selected_item (GTK_DROP_DOWN (app->ws_dropdown)));
    if (!item) return;

    char command[64];
    snprintf (command, sizeof (command), "unlock %s", gtk_string_object_get_string (item));
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

    app->ws_model = gtk_string_list_new (NULL);
    app->ws_dropdown = gtk_drop_down_new (G_LIST_MODEL (app->ws_model), NULL);
    gtk_widget_set_size_request (app->ws_dropdown, 140, -1);
    gtk_box_append (GTK_BOX (box), app->ws_dropdown);

    GtkWidget *lock = gtk_button_new_with_label ("🔒 Lock");
    GtkWidget *unlock = gtk_button_new_with_label ("🔓 Unlock");
    GtkWidget *refresh = gtk_button_new_with_label ("🔄 Refresh");
    GtkWidget *config = gtk_button_new_with_label ("⚙️ Config");

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
