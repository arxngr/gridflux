#include "ipc.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    GtkWidget *window;
    GtkWidget *workspace_list;
    GtkWidget *window_list;
} AppWidgets;

static char *
run_gridfluxc_command (const char *command)
{
    gf_ipc_handle_t handle = gf_ipc_client_connect ();
    if (handle < 0)
    {
        return strdup ("Error: Cannot connect to GridFlux");
    }

    gf_ipc_response_t response;
    if (!gf_ipc_client_send (handle, command, &response))
    {
        gf_ipc_client_disconnect (handle);
        return strdup ("Error: Failed to send command");
    }

    gf_ipc_client_disconnect (handle);
    return strdup (response.message);
}

static void
refresh_workspaces (AppWidgets *app)
{
    char *output = run_gridfluxc_command ("query workspaces");

    GtkWidget *old_child
        = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (app->workspace_list));
    if (old_child)
    {
        gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (app->workspace_list), NULL);
    }

    GtkWidget *label = gtk_label_new (output);
    gtk_label_set_selectable (GTK_LABEL (label), TRUE);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (app->workspace_list), label);

    free (output);
}

static void
on_lock_clicked (GtkButton *button, gpointer data)
{
    AppWidgets *app = data;
    GtkWidget *entry = g_object_get_data (G_OBJECT (button), "entry");
    const char *ws_id = gtk_editable_get_text (GTK_EDITABLE (entry));

    char command[128];
    snprintf (command, sizeof (command), "lock %s", ws_id);

    char *result = run_gridfluxc_command (command);

    GtkAlertDialog *dialog = gtk_alert_dialog_new ("%s", result);
    gtk_alert_dialog_show (dialog, GTK_WINDOW (app->window));

    free (result);
    refresh_workspaces (app);
}

static void
on_unlock_clicked (GtkButton *button, gpointer data)
{
    AppWidgets *app = data;
    GtkWidget *entry = g_object_get_data (G_OBJECT (button), "entry");
    const char *ws_id = gtk_editable_get_text (GTK_EDITABLE (entry));

    char command[128];
    snprintf (command, sizeof (command), "unlock %s", ws_id);

    char *result = run_gridfluxc_command (command);

    GtkAlertDialog *dialog = gtk_alert_dialog_new ("%s", result);
    gtk_alert_dialog_show (dialog, GTK_WINDOW (app->window));

    free (result);
    refresh_workspaces (app);
}

static void
on_refresh_clicked (GtkButton *button, gpointer data)
{
    refresh_workspaces ((AppWidgets *)data);
}

static void
activate (GtkApplication *app, gpointer user_data)
{
    AppWidgets *widgets = g_new0 (AppWidgets, 1);

    widgets->window = gtk_application_window_new (app);
    gtk_window_set_title (GTK_WINDOW (widgets->window), "GridFlux Control Panel");
    gtk_window_set_default_size (GTK_WINDOW (widgets->window), 800, 600);

    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start (vbox, 10);
    gtk_widget_set_margin_end (vbox, 10);
    gtk_widget_set_margin_top (vbox, 10);
    gtk_widget_set_margin_bottom (vbox, 10);
    gtk_window_set_child (GTK_WINDOW (widgets->window), vbox);

    GtkWidget *control_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *ws_entry = gtk_entry_new ();
    gtk_entry_set_placeholder_text (GTK_ENTRY (ws_entry), "Workspace ID");
    gtk_widget_set_size_request (ws_entry, 100, -1);

    GtkWidget *lock_btn = gtk_button_new_with_label ("Lock");
    GtkWidget *unlock_btn = gtk_button_new_with_label ("Unlock");
    GtkWidget *refresh_btn = gtk_button_new_with_label ("Refresh");

    g_object_set_data (G_OBJECT (lock_btn), "entry", ws_entry);
    g_object_set_data (G_OBJECT (unlock_btn), "entry", ws_entry);

    g_signal_connect (lock_btn, "clicked", G_CALLBACK (on_lock_clicked), widgets);
    g_signal_connect (unlock_btn, "clicked", G_CALLBACK (on_unlock_clicked), widgets);
    g_signal_connect (refresh_btn, "clicked", G_CALLBACK (on_refresh_clicked), widgets);

    gtk_box_append (GTK_BOX (control_box), ws_entry);
    gtk_box_append (GTK_BOX (control_box), lock_btn);
    gtk_box_append (GTK_BOX (control_box), unlock_btn);
    gtk_box_append (GTK_BOX (control_box), refresh_btn);
    gtk_box_append (GTK_BOX (vbox), control_box);

    GtkWidget *label = gtk_label_new ("Workspaces:");
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (vbox), label);

    widgets->workspace_list = gtk_scrolled_window_new ();
    gtk_widget_set_vexpand (widgets->workspace_list, TRUE);
    gtk_box_append (GTK_BOX (vbox), widgets->workspace_list);

    refresh_workspaces (widgets);

    gtk_window_present (GTK_WINDOW (widgets->window));
}

int
main (int argc, char **argv)
{
    GtkApplication *app
        = gtk_application_new ("dev.gridflux.gui", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
    int status = g_application_run (G_APPLICATION (app), argc, argv);
    g_object_unref (app);
    return status;
}
