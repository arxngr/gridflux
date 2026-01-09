#include "ipc.h"
#include "ipc_command.h"
#include "list.h"
#include "types.h"
#ifdef __linux__
#include <gdk/x11/gdkx.h>
#endif
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    GtkWidget *window;
    GtkWidget *workspace_table;
    GtkWidget *ws_dropdown;
    GtkStringList *ws_model;
    GtkWidget *window_dropdown;
    GtkStringList *window_model;
    GtkWidget *target_ws_dropdown;
    GtkStringList *target_ws_model;
} AppWidgets;

static gboolean
_is_window_itself (const gf_window_info_t *win)
{
    if (!win || !win->name[0])
        return FALSE;

    return g_strcmp0 (win->name, "GridFlux") == 0;
}

static gf_ipc_response_t
gf_run_client_command (const char *command)
{
    gf_ipc_handle_t handle = gf_ipc_client_connect ();
    if (handle < 0)
    {
        gf_ipc_response_t err = { .status = GF_IPC_ERROR_CONNECTION };
        gf_command_response_t resp = { .type = 1 };
        snprintf (resp.message, sizeof (resp.message), "Cannot connect to GridFlux");
        memcpy (err.message, &resp, sizeof (resp));
        return err;
    }

    gf_ipc_response_t response;
    if (!gf_ipc_client_send (handle, command, &response))
    {
        gf_ipc_client_disconnect (handle);
        gf_ipc_response_t err = { .status = GF_IPC_ERROR_INVALID_COMMAND };
        gf_command_response_t resp = { .type = 1 };
        snprintf (resp.message, sizeof (resp.message), "IPC send failed");
        memcpy (err.message, &resp, sizeof (resp));
        return err;
    }

    gf_ipc_client_disconnect (handle);

    return response;
}

static void
on_window_dropdown_changed (GtkDropDown *dropdown, GParamSpec *pspec, gpointer data)
{
    AppWidgets *app = (AppWidgets *)data;

    guint selected = gtk_drop_down_get_selected (dropdown);
    if (selected == GTK_INVALID_LIST_POSITION)
    {
        return;
    }

    GListModel *model = gtk_drop_down_get_model (dropdown);
    GArray *window_data = g_object_get_data (G_OBJECT (model), "window-data");

    if (!window_data || selected >= window_data->len)
    {
        return;
    }

    gf_window_info_t *win = &g_array_index (window_data, gf_window_info_t, selected);
    int current_workspace = win->workspace_id;

    gf_ipc_response_t ws_response = gf_run_client_command ("query workspaces");
    gf_workspace_list_t *workspaces = gf_parse_workspace_list (ws_response.message);

    if (!workspaces)
    {
        return;
    }

    GtkStringList *new_target_ws_model = gtk_string_list_new (NULL);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_workspace_info_t *ws = &workspaces->items[i];

        if (ws->id == current_workspace)
            continue;

        char ws_id_str[16];
        snprintf (ws_id_str, sizeof (ws_id_str), "%d", ws->id);
        gtk_string_list_append (new_target_ws_model, ws_id_str);
    }

    gtk_drop_down_set_model (GTK_DROP_DOWN (app->target_ws_dropdown),
                             G_LIST_MODEL (new_target_ws_model));
    app->target_ws_model = new_target_ws_model;

    gf_workspace_list_cleanup (workspaces);
}

static void
gf_refresh_workspaces (AppWidgets *app)
{
    gf_ipc_response_t ws_response = gf_run_client_command ("query workspaces");
    gf_ipc_response_t win_response = gf_run_client_command ("query windows");

    gf_workspace_list_t *workspaces = gf_parse_workspace_list (ws_response.message);
    gf_window_list_t *windows = gf_parse_window_list (win_response.message);

    if (!workspaces || !windows)
        goto cleanup;

    GtkStringList *new_ws_model = gtk_string_list_new (NULL);
    gtk_drop_down_set_model (GTK_DROP_DOWN (app->ws_dropdown),
                             G_LIST_MODEL (new_ws_model));
    app->ws_model = new_ws_model;

    GtkStringList *new_window_model = gtk_string_list_new (NULL);

    GArray *window_data = g_array_new (FALSE, FALSE, sizeof (gf_window_info_t));

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_window_info_t *win = &windows->items[i];

        if (_is_window_itself (win) || !win->is_valid)
            continue;

        gtk_string_list_append (new_window_model, win->name);

        g_array_append_val (window_data, *win);
    }

    g_object_set_data_full (G_OBJECT (new_window_model), "window-data", window_data,
                            (GDestroyNotify)g_array_unref);

    gtk_drop_down_set_model (GTK_DROP_DOWN (app->window_dropdown),
                             G_LIST_MODEL (new_window_model));
    app->window_model = new_window_model;

    GtkStringList *new_target_ws_model = gtk_string_list_new (NULL);
    gtk_drop_down_set_model (GTK_DROP_DOWN (app->target_ws_dropdown),
                             G_LIST_MODEL (new_target_ws_model));
    app->target_ws_model = new_target_ws_model;

    GtkWidget *old
        = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (app->workspace_table));
    if (old)
        gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (app->workspace_table), NULL);

    GtkWidget *grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 5);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
    gtk_widget_set_margin_top (grid, 12);
    gtk_widget_set_margin_bottom (grid, 12);
    gtk_widget_set_margin_start (grid, 12);
    gtk_widget_set_margin_end (grid, 12);

    GtkWidget *ws_header = gtk_label_new ("Workspace");
    gtk_widget_add_css_class (ws_header, "table-header");
    gtk_grid_attach (GTK_GRID (grid), ws_header, 0, 0, 1, 1);

    GtkWidget *count_header = gtk_label_new ("Window Count");
    gtk_widget_add_css_class (count_header, "table-header");
    gtk_grid_attach (GTK_GRID (grid), count_header, 1, 0, 1, 1);

    GtkWidget *slots_header = gtk_label_new ("Available Slots");
    gtk_widget_add_css_class (slots_header, "table-header");
    gtk_grid_attach (GTK_GRID (grid), slots_header, 2, 0, 1, 1);

    GtkWidget *status_header = gtk_label_new ("Status");
    gtk_widget_add_css_class (status_header, "table-header");
    gtk_grid_attach (GTK_GRID (grid), status_header, 3, 0, 1, 1);

    GtkWidget *sep = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_grid_attach (GTK_GRID (grid), sep, 0, 1, 4, 1);

    int row = 2;
    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_workspace_info_t *ws = &workspaces->items[i];

        char ws_id_str[16];
        snprintf (ws_id_str, sizeof (ws_id_str), "%d", ws->id);
        GtkWidget *ws_label = gtk_label_new (ws_id_str);
        gtk_widget_add_css_class (ws_label, "table-cell");
        gtk_grid_attach (GTK_GRID (grid), ws_label, 0, row, 1, 1);

        char count_str[16];
        snprintf (count_str, sizeof (count_str), "%u", ws->window_count);
        GtkWidget *count_label = gtk_label_new (count_str);
        gtk_widget_add_css_class (count_label, "table-cell");
        gtk_grid_attach (GTK_GRID (grid), count_label, 1, row, 1, 1);

        char slots_str[16];
        snprintf (slots_str, sizeof (slots_str), "%d", ws->available_space);
        GtkWidget *slots_label = gtk_label_new (slots_str);
        gtk_widget_add_css_class (slots_label, "table-cell");
        gtk_grid_attach (GTK_GRID (grid), slots_label, 2, row, 1, 1);

        const char *status_text = ws->is_locked ? "🔒 Locked" : "🔓 Unlocked";
        GtkWidget *status_label = gtk_label_new (status_text);
        gtk_widget_add_css_class (status_label, "table-cell");
        gtk_grid_attach (GTK_GRID (grid), status_label, 3, row, 1, 1);

        row++;

        gtk_string_list_append (new_ws_model, ws_id_str);
        gtk_string_list_append (new_target_ws_model, ws_id_str);
    }

    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (app->workspace_table), grid);

cleanup:
    if (workspaces)
        gf_workspace_list_cleanup (workspaces);

    if (windows)
        gf_window_list_cleanup (windows);
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

    guint selected = gtk_drop_down_get_selected (GTK_DROP_DOWN (app->ws_dropdown));
    if (selected == GTK_INVALID_LIST_POSITION)
    {
        GtkAlertDialog *d = gtk_alert_dialog_new (NULL);
        gtk_alert_dialog_set_message (d, "Please select a workspace");
        gtk_alert_dialog_show (d, GTK_WINDOW (app->window));
        return;
    }

    GtkStringObject *item = GTK_STRING_OBJECT (
        gtk_drop_down_get_selected_item (GTK_DROP_DOWN (app->ws_dropdown)));
    if (!item)
    {
        GtkAlertDialog *d = gtk_alert_dialog_new (NULL);
        gtk_alert_dialog_set_message (d, "Invalid selection");
        gtk_alert_dialog_show (d, GTK_WINDOW (app->window));
        return;
    }

    const char *ws_id = gtk_string_object_get_string (item);
    if (!ws_id || !is_number (ws_id))
    {
        GtkAlertDialog *d = gtk_alert_dialog_new (NULL);
        gtk_alert_dialog_set_message (d, "Please select a valid workspace");
        gtk_alert_dialog_show (d, GTK_WINDOW (app->window));
        return;
    }

    char command[64];
    snprintf (command, sizeof (command), "lock %s", ws_id);

    gf_ipc_response_t response = gf_run_client_command (command);

    gf_refresh_workspaces (app);
}

static void
on_unlock_clicked (GtkButton *btn, gpointer data)
{
    AppWidgets *app = data;

    guint selected = gtk_drop_down_get_selected (GTK_DROP_DOWN (app->ws_dropdown));
    if (selected == GTK_INVALID_LIST_POSITION)
    {
        GtkAlertDialog *d = gtk_alert_dialog_new (NULL);
        gtk_alert_dialog_set_message (d, "Please select a workspace");
        gtk_alert_dialog_show (d, GTK_WINDOW (app->window));
        return;
    }

    GtkStringObject *item = GTK_STRING_OBJECT (
        gtk_drop_down_get_selected_item (GTK_DROP_DOWN (app->ws_dropdown)));
    if (!item)
    {
        GtkAlertDialog *d = gtk_alert_dialog_new (NULL);
        gtk_alert_dialog_set_message (d, "Invalid selection");
        gtk_alert_dialog_show (d, GTK_WINDOW (app->window));
        return;
    }

    const char *ws_id = gtk_string_object_get_string (item);
    if (!ws_id || !is_number (ws_id))
    {
        GtkAlertDialog *d = gtk_alert_dialog_new (NULL);
        gtk_alert_dialog_set_message (d, "Please select a valid workspace");
        gtk_alert_dialog_show (d, GTK_WINDOW (app->window));
        return;
    }

    char command[64];
    snprintf (command, sizeof (command), "unlock %s", ws_id);

    gf_ipc_response_t response = gf_run_client_command (command);

    gf_refresh_workspaces (app);
}

static void
on_refresh_clicked (GtkButton *btn, gpointer data)
{
    gf_refresh_workspaces ((AppWidgets *)data);
}

static void
on_move_clicked (GtkButton *btn, gpointer data)
{
    AppWidgets *app = data;

    guint wsel = gtk_drop_down_get_selected (GTK_DROP_DOWN (app->window_dropdown));
    guint tsel = gtk_drop_down_get_selected (GTK_DROP_DOWN (app->target_ws_dropdown));

    if (wsel == GTK_INVALID_LIST_POSITION || tsel == GTK_INVALID_LIST_POSITION)
        return;

    GListModel *model = gtk_drop_down_get_model (GTK_DROP_DOWN (app->window_dropdown));
    GArray *window_data = g_object_get_data (G_OBJECT (model), "window-data");

    if (!window_data || wsel >= window_data->len)
        return;

    gf_window_info_t *win = &g_array_index (window_data, gf_window_info_t, wsel);

    GtkStringObject *target_item = GTK_STRING_OBJECT (
        gtk_drop_down_get_selected_item (GTK_DROP_DOWN (app->target_ws_dropdown)));

    if (!target_item)
        return;

    const char *target_ws = gtk_string_object_get_string (target_item);
    if (!target_ws)
        return;

    unsigned long window_id = win->id;

    char cmd[64];
    snprintf (cmd, sizeof cmd, "move %lu %s", window_id, target_ws);

    gf_run_client_command (cmd);
    gf_refresh_workspaces (app);
}

static void
on_window_realize (GtkWidget *widget, gpointer user_data)
{
#ifdef __linux__
    GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (widget));

    if (!surface || !GDK_IS_X11_SURFACE (surface))
        return;

    gdk_x11_surface_set_skip_taskbar_hint (GDK_X11_SURFACE (surface), TRUE);
    gdk_x11_surface_set_skip_pager_hint (GDK_X11_SURFACE (surface), TRUE);
#endif
}

static void
gf_gtk_activate (GtkApplication *app, gpointer user_data)
{
    AppWidgets *widgets = g_new0 (AppWidgets, 1);

    // Add CSS for table-like appearance
    GtkCssProvider *provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_string (
        provider, ".table-cell { border: 1px solid #555; padding: 4px; background-color: "
                  "#2d2d2d; color: white; }"
                  ".table-header { border: 1px solid #777; padding: 4px; "
                  "background-color: #1a1a1a; color: white; font-weight: bold; }");
    gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                                GTK_STYLE_PROVIDER (provider),
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref (provider);

    widgets->window = gtk_application_window_new (app);
    gtk_window_set_title (GTK_WINDOW (widgets->window), "GridFlux");
    gtk_window_set_default_size (GTK_WINDOW (widgets->window), 700, 500);
    gtk_window_set_resizable (GTK_WINDOW (widgets->window), TRUE);

    g_signal_connect (widgets->window, "realize", G_CALLBACK (on_window_realize), NULL);
    GtkWidget *main = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_window_set_child (GTK_WINDOW (widgets->window), main);

    // Lock/Unlock controls
    GtkWidget *controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (main), controls);

    widgets->ws_model = gtk_string_list_new (NULL);
    widgets->ws_dropdown = gtk_drop_down_new (G_LIST_MODEL (widgets->ws_model), NULL);
    gtk_widget_set_size_request (widgets->ws_dropdown, 140, -1);
    gtk_box_append (GTK_BOX (controls), widgets->ws_dropdown);

    GtkWidget *lock = gtk_button_new_with_label ("🔒 Lock");
    GtkWidget *unlock = gtk_button_new_with_label ("🔓 Unlock");
    GtkWidget *refresh = gtk_button_new_with_label ("🔄 Refresh");

    gtk_box_append (GTK_BOX (controls), lock);
    gtk_box_append (GTK_BOX (controls), unlock);
    gtk_box_append (GTK_BOX (controls), refresh);

    g_signal_connect (lock, "clicked", G_CALLBACK (on_lock_clicked), widgets);
    g_signal_connect (unlock, "clicked", G_CALLBACK (on_unlock_clicked), widgets);
    g_signal_connect (refresh, "clicked", G_CALLBACK (on_refresh_clicked), widgets);

    // Move window controls
    GtkWidget *move_controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (main), move_controls);

    widgets->window_model = gtk_string_list_new (NULL);
    widgets->window_dropdown
        = gtk_drop_down_new (G_LIST_MODEL (widgets->window_model), NULL);
    gtk_widget_set_size_request (widgets->window_dropdown, 200, -1);
    gtk_box_append (GTK_BOX (move_controls), widgets->window_dropdown);

    g_signal_connect (widgets->window_dropdown, "notify::selected",
                      G_CALLBACK (on_window_dropdown_changed), widgets);

    widgets->target_ws_model = gtk_string_list_new (NULL);
    widgets->target_ws_dropdown
        = gtk_drop_down_new (G_LIST_MODEL (widgets->target_ws_model), NULL);
    gtk_widget_set_size_request (widgets->target_ws_dropdown, 140, -1);
    gtk_box_append (GTK_BOX (move_controls), widgets->target_ws_dropdown);

    GtkWidget *move_btn = gtk_button_new_with_label ("Move Window");
    gtk_box_append (GTK_BOX (move_controls), move_btn);

    g_signal_connect (move_btn, "clicked", G_CALLBACK (on_move_clicked), widgets);

    // Workspace table
    widgets->workspace_table = gtk_scrolled_window_new ();
    gtk_widget_set_size_request (widgets->workspace_table, 600, 300);
    gtk_box_append (GTK_BOX (main), widgets->workspace_table);

    gf_refresh_workspaces (widgets);

    gtk_widget_grab_focus (widgets->ws_dropdown);
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
