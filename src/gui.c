#include "config.h"
#include "ipc.h"
#include "ipc_command.h"
#include "list.h"
#include "types.h"
#include <glib.h>
#ifdef __linux__
#include <gdk/x11/gdkx.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <gdk/gdk.h>
#include <gdk/win32/gdkwin32.h>
#define IDI_ICON1 101
#endif
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
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
#ifdef _WIN32
    gboolean operation_in_progress;
#endif
} gf_gtk_app_widget_t;

#ifdef _WIN32
typedef struct
{
    gf_gtk_app_widget_t *app;
    gchar *command;
    gboolean should_refresh;
    gboolean show_dialog;
} gf_gtk_command_data_t;

typedef struct
{
    gf_gtk_app_widget_t *app;
    gf_ipc_response_t response;
    gboolean should_refresh;
    gboolean show_dialog;
} gf_gtk_resp_data_t;

typedef struct
{
    gf_gtk_app_widget_t *app;
} gf_gtk_refresh_data_t;
#endif

// Forward declarations
static void gf_refresh_workspaces (gf_gtk_app_widget_t *app);
static void on_config_button_clicked (GtkButton *btn, gpointer data);
static void on_lock_clicked (GtkButton *btn, gpointer data);
static void on_unlock_clicked (GtkButton *btn, gpointer data);
static void on_refresh_clicked (GtkButton *btn, gpointer data);
static void on_move_clicked (GtkButton *btn, gpointer data);
#ifdef _WIN32
static gboolean handle_command_response (gpointer user_data);
static gboolean handle_refresh_response (gpointer user_data);
static void run_refresh_async (gf_gtk_app_widget_t *app);
#endif

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

#ifdef _WIN32
// Callback when alert dialog is dismissed
static void
on_alert_dialog_response (GObject *source, GAsyncResult *result, gpointer user_data)
{
    GtkAlertDialog *dialog = GTK_ALERT_DIALOG (source);

    // Get the response (we don't need to do anything with it)
    gtk_alert_dialog_choose_finish (dialog, result, NULL);

    g_debug ("[dialog] Alert dialog dismissed");

    // Dialog is automatically freed by GTK
}

// Windows: Async command execution
static gpointer
run_command_thread (gpointer user_data)
{
    gf_gtk_command_data_t *data = (gf_gtk_command_data_t *)user_data;

    g_debug ("[async] Executing command: %s", data->command);

    gf_ipc_response_t response = gf_run_client_command (data->command);

    g_debug ("[async] Command completed with status: %d", response.status);

    // Prepare data for main thread
    gf_gtk_resp_data_t *resp_data = g_new0 (gf_gtk_resp_data_t, 1);
    resp_data->app = data->app;
    resp_data->response = response;
    resp_data->should_refresh = data->should_refresh;
    resp_data->show_dialog = data->show_dialog;

    // Schedule UI update on main thread
    g_idle_add ((GSourceFunc)handle_command_response, resp_data);

    g_free (data->command);
    g_free (data);

    return NULL;
}

static gboolean
handle_command_response (gpointer user_data)
{
    gf_gtk_resp_data_t *data = (gf_gtk_resp_data_t *)user_data;

    g_debug ("[async] Handling response on main thread");

    // Clear operation flag
    data->app->operation_in_progress = FALSE;

    if (data->show_dialog)
    {
        gf_command_response_t cmd_resp;
        memset (&cmd_resp, 0, sizeof (cmd_resp));
        memcpy (&cmd_resp, data->response.message, sizeof (cmd_resp));
        cmd_resp.message[sizeof (cmd_resp.message) - 1] = '\0';

        const char *text = cmd_resp.message[0] ? cmd_resp.message
                                               : (data->response.status == GF_IPC_SUCCESS
                                                      ? "Command executed successfully"
                                                      : "Command failed");

        g_debug ("[async] Showing dialog: %s", text);

        // Create simple message dialog using GTK 4 MessageDialog alternative
        GtkWidget *dialog = gtk_window_new ();
        gtk_window_set_title (GTK_WINDOW (dialog), "GridFlux");
        gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
        gtk_window_set_transient_for (GTK_WINDOW (dialog),
                                      GTK_WINDOW (data->app->window));
        gtk_window_set_default_size (GTK_WINDOW (dialog), 300, 100);

        GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
        gtk_widget_set_margin_top (box, 20);
        gtk_widget_set_margin_bottom (box, 20);
        gtk_widget_set_margin_start (box, 20);
        gtk_widget_set_margin_end (box, 20);

        GtkWidget *label = gtk_label_new (text);
        gtk_label_set_wrap (GTK_LABEL (label), TRUE);
        gtk_box_append (GTK_BOX (box), label);

        GtkWidget *button = gtk_button_new_with_label ("OK");
        gtk_widget_set_halign (button, GTK_ALIGN_CENTER);
        g_signal_connect_swapped (button, "clicked", G_CALLBACK (gtk_window_destroy),
                                  dialog);
        gtk_box_append (GTK_BOX (box), button);

        gtk_window_set_child (GTK_WINDOW (dialog), box);
        gtk_window_present (GTK_WINDOW (dialog));
    }

    if (data->should_refresh)
    {
        g_debug ("[async] Scheduling refresh");
        // Schedule async refresh instead of blocking
        run_refresh_async (data->app);
    }

    g_free (data);
    return G_SOURCE_REMOVE;
}

// Async refresh thread
static gpointer
run_refresh_thread (gpointer user_data)
{
    gf_gtk_refresh_data_t *data = (gf_gtk_refresh_data_t *)user_data;

    g_debug ("[refresh-async] Fetching workspace data");

    // Give server time to process the move
    g_usleep (200000); // 200ms delay

    gf_ipc_response_t ws_response = gf_run_client_command ("query workspaces");
    gf_ipc_response_t win_response = gf_run_client_command ("query windows");

    // Create response data with both results
    gf_gtk_refresh_data_t *resp_data = g_new0 (gf_gtk_refresh_data_t, 1);
    resp_data->app = data->app;

    // Store the responses in app's temporary storage
    // We'll parse them on the main thread
    g_object_set_data_full (G_OBJECT (data->app->window), "ws_response",
                            g_memdup2 (&ws_response, sizeof (ws_response)), g_free);
    g_object_set_data_full (G_OBJECT (data->app->window), "win_response",
                            g_memdup2 (&win_response, sizeof (win_response)), g_free);

    // Schedule UI update on main thread
    g_idle_add ((GSourceFunc)handle_refresh_response, resp_data);

    g_free (data);
    return NULL;
}

static gboolean
handle_refresh_response (gpointer user_data)
{
    gf_gtk_refresh_data_t *data = (gf_gtk_refresh_data_t *)user_data;

    g_debug ("[refresh-async] Updating UI on main thread");

    // Retrieve responses from temporary storage
    gf_ipc_response_t *ws_response
        = g_object_get_data (G_OBJECT (data->app->window), "ws_response");
    gf_ipc_response_t *win_response
        = g_object_get_data (G_OBJECT (data->app->window), "win_response");

    if (!ws_response || !win_response)
    {
        g_warning ("[refresh-async] Missing response data");
        g_free (data);
        return G_SOURCE_REMOVE;
    }

    if (ws_response->status != GF_IPC_SUCCESS || win_response->status != GF_IPC_SUCCESS)
    {
        g_warning ("[refresh-async] IPC query failed");
        g_free (data);
        return G_SOURCE_REMOVE;
    }

    gf_workspace_list_t *workspaces = gf_parse_workspace_list (ws_response->message);
    gf_window_list_t *windows = gf_parse_window_list (win_response->message);

    if (!workspaces || !windows)
    {
        g_warning ("[refresh-async] Failed to parse responses");
        if (workspaces)
            gf_workspace_list_cleanup (workspaces);
        if (windows)
            gf_window_list_cleanup (windows);
        g_free (data);
        return G_SOURCE_REMOVE;
    }

    g_debug ("[refresh-async] Parsed %u workspaces and %u windows", workspaces->count,
             windows->count);

    // Update UI (same code as gf_refresh_workspaces but inline)
    GtkStringList *new_ws_model = gtk_string_list_new (NULL);
    gtk_drop_down_set_model (GTK_DROP_DOWN (data->app->ws_dropdown),
                             G_LIST_MODEL (new_ws_model));
    data->app->ws_model = new_ws_model;

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
    gtk_drop_down_set_model (GTK_DROP_DOWN (data->app->window_dropdown),
                             G_LIST_MODEL (new_window_model));
    data->app->window_model = new_window_model;

    GtkStringList *new_target_ws_model = gtk_string_list_new (NULL);
    gtk_drop_down_set_model (GTK_DROP_DOWN (data->app->target_ws_dropdown),
                             G_LIST_MODEL (new_target_ws_model));
    data->app->target_ws_model = new_target_ws_model;

    GtkWidget *old = gtk_scrolled_window_get_child (
        GTK_SCROLLED_WINDOW (data->app->workspace_table));
    if (old)
        gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (data->app->workspace_table),
                                       NULL);

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

    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (data->app->workspace_table),
                                   grid);

    gf_workspace_list_cleanup (workspaces);
    gf_window_list_cleanup (windows);

    g_debug ("[refresh-async] UI update completed");

    g_free (data);
    return G_SOURCE_REMOVE;
}

static void
run_refresh_async (gf_gtk_app_widget_t *app)
{
    gf_gtk_refresh_data_t *data = g_new0 (gf_gtk_refresh_data_t, 1);
    data->app = app;

    g_debug ("[refresh-async] Starting async refresh");
    g_thread_new ("ipc-refresh", run_refresh_thread, data);
}

static void
run_command_async (gf_gtk_app_widget_t *app, const char *command, gboolean should_refresh,
                   gboolean show_dialog)
{
    // Prevent multiple simultaneous operations
    if (app->operation_in_progress)
    {
        g_debug ("[async] Operation already in progress, ignoring request");
        return;
    }

    app->operation_in_progress = TRUE;

    gf_gtk_command_data_t *data = g_new0 (gf_gtk_command_data_t, 1);
    data->app = app;
    data->command = g_strdup (command);
    data->should_refresh = should_refresh;
    data->show_dialog = show_dialog;

    g_debug ("[async] Starting async command: %s", command);
    g_thread_new ("ipc-command", run_command_thread, data);
}
#endif

static void
on_window_dropdown_changed (GtkDropDown *dropdown, GParamSpec *pspec, gpointer data)
{
    gf_gtk_app_widget_t *app = (gf_gtk_app_widget_t *)data;

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
gf_refresh_workspaces (gf_gtk_app_widget_t *app) // Definition
{
    g_debug ("[refresh] Starting workspace refresh");

    gf_ipc_response_t ws_response = gf_run_client_command ("query workspaces");
    if (ws_response.status != GF_IPC_SUCCESS)
    {
        g_warning ("[refresh] Failed to query workspaces");
        return;
    }

    gf_ipc_response_t win_response = gf_run_client_command ("query windows");
    if (win_response.status != GF_IPC_SUCCESS)
    {
        g_warning ("[refresh] Failed to query windows");
        return;
    }

    gf_workspace_list_t *workspaces = gf_parse_workspace_list (ws_response.message);
    gf_window_list_t *windows = gf_parse_window_list (win_response.message);

    if (!workspaces || !windows)
    {
        g_warning ("[refresh] Failed to parse responses");
        goto cleanup;
    }

    g_debug ("[refresh] Parsed %u workspaces and %u windows", workspaces->count,
             windows->count);

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

    g_debug ("[refresh] Workspace refresh completed successfully");

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
    gf_gtk_app_widget_t *app = data;

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

#ifdef _WIN32
    run_command_async (app, command, TRUE, FALSE);
#else
    gf_ipc_response_t response = gf_run_client_command (command);
    gf_refresh_workspaces (app);
#endif
}

static void
on_unlock_clicked (GtkButton *btn, gpointer data)
{
    gf_gtk_app_widget_t *app = data;

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

#ifdef _WIN32
    run_command_async (app, command, TRUE, FALSE);
#else
    gf_ipc_response_t response = gf_run_client_command (command);
    gf_refresh_workspaces (app);
#endif
}

static void
on_refresh_clicked (GtkButton *btn, gpointer data)
{
#ifdef _WIN32
    run_refresh_async ((gf_gtk_app_widget_t *)data);
#else
    gf_refresh_workspaces ((gf_gtk_app_widget_t *)data);
#endif
}

static void
on_move_clicked (GtkButton *btn, gpointer data)
{
    gf_gtk_app_widget_t *app = data;

    guint wsel = gtk_drop_down_get_selected (GTK_DROP_DOWN (app->window_dropdown));
    guint tsel = gtk_drop_down_get_selected (GTK_DROP_DOWN (app->target_ws_dropdown));

    if (wsel == GTK_INVALID_LIST_POSITION || tsel == GTK_INVALID_LIST_POSITION)
        return;

    GListModel *model = gtk_drop_down_get_model (GTK_DROP_DOWN (app->window_dropdown));
    GArray *window_data = g_object_get_data (G_OBJECT (model), "window-data");

    if (!window_data || wsel >= window_data->len)
    {
        g_warning ("[move] window-data missing or index out of range");
        return;
    }

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

    g_debug ("[move] ipc cmd=\"%s\"", cmd);

#ifdef _WIN32
    // Windows: Run async with dialog and refresh
    run_command_async (app, cmd, TRUE, TRUE);
#else
    // Unix: Run synchronously
    gf_ipc_response_t resp = gf_run_client_command (cmd);

    gf_command_response_t cmd_resp;
    memset (&cmd_resp, 0, sizeof cmd_resp);
    memcpy (&cmd_resp, resp.message, sizeof cmd_resp);
    cmd_resp.message[sizeof (cmd_resp.message) - 1] = '\0';

    const char *text = cmd_resp.message[0] ? cmd_resp.message
                                           : (resp.status == GF_IPC_SUCCESS
                                                  ? "Command executed successfully"
                                                  : "Command failed");

    if (resp.status == GF_IPC_SUCCESS)
        g_debug ("[move] success: %s", text);
    else
        g_warning ("[move] failed: %s", text);

    GtkAlertDialog *dialog = gtk_alert_dialog_new ("");

    gtk_alert_dialog_set_message (dialog, text);
    gtk_alert_dialog_show (dialog, GTK_WINDOW (app->window));

    g_debug ("[move] refreshing workspaces");
    gf_refresh_workspaces (app);
#endif
}

static void
on_window_realize (GtkWidget *widget, gpointer user_data)
{
    GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (widget));
    if (!surface)
        return;

#ifdef _WIN32
    HWND hwnd = GDK_SURFACE_HWND(surface);
    if (hwnd) {
        HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
        if (hIcon) {
            SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            g_debug("Windows icon set successfully");
        } else {
            g_warning("Failed to load icon resource");
        }
    }
#endif

#ifdef __linux__
    if (GDK_IS_X11_SURFACE (surface)) {
        gdk_x11_surface_set_skip_taskbar_hint (GDK_X11_SURFACE (surface), TRUE);
        gdk_x11_surface_set_skip_pager_hint (GDK_X11_SURFACE (surface), TRUE);
    }
#endif
}

static void
gf_gtk_activate (GtkApplication *app, gpointer user_data)
{
    gf_gtk_app_widget_t *widgets = g_new0 (gf_gtk_app_widget_t, 1);

#ifdef _WIN32
    widgets->operation_in_progress = FALSE;
#endif

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
    
    // Set icon name for desktop integration
    gtk_window_set_icon_name (GTK_WINDOW (widgets->window), "gridflux");

    // Set window icon for GTK4
    GError *error = NULL;
    GdkPixbuf *icon = gdk_pixbuf_new_from_file_at_scale ("icons/gridflux-48.png", 48, 48, FALSE, &error);
    if (error) {
        g_warning ("Failed to load icon: %s", error->message);
        g_error_free (error);
    } else {
        // For GTK4, we need to set the icon on the surface after realization
        g_object_set_data_full (G_OBJECT (widgets->window), "window_icon", icon, g_object_unref);
    }

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
    GtkWidget *config = gtk_button_new_with_label ("⚙️ Config");

    gtk_box_append (GTK_BOX (controls), lock);
    gtk_box_append (GTK_BOX (controls), unlock);
    gtk_box_append (GTK_BOX (controls), refresh);
    gtk_box_append (GTK_BOX (controls), config);

    g_signal_connect (lock, "clicked", G_CALLBACK (on_lock_clicked), widgets);
    g_signal_connect (unlock, "clicked", G_CALLBACK (on_unlock_clicked), widgets);
    g_signal_connect (refresh, "clicked", G_CALLBACK (on_refresh_clicked), widgets);
    g_signal_connect (config, "clicked", G_CALLBACK (on_config_button_clicked), widgets);

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

static void
on_config_save_clicked (GtkButton *btn, gpointer data)
{
    GtkWidget *config_window = g_object_get_data (G_OBJECT (btn), "config_window");

    // Get current config
    const char *config_path = gf_config_get_path ();
    if (!config_path)
    {
        GtkAlertDialog *dialog = gtk_alert_dialog_new (NULL);
        gtk_alert_dialog_set_message (dialog, "Failed to get config path");
        gtk_alert_dialog_show (dialog, GTK_WINDOW (config_window));
        return;
    }

    gf_config_t config = load_or_create_config (config_path);

    // Get values from UI
    GtkWidget *max_windows_spin
        = g_object_get_data (G_OBJECT (config_window), "max_windows_spin");
    GtkWidget *max_workspaces_spin
        = g_object_get_data (G_OBJECT (config_window), "max_workspaces_spin");
    GtkWidget *default_padding_spin
        = g_object_get_data (G_OBJECT (config_window), "default_padding_spin");
    GtkWidget *min_window_size_spin
        = g_object_get_data (G_OBJECT (config_window), "min_window_size_spin");

    config.max_windows_per_workspace
        = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (max_windows_spin));
    config.max_workspaces
        = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (max_workspaces_spin));
    config.default_padding
        = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (default_padding_spin));
    config.min_window_size
        = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (min_window_size_spin));

    // Save config
    gf_config_save_config (config_path, &config);

    GtkAlertDialog *dialog = gtk_alert_dialog_new ("");
    gtk_alert_dialog_set_message (dialog, "Configuration saved successfully!");
    gtk_alert_dialog_show (dialog, GTK_WINDOW (config_window));
}

static void
on_config_button_clicked (GtkButton *btn, gpointer data)
{
    gf_gtk_app_widget_t *app = (gf_gtk_app_widget_t *)data;

    // Create config window
    GtkWidget *config_window = gtk_window_new ();
    gtk_window_set_title (GTK_WINDOW (config_window), "GridFlux Configuration");
    gtk_window_set_default_size (GTK_WINDOW (config_window), 400, 450);
    gtk_window_set_modal (GTK_WINDOW (config_window), TRUE);
    gtk_window_set_transient_for (GTK_WINDOW (config_window), GTK_WINDOW (app->window));

    GtkWidget *main = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top (main, 20);
    gtk_widget_set_margin_bottom (main, 20);
    gtk_widget_set_margin_start (main, 20);
    gtk_widget_set_margin_end (main, 20);
    gtk_window_set_child (GTK_WINDOW (config_window), main);

    // Title
    GtkWidget *title = gtk_label_new ("GridFlux Configuration");
    gtk_widget_add_css_class (title, "config-title");
    gtk_box_append (GTK_BOX (main), title);

    // Get current config
    const char *config_path = gf_config_get_path ();
    gf_config_t config = { 0 };
    if (config_path)
    {
        config = load_or_create_config (config_path);
    }
    else
    {
        config = (gf_config_t){ .max_windows_per_workspace = 4,
                                .max_workspaces = 10,
                                .default_padding = 10,
                                .min_window_size = 100 };
    }

    // Configuration form
    GtkWidget *form = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append (GTK_BOX (main), form);

    // Max windows per workspace
    GtkWidget *max_windows_label = gtk_label_new ("Max Windows per Workspace:");
    gtk_widget_set_halign (max_windows_label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (form), max_windows_label);

    GtkWidget *max_windows_spin = gtk_spin_button_new_with_range (1, 20, 1);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (max_windows_spin),
                               config.max_windows_per_workspace);
    gtk_widget_set_hexpand (max_windows_spin, TRUE);
    g_object_set_data (G_OBJECT (config_window), "max_windows_spin", max_windows_spin);
    gtk_box_append (GTK_BOX (form), max_windows_spin);

    // Max workspaces
    GtkWidget *max_workspaces_label = gtk_label_new ("Max Workspaces:");
    gtk_widget_set_halign (max_workspaces_label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (form), max_workspaces_label);

    GtkWidget *max_workspaces_spin = gtk_spin_button_new_with_range (1, 50, 1);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (max_workspaces_spin),
                               config.max_workspaces);
    gtk_widget_set_hexpand (max_workspaces_spin, TRUE);
    g_object_set_data (G_OBJECT (config_window), "max_workspaces_spin",
                       max_workspaces_spin);
    gtk_box_append (GTK_BOX (form), max_workspaces_spin);

    // Default padding
    GtkWidget *default_padding_label = gtk_label_new ("Default Padding:");
    gtk_widget_set_halign (default_padding_label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (form), default_padding_label);

    GtkWidget *default_padding_spin = gtk_spin_button_new_with_range (0, 50, 1);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (default_padding_spin),
                               config.default_padding);
    gtk_widget_set_hexpand (default_padding_spin, TRUE);
    g_object_set_data (G_OBJECT (config_window), "default_padding_spin",
                       default_padding_spin);
    gtk_box_append (GTK_BOX (form), default_padding_spin);

    // Min window size
    GtkWidget *min_window_size_label = gtk_label_new ("Min Window Size:");
    gtk_widget_set_halign (min_window_size_label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (form), min_window_size_label);

    GtkWidget *min_window_size_spin = gtk_spin_button_new_with_range (50, 500, 10);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (min_window_size_spin),
                               config.min_window_size);
    gtk_widget_set_hexpand (min_window_size_spin, TRUE);
    g_object_set_data (G_OBJECT (config_window), "min_window_size_spin",
                       min_window_size_spin);
    gtk_box_append (GTK_BOX (form), min_window_size_spin);

    // Buttons
    GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (main), button_box);

    GtkWidget *save_btn = gtk_button_new_with_label ("Save");
    gtk_widget_add_css_class (save_btn, "config-save-btn");
    g_object_set_data (G_OBJECT (save_btn), "config_window", config_window);
    g_signal_connect (save_btn, "clicked", G_CALLBACK (on_config_save_clicked), NULL);
    gtk_box_append (GTK_BOX (button_box), save_btn);

    GtkWidget *close_btn = gtk_button_new_with_label ("Close");
    g_signal_connect_swapped (close_btn, "clicked", G_CALLBACK (gtk_window_destroy),
                              config_window);
    gtk_box_append (GTK_BOX (button_box), close_btn);

    gtk_window_present (GTK_WINDOW (config_window));
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
