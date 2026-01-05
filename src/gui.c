#include "ipc.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    GtkWidget *window;
    GtkWidget *workspace_list;
    GtkWidget *ws_entry;
} AppWidgets;

static char *
gf_run_client_command (const char *command)
{
    gf_ipc_handle_t handle = gf_ipc_client_connect ();
    if (handle < 0)
        return g_strdup ("Error: Cannot connect to GridFlux");

    gf_ipc_response_t response;
    if (!gf_ipc_client_send (handle, command, &response))
    {
        gf_ipc_client_disconnect (handle);
        return g_strdup ("Error: IPC send failed");
    }

    gf_ipc_client_disconnect (handle);
    return g_strdup (response.message);
}

static GtkWidget *
gf_create_workspace_box (const char *line)
{
    GtkWidget *frame = gtk_frame_new (NULL);
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_frame_set_child (GTK_FRAME (frame), box);

    GtkWidget *label = gtk_label_new (line);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);

    gtk_box_append (GTK_BOX (box), label);
    return frame;
}

static void
gf_refresh_workspaces (AppWidgets *app)
{
    char *output = gf_run_client_command ("query workspaces");

    GtkWidget *old
        = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (app->workspace_list));
    if (old)
        gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (app->workspace_list), NULL);

    GtkWidget *container = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top (container, 12);
    gtk_widget_set_margin_bottom (container, 12);

    char *copy = g_strdup (output);
    char *line = strtok (copy, "\n");
    while (line)
    {
        gtk_box_append (GTK_BOX (container), gf_create_workspace_box (line));
        line = strtok (NULL, "\n");
    }

    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (app->workspace_list), container);

    g_free (copy);
    g_free (output);
}

static gboolean
is_number (const char *s)
{
    if (!s || !*s)
        return FALSE;
    for (; *s; s++)
        if (!g_ascii_isdigit (*s))
            return FALSE;
    return TRUE;
}

static void
on_lock_clicked (GtkButton *btn, gpointer data)
{
    AppWidgets *app = data;

    const char *ws_id = gtk_editable_get_text (GTK_EDITABLE (app->ws_entry));

    if (!is_number (ws_id))
    {
        GtkAlertDialog *d
            = gtk_alert_dialog_new ("Please enter a valid workspace number");
        gtk_alert_dialog_show (d, GTK_WINDOW (app->window));
        return;
    }

    char command[64];
    snprintf (command, sizeof (command), "lock %s", ws_id);

    char *result = gf_run_client_command (command);
    GtkAlertDialog *d = gtk_alert_dialog_new ("%s", result);
    gtk_alert_dialog_show (d, GTK_WINDOW (app->window));

    g_free (result);
    gf_refresh_workspaces (app);
}

static void
on_unlock_clicked (GtkButton *btn, gpointer data)
{
    AppWidgets *app = data;

    const char *ws_id = gtk_editable_get_text (GTK_EDITABLE (app->ws_entry));

    if (!is_number (ws_id))
    {
        GtkAlertDialog *d
            = gtk_alert_dialog_new ("Please enter a valid workspace number");
        gtk_alert_dialog_show (d, GTK_WINDOW (app->window));
        return;
    }

    char command[64];
    snprintf (command, sizeof (command), "unlock %s", ws_id);

    char *result = gf_run_client_command (command);
    GtkAlertDialog *d = gtk_alert_dialog_new ("%s", result);
    gtk_alert_dialog_show (d, GTK_WINDOW (app->window));

    g_free (result);
    gf_refresh_workspaces (app);
}

static void
on_refresh_clicked (GtkButton *btn, gpointer data)
{
    gf_refresh_workspaces ((AppWidgets *)data);
}

static void
gf_gtk_activate (GtkApplication *app, gpointer user_data)
{
    AppWidgets *widgets = g_new0 (AppWidgets, 1);

    widgets->window = gtk_application_window_new (app);
    gtk_window_set_title (GTK_WINDOW (widgets->window), "GridFlux");
    gtk_window_set_default_size (GTK_WINDOW (widgets->window), 500, 350);
    gtk_window_set_resizable (GTK_WINDOW (widgets->window), FALSE);

    GtkWidget *main = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_window_set_child (GTK_WINDOW (widgets->window), main);

    GtkWidget *controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (main), controls);

    widgets->ws_entry = gtk_entry_new ();
    gtk_entry_set_placeholder_text (GTK_ENTRY (widgets->ws_entry), "Workspace number");
    gtk_widget_set_size_request (widgets->ws_entry, 140, -1);
    gtk_box_append (GTK_BOX (controls), widgets->ws_entry);

    GtkWidget *lock = gtk_button_new_with_label ("🔒 Lock");
    GtkWidget *unlock = gtk_button_new_with_label ("🔓 Unlock");
    GtkWidget *refresh = gtk_button_new_with_label ("🔄 Refresh");

    gtk_box_append (GTK_BOX (controls), lock);
    gtk_box_append (GTK_BOX (controls), unlock);
    gtk_box_append (GTK_BOX (controls), refresh);

    g_signal_connect (lock, "clicked", G_CALLBACK (on_lock_clicked), widgets);
    g_signal_connect (unlock, "clicked", G_CALLBACK (on_unlock_clicked), widgets);
    g_signal_connect (refresh, "clicked", G_CALLBACK (on_refresh_clicked), widgets);

    widgets->workspace_list = gtk_scrolled_window_new ();
    gtk_widget_set_size_request (widgets->workspace_list, 400, 220);
    gtk_box_append (GTK_BOX (main), widgets->workspace_list);

    gf_refresh_workspaces (widgets);

    gtk_widget_grab_focus (widgets->ws_entry);
    gtk_window_present (GTK_WINDOW (widgets->window));
}

int
main (int argc, char **argv)
{
    GtkApplication *app
        = gtk_application_new ("dev.gridflux.gui", G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect (app, "activate", G_CALLBACK (gf_gtk_activate), NULL);

    int status = g_application_run (G_APPLICATION (app), argc, argv);
    g_object_unref (app);
    return status;
}
