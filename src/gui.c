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
#include <gdk/gdk.h>
#include <gdk/win32/gdkwin32.h>
#include <windows.h>
#define IDI_ICON1 101
#define WM_TRAYICON (WM_USER + 1)
#endif
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

#define WINDOW_WIDTH 700
#define WINDOW_HEIGHT 500
#define TABLE_MIN_WIDTH 600
#define TABLE_MIN_HEIGHT 300
#define DROPDOWN_WIDTH 140
#define WINDOW_DROPDOWN_WIDTH 200
#define CONFIG_WINDOW_WIDTH 400
#define CONFIG_WINDOW_HEIGHT 450
#define COMMAND_BUFFER_SIZE 64
#define WS_ID_STR_SIZE 16
#define REFRESH_DELAY_MS 200000 // 200ms

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
    NOTIFYICONDATA tray_data;
    WNDPROC prev_wnd_proc;
#endif
} gf_gtk_app_widget_t;

#ifdef _WIN32
typedef struct
{
    gf_gtk_app_widget_t *app;
    gchar *command;
    gboolean should_refresh;
    gboolean show_dialog;
} gf_command_data_t;

typedef struct
{
    gf_gtk_app_widget_t *app;
    gf_ipc_response_t response;
    gboolean should_refresh;
    gboolean show_dialog;
} gf_response_data_t;

typedef struct
{
    gf_gtk_app_widget_t *app;
} gf_refresh_data_t;
#endif

static void _refresh_workspaces (gf_gtk_app_widget_t *app);
static void _on_lock_clicked (GtkButton *btn, gpointer data);
static void _on_unlock_clicked (GtkButton *btn, gpointer data);
static void _on_refresh_clicked (GtkButton *btn, gpointer data);
static void _on_move_clicked (GtkButton *btn, gpointer data);
static void _on_config_button_clicked (GtkButton *btn, gpointer data);
static void _on_window_dropdown_changed (GtkDropDown *dropdown, GParamSpec *pspec,
                                         gpointer data);

#ifdef _WIN32
static gboolean _handle_command_response (gpointer user_data);
static gboolean _handle_refresh_response (gpointer user_data);
static void _run_refresh_async (gf_gtk_app_widget_t *app);
static void _run_command_async (gf_gtk_app_widget_t *app, const char *command,
                                gboolean should_refresh, gboolean show_dialog);
static void _setup_tray_icon (gf_gtk_app_widget_t *app, HWND hwnd);
static void _remove_tray_icon (gf_gtk_app_widget_t *app);
static LRESULT CALLBACK _tray_wnd_proc (HWND hwnd, UINT msg, WPARAM wparam,
                                        LPARAM lparam);
#endif

// Check if window is GridFlux itself
static gboolean
_is_window_itself (const gf_window_info_t *win)
{
    return win && win->name[0] && g_strcmp0 (win->name, "GridFlux") == 0;
}

// Validate string is a number
static gboolean
_is_number (const char *s)
{
    if (!s || !*s)
        return FALSE;
    for (; *s; s++)
        if (!g_ascii_isdigit (*s))
            return FALSE;
    return TRUE;
}

static gf_ipc_response_t
_run_client_command (const char *command)
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
_show_alert (GtkWindow *parent, const char *message)
{
    GtkAlertDialog *dialog = gtk_alert_dialog_new ("");
    gtk_alert_dialog_set_message (dialog, message);
    gtk_alert_dialog_show (dialog, parent);
}

static const char *
_get_selected_workspace_id (GtkWidget *dropdown, GtkWindow *parent_window)
{
    guint selected = gtk_drop_down_get_selected (GTK_DROP_DOWN (dropdown));
    if (selected == GTK_INVALID_LIST_POSITION)
    {
        _show_alert (parent_window, "Please select a workspace");
        return NULL;
    }

    GtkStringObject *item
        = GTK_STRING_OBJECT (gtk_drop_down_get_selected_item (GTK_DROP_DOWN (dropdown)));
    if (!item)
    {
        _show_alert (parent_window, "Invalid selection");
        return NULL;
    }

    const char *ws_id = gtk_string_object_get_string (item);
    if (!ws_id || !_is_number (ws_id))
    {
        _show_alert (parent_window, "Please select a valid workspace");
        return NULL;
    }

    return ws_id;
}

static void
_create_grid_headers (GtkWidget *grid)
{
    const char *headers[]
        = { "Workspace", "Windows List", "Total Window", "Available Slots", "Status" };

    for (int i = 0; i < 5; i++)
    {
        GtkWidget *header = gtk_label_new (headers[i]);
        gtk_widget_add_css_class (header, "table-header");
        gtk_grid_attach (GTK_GRID (grid), header, i, 0, 1, 1);
    }

    GtkWidget *sep = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_grid_attach (GTK_GRID (grid), sep, 0, 1, 5, 1);
}

static void
_create_workspace_row (GtkWidget *grid, int row, gf_workspace_info_t *ws,
                       gf_window_list_t *windows)
{
    // Workspace ID
    char ws_id_str[WS_ID_STR_SIZE];
    snprintf (ws_id_str, sizeof (ws_id_str), "%d", ws->id);
    GtkWidget *ws_label = gtk_label_new (ws_id_str);
    gtk_widget_add_css_class (ws_label, "table-cell");
    gtk_grid_attach (GTK_GRID (grid), ws_label, 0, row, 1, 1);

    // Windows list
    GString *windows_list = g_string_new ("");
    int counter = 1;
    for (uint32_t j = 0; j < windows->count; j++)
    {
        gf_window_info_t *win = &windows->items[j];
        if (!win->is_valid || _is_window_itself (win))
            continue;
        if (win->workspace_id == ws->id)
        {
            if (windows_list->len > 0)
                g_string_append (windows_list, "\n");
            g_string_append_printf (windows_list, "[%d] %s", counter, win->name);
            counter++;
        }
    }

    GtkWidget *windows_label = gtk_label_new (windows_list->str);
    gtk_label_set_xalign (GTK_LABEL (windows_label), 0.0);
    gtk_label_set_yalign (GTK_LABEL (windows_label), 0.0);
    gtk_label_set_justify (GTK_LABEL (windows_label), GTK_JUSTIFY_LEFT);
    gtk_label_set_selectable (GTK_LABEL (windows_label), TRUE);
    gtk_widget_add_css_class (windows_label, "table-cell");

    GtkWidget *scroll = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), windows_label);
    gtk_widget_add_css_class (scroll, "table-cell");
    gtk_widget_set_vexpand (scroll, TRUE);
    gtk_widget_set_hexpand (scroll, TRUE);
    gtk_grid_attach (GTK_GRID (grid), scroll, 1, row, 1, 1);
    g_string_free (windows_list, TRUE);

    // Window count
    char count_str[WS_ID_STR_SIZE];
    snprintf (count_str, sizeof (count_str), "%u", ws->window_count);
    GtkWidget *count_label = gtk_label_new (count_str);
    gtk_widget_add_css_class (count_label, "table-cell");
    gtk_grid_attach (GTK_GRID (grid), count_label, 2, row, 1, 1);

    // Available slots
    char slots_str[WS_ID_STR_SIZE];
    snprintf (slots_str, sizeof (slots_str), "%d", ws->available_space);
    GtkWidget *slots_label = gtk_label_new (slots_str);
    gtk_widget_add_css_class (slots_label, "table-cell");
    gtk_grid_attach (GTK_GRID (grid), slots_label, 3, row, 1, 1);

    // Status
    const char *status_text = ws->is_locked ? "🔒 Locked" : "🔓 Unlocked";
    GtkWidget *status_label = gtk_label_new (status_text);
    gtk_widget_add_css_class (status_label, "table-cell");
    gtk_grid_attach (GTK_GRID (grid), status_label, 4, row, 1, 1);
}

static void
_update_ui_models (gf_gtk_app_widget_t *app, gf_workspace_list_t *workspaces,
                   gf_window_list_t *windows)
{
    // Update workspace dropdown
    GtkStringList *new_ws_model = gtk_string_list_new (NULL);
    gtk_drop_down_set_model (GTK_DROP_DOWN (app->ws_dropdown),
                             G_LIST_MODEL (new_ws_model));
    app->ws_model = new_ws_model;

    // Update window dropdown
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

    // Update target workspace dropdown
    GtkStringList *new_target_ws_model = gtk_string_list_new (NULL);
    gtk_drop_down_set_model (GTK_DROP_DOWN (app->target_ws_dropdown),
                             G_LIST_MODEL (new_target_ws_model));
    app->target_ws_model = new_target_ws_model;

    // Populate workspace lists
    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        char ws_id_str[WS_ID_STR_SIZE];
        snprintf (ws_id_str, sizeof (ws_id_str), "%d", workspaces->items[i].id);
        gtk_string_list_append (new_ws_model, ws_id_str);
        gtk_string_list_append (new_target_ws_model, ws_id_str);
    }
}

// Create workspace grid
static GtkWidget *
_create_workspace_grid (gf_workspace_list_t *workspaces, gf_window_list_t *windows)
{
    GtkWidget *grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 5);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
    gtk_widget_set_margin_top (grid, 12);
    gtk_widget_set_margin_bottom (grid, 12);
    gtk_widget_set_margin_start (grid, 12);
    gtk_widget_set_margin_end (grid, 12);

    _create_grid_headers (grid);

    int row = 2;
    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        _create_workspace_row (grid, row, &workspaces->items[i], windows);
        row++;
    }

    return grid;
}

#ifdef _WIN32
// Windows async implementation
static gpointer
_run_command_thread (gpointer user_data)
{
    gf_command_data_t *data = (gf_command_data_t *)user_data;
    g_debug ("[async] Executing command: %s", data->command);

    gf_ipc_response_t response = _run_client_command (data->command);
    g_debug ("[async] Command completed with status: %d", response.status);

    gf_response_data_t *resp_data = g_new0 (gf_response_data_t, 1);
    resp_data->app = data->app;
    resp_data->response = response;
    resp_data->should_refresh = data->should_refresh;
    resp_data->show_dialog = data->show_dialog;

    g_idle_add ((GSourceFunc)_handle_command_response, resp_data);

    g_free (data->command);
    g_free (data);
    return NULL;
}

static gboolean
_handle_command_response (gpointer user_data)
{
    gf_response_data_t *data = (gf_response_data_t *)user_data;
    g_debug ("[async] Handling response on main thread");

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

        _show_alert (GTK_WINDOW (data->app->window), text);
    }

    if (data->should_refresh)
    {
        g_debug ("[async] Scheduling refresh");
        _run_refresh_async (data->app);
    }

    g_free (data);
    return G_SOURCE_REMOVE;
}

static gpointer
_run_refresh_thread (gpointer user_data)
{
    gf_refresh_data_t *data = (gf_refresh_data_t *)user_data;
    g_debug ("[refresh-async] Fetching workspace data");

    g_usleep (REFRESH_DELAY_MS);

    gf_ipc_response_t ws_response = _run_client_command ("query workspaces");
    gf_ipc_response_t win_response = _run_client_command ("query windows");

    g_object_set_data_full (G_OBJECT (data->app->window), "ws_response",
                            g_memdup2 (&ws_response, sizeof (ws_response)), g_free);
    g_object_set_data_full (G_OBJECT (data->app->window), "win_response",
                            g_memdup2 (&win_response, sizeof (win_response)), g_free);

    g_idle_add ((GSourceFunc)_handle_refresh_response, data);
    return NULL;
}

static gboolean
_handle_refresh_response (gpointer user_data)
{
    gf_refresh_data_t *data = (gf_refresh_data_t *)user_data;
    g_debug ("[refresh-async] Updating UI on main thread");

    gf_ipc_response_t *ws_response
        = g_object_get_data (G_OBJECT (data->app->window), "ws_response");
    gf_ipc_response_t *win_response
        = g_object_get_data (G_OBJECT (data->app->window), "win_response");

    if (!ws_response || !win_response || ws_response->status != GF_IPC_SUCCESS
        || win_response->status != GF_IPC_SUCCESS)
    {
        g_warning ("[refresh-async] Query failed or missing response data");
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

    _update_ui_models (data->app, workspaces, windows);

    GtkWidget *old = gtk_scrolled_window_get_child (
        GTK_SCROLLED_WINDOW (data->app->workspace_table));
    if (old)
        gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (data->app->workspace_table),
                                       NULL);

    GtkWidget *grid = _create_workspace_grid (workspaces, windows);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (data->app->workspace_table),
                                   grid);

    gf_workspace_list_cleanup (workspaces);
    gf_window_list_cleanup (windows);
    g_debug ("[refresh-async] UI update completed");

    g_free (data);
    return G_SOURCE_REMOVE;
}

static void
_run_refresh_async (gf_gtk_app_widget_t *app)
{
    gf_refresh_data_t *data = g_new0 (gf_refresh_data_t, 1);
    data->app = app;
    g_debug ("[refresh-async] Starting async refresh");
    g_thread_new ("ipc-refresh", _run_refresh_thread, data);
}

static void
_run_command_async (gf_gtk_app_widget_t *app, const char *command,
                    gboolean should_refresh, gboolean show_dialog)
{
    if (app->operation_in_progress)
    {
        g_debug ("[async] Operation already in progress, ignoring request");
        return;
    }

    app->operation_in_progress = TRUE;

    gf_command_data_t *data = g_new0 (gf_command_data_t, 1);
    data->app = app;
    data->command = g_strdup (command);
    data->should_refresh = should_refresh;
    data->show_dialog = show_dialog;

    g_debug ("[async] Starting async command: %s", command);
    g_thread_new ("ipc-command", _run_command_thread, data);
}
#endif

static void
_on_window_dropdown_changed (GtkDropDown *dropdown, GParamSpec *pspec, gpointer data)
{
    gf_gtk_app_widget_t *app = (gf_gtk_app_widget_t *)data;

    guint selected = gtk_drop_down_get_selected (dropdown);
    if (selected == GTK_INVALID_LIST_POSITION)
        return;

    GListModel *model = gtk_drop_down_get_model (dropdown);
    GArray *window_data = g_object_get_data (G_OBJECT (model), "window-data");

    if (!window_data || selected >= window_data->len)
        return;

    gf_window_info_t *win = &g_array_index (window_data, gf_window_info_t, selected);
    int current_workspace = win->workspace_id;

    gf_ipc_response_t ws_response = _run_client_command ("query workspaces");
    gf_workspace_list_t *workspaces = gf_parse_workspace_list (ws_response.message);

    if (!workspaces)
        return;

    GtkStringList *new_target_ws_model = gtk_string_list_new (NULL);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_workspace_info_t *ws = &workspaces->items[i];
        if (ws->id == current_workspace || ws->is_locked || ws->available_space == 0)
            continue;

        char ws_id_str[WS_ID_STR_SIZE];
        snprintf (ws_id_str, sizeof (ws_id_str), "%d", ws->id);
        gtk_string_list_append (new_target_ws_model, ws_id_str);
    }

    gtk_drop_down_set_model (GTK_DROP_DOWN (app->target_ws_dropdown),
                             G_LIST_MODEL (new_target_ws_model));
    app->target_ws_model = new_target_ws_model;

    gf_workspace_list_cleanup (workspaces);
}

static void
_refresh_workspaces (gf_gtk_app_widget_t *app)
{
    g_debug ("[refresh] Starting workspace refresh");

    gf_ipc_response_t ws_response = _run_client_command ("query workspaces");
    if (ws_response.status != GF_IPC_SUCCESS)
    {
        g_warning ("[refresh] Failed to query workspaces");
        return;
    }

    gf_ipc_response_t win_response = _run_client_command ("query windows");
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

    _update_ui_models (app, workspaces, windows);

    GtkWidget *old
        = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (app->workspace_table));
    if (old)
        gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (app->workspace_table), NULL);

    GtkWidget *grid = _create_workspace_grid (workspaces, windows);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (app->workspace_table), grid);

    g_debug ("[refresh] Workspace refresh completed successfully");

cleanup:
    if (workspaces)
        gf_workspace_list_cleanup (workspaces);
    if (windows)
        gf_window_list_cleanup (windows);
}

static void
_on_lock_clicked (GtkButton *btn, gpointer data)
{
    gf_gtk_app_widget_t *app = data;
    const char *ws_id
        = _get_selected_workspace_id (app->ws_dropdown, GTK_WINDOW (app->window));
    if (!ws_id)
        return;

    char command[COMMAND_BUFFER_SIZE];
    snprintf (command, sizeof (command), "lock %s", ws_id);

#ifdef _WIN32
    _run_command_async (app, command, TRUE, FALSE);
#else
    _run_client_command (command);
    _refresh_workspaces (app);
#endif
}

static void
_on_unlock_clicked (GtkButton *btn, gpointer data)
{
    gf_gtk_app_widget_t *app = data;
    const char *ws_id
        = _get_selected_workspace_id (app->ws_dropdown, GTK_WINDOW (app->window));
    if (!ws_id)
        return;

    char command[COMMAND_BUFFER_SIZE];
    snprintf (command, sizeof (command), "unlock %s", ws_id);

#ifdef _WIN32
    _run_command_async (app, command, TRUE, FALSE);
#else
    _run_client_command (command);
    _refresh_workspaces (app);
#endif
}

static void
_on_refresh_clicked (GtkButton *btn, gpointer data)
{
#ifdef _WIN32
    _run_refresh_async ((gf_gtk_app_widget_t *)data);
#else
    _refresh_workspaces ((gf_gtk_app_widget_t *)data);
#endif
}

static void
_on_move_clicked (GtkButton *btn, gpointer data)
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

    char cmd[COMMAND_BUFFER_SIZE];
    snprintf (cmd, sizeof (cmd), "move %lu %s", win->id, target_ws);
    g_debug ("[move] ipc cmd=\"%s\"", cmd);

#ifdef _WIN32
    _run_command_async (app, cmd, TRUE, TRUE);
#else
    gf_ipc_response_t resp = _run_client_command (cmd);

    gf_command_response_t cmd_resp;
    memset (&cmd_resp, 0, sizeof (cmd_resp));
    memcpy (&cmd_resp, resp.message, sizeof (cmd_resp));
    cmd_resp.message[sizeof (cmd_resp.message) - 1] = '\0';

    const char *text = cmd_resp.message[0] ? cmd_resp.message
                                           : (resp.status == GF_IPC_SUCCESS
                                                  ? "Command executed successfully"
                                                  : "Command failed");

    if (resp.status == GF_IPC_SUCCESS)
        g_debug ("[move] success: %s", text);
    else
        g_warning ("[move] failed: %s", text);

    _show_alert (GTK_WINDOW (app->window), text);
    g_debug ("[move] refreshing workspaces");
    _refresh_workspaces (app);
#endif
}

static void
_on_config_save_clicked (GtkButton *btn, gpointer data)
{
    GtkWidget *config_window = g_object_get_data (G_OBJECT (btn), "config_window");

    const char *config_path = gf_config_get_path ();
    if (!config_path)
    {
        _show_alert (GTK_WINDOW (config_window), "Failed to get config path");
        return;
    }

    gf_config_t config = load_or_create_config (config_path);

    GtkWidget *max_windows_spin
        = g_object_get_data (G_OBJECT (config_window), "max_windows_spin");
    GtkWidget *max_workspaces_spin
        = g_object_get_data (G_OBJECT (config_window), "max_workspaces_spin");
    GtkWidget *default_padding_spin
        = g_object_get_data (G_OBJECT (config_window), "default_padding_spin");
    GtkWidget *min_window_size_spin
        = g_object_get_data (G_OBJECT (config_window), "min_window_size_spin");
    GtkWidget *border_color_btn
        = g_object_get_data (G_OBJECT (config_window), "border_color_btn");
    GtkWidget *enable_borders_check
        = g_object_get_data (G_OBJECT (config_window), "enable_borders_check");

    config.max_windows_per_workspace
        = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (max_windows_spin));
    config.max_workspaces
        = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (max_workspaces_spin));
    config.default_padding
        = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (default_padding_spin));
    config.min_window_size
        = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (min_window_size_spin));

    // Get color from picker
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (border_color_btn), &rgba);

    // Convert RGBA to 0x00BBGGRR
    uint8_t r = (uint8_t)(rgba.red * 255.0);
    uint8_t g = (uint8_t)(rgba.green * 255.0);
    uint8_t b = (uint8_t)(rgba.blue * 255.0);

    // Use standard 0x00RRGGBB format
    config.border_color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;

    config.enable_borders
        = gtk_check_button_get_active (GTK_CHECK_BUTTON (enable_borders_check));

    gf_config_save_config (config_path, &config);
    _show_alert (GTK_WINDOW (config_window), "Configuration saved successfully!");
}

// Helper: Create config spin button row
static void
_create_config_spin_row (GtkWidget *form, GtkWidget *config_window,
                         const char *label_text, const char *data_key, double min,
                         double max, double step, double value)
{
    GtkWidget *label = gtk_label_new (label_text);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (form), label);

    GtkWidget *spin = gtk_spin_button_new_with_range (min, max, step);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin), value);
    gtk_widget_set_hexpand (spin, TRUE);
    g_object_set_data (G_OBJECT (config_window), data_key, spin);
    gtk_box_append (GTK_BOX (form), spin);
}

// Helper: Create config color picker row
static void
_create_config_color_row (GtkWidget *form, GtkWidget *config_window,
                          const char *label_text, const char *data_key,
                          uint32_t color_ref)
{
    GtkWidget *label = gtk_label_new (label_text);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (form), label);

    GtkWidget *btn = gtk_color_button_new ();

    // Convert 0x00RRGGBB to GdkRGBA
    GdkRGBA rgba;
    rgba.red = (float)((color_ref >> 16) & 0xFF) / 255.0f;
    rgba.green = (float)((color_ref >> 8) & 0xFF) / 255.0f;
    rgba.blue = (float)(color_ref & 0xFF) / 255.0f;
    rgba.alpha = 1.0f;

    gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (btn), &rgba);

    gtk_widget_set_hexpand (btn, TRUE);
    g_object_set_data (G_OBJECT (config_window), data_key, btn);
    gtk_box_append (GTK_BOX (form), btn);
}

// Helper: Create config toggle row
static void
_create_config_toggle_row (GtkWidget *form, GtkWidget *config_window,
                           const char *label_text, const char *data_key, bool value)
{
    GtkWidget *check = gtk_check_button_new_with_label (label_text);
    gtk_check_button_set_active (GTK_CHECK_BUTTON (check), value);
    g_object_set_data (G_OBJECT (config_window), data_key, check);
    gtk_box_append (GTK_BOX (form), check);
}

static void
_on_config_button_clicked (GtkButton *btn, gpointer data)
{
    gf_gtk_app_widget_t *app = (gf_gtk_app_widget_t *)data;

    GtkWidget *config_window = gtk_window_new ();
    gtk_window_set_title (GTK_WINDOW (config_window), "GridFlux Configuration");
    gtk_window_set_default_size (GTK_WINDOW (config_window), CONFIG_WINDOW_WIDTH,
                                 CONFIG_WINDOW_HEIGHT);
    gtk_window_set_modal (GTK_WINDOW (config_window), TRUE);
    gtk_window_set_transient_for (GTK_WINDOW (config_window), GTK_WINDOW (app->window));

    GtkWidget *main = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top (main, 20);
    gtk_widget_set_margin_bottom (main, 20);
    gtk_widget_set_margin_start (main, 20);
    gtk_widget_set_margin_end (main, 20);
    gtk_window_set_child (GTK_WINDOW (config_window), main);

    GtkWidget *title = gtk_label_new ("GridFlux Configuration");
    gtk_widget_add_css_class (title, "config-title");
    gtk_box_append (GTK_BOX (main), title);

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
                                .min_window_size = 100,
                                .border_color = 0x00F49D2A };
    }

    GtkWidget *form = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append (GTK_BOX (main), form);

    _create_config_spin_row (form, config_window,
                             "Max Windows per Workspace:", "max_windows_spin", 1, 20, 1,
                             config.max_windows_per_workspace);
    _create_config_spin_row (form, config_window,
                             "Max Workspaces:", "max_workspaces_spin", 1, 50, 1,
                             config.max_workspaces);
    _create_config_spin_row (form, config_window,
                             "Min Window Size:", "min_window_size_spin", 50, 500, 10,
                             config.min_window_size);
    _create_config_color_row (form, config_window,
                              "Active Window Border Color:", "border_color_btn",
                              config.border_color);
    _create_config_toggle_row (form, config_window, "Enable Window Borders",
                               "enable_borders_check", config.enable_borders);
    GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (main), button_box);

    GtkWidget *save_btn = gtk_button_new_with_label ("Save");
    gtk_widget_add_css_class (save_btn, "config-save-btn");
    g_object_set_data (G_OBJECT (save_btn), "config_window", config_window);
    g_signal_connect (save_btn, "clicked", G_CALLBACK (_on_config_save_clicked), NULL);
    gtk_box_append (GTK_BOX (button_box), save_btn);

    GtkWidget *close_btn = gtk_button_new_with_label ("Close");
    g_signal_connect_swapped (close_btn, "clicked", G_CALLBACK (gtk_window_destroy),
                              config_window);
    gtk_box_append (GTK_BOX (button_box), close_btn);

    gtk_window_present (GTK_WINDOW (config_window));
}

// Helper: Create button with label
static GtkWidget *
_create_button (const char *label, GCallback callback, gpointer data)
{
    GtkWidget *button = gtk_button_new_with_label (label);
    if (callback)
        g_signal_connect (button, "clicked", callback, data);
    return button;
}

// Helper: Create dropdown
static GtkWidget *
_create_dropdown (GtkStringList **model, int width)
{
    *model = gtk_string_list_new (NULL);
    GtkWidget *dropdown = gtk_drop_down_new (G_LIST_MODEL (*model), NULL);
    gtk_widget_set_size_request (dropdown, width, -1);
    return dropdown;
}

#ifdef _WIN32
static void
_setup_tray_icon (gf_gtk_app_widget_t *app, HWND hwnd)
{
    memset (&app->tray_data, 0, sizeof (NOTIFYICONDATA));
    app->tray_data.cbSize = sizeof (NOTIFYICONDATA);
    app->tray_data.hWnd = hwnd;
    app->tray_data.uID = 1;
    app->tray_data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    app->tray_data.uCallbackMessage = WM_TRAYICON;
    HICON hIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_ICON1));
    if (!hIcon)
    {
        hIcon = (HICON)LoadImageA (NULL, "..\\icons\\gridflux.ico", IMAGE_ICON, 0, 0,
                                   LR_LOADFROMFILE | LR_DEFAULTSIZE);
    }
    app->tray_data.hIcon = hIcon;
    strcpy (app->tray_data.szTip, "GridFlux Control Panel");

    Shell_NotifyIcon (NIM_ADD, &app->tray_data);

    // Subclass the window to handle tray messages
    SetProp (hwnd, "GF_APP_PTR", (HANDLE)app);
    app->prev_wnd_proc
        = (WNDPROC)SetWindowLongPtr (hwnd, GWLP_WNDPROC, (LONG_PTR)_tray_wnd_proc);
}

static void
_remove_tray_icon (gf_gtk_app_widget_t *app)
{
    Shell_NotifyIcon (NIM_DELETE, &app->tray_data);
}

static LRESULT CALLBACK
_tray_wnd_proc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    gf_gtk_app_widget_t *app = (gf_gtk_app_widget_t *)GetProp (hwnd, "GF_APP_PTR");

    if (msg == WM_TRAYICON)
    {
        if (lparam == WM_LBUTTONDBLCLK)
        {
            if (gtk_widget_get_visible (app->window))
                gtk_widget_set_visible (app->window, FALSE);
            else
            {
                gtk_widget_set_visible (app->window, TRUE);
                gtk_window_present (GTK_WINDOW (app->window));
                SetForegroundWindow (hwnd);
            }
            return 0;
        }
        else if (lparam == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos (&pt);
            HMENU hMenu = CreatePopupMenu ();
            if (hMenu)
            {
                InsertMenu (hMenu, 0, MF_BYPOSITION | MF_STRING, 1,
                            gtk_widget_get_visible (app->window) ? "Hide" : "Show");
                InsertMenu (hMenu, 1, MF_BYPOSITION | MF_STRING, 2, "Exit");

                SetForegroundWindow (hwnd);
                int id = TrackPopupMenu (hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x,
                                         pt.y, 0, hwnd, NULL);
                if (id == 1)
                {
                    if (gtk_widget_get_visible (app->window))
                        gtk_widget_set_visible (app->window, FALSE);
                    else
                    {
                        gtk_widget_set_visible (app->window, TRUE);
                        gtk_window_present (GTK_WINDOW (app->window));
                        SetForegroundWindow (hwnd);
                    }
                }
                else if (id == 2)
                {
                    PostQuitMessage (0);
                    // Also tell GTK to quit
                    g_application_quit (G_APPLICATION (
                        gtk_window_get_application (GTK_WINDOW (app->window))));
                }
                DestroyMenu (hMenu);
            }
            return 0;
        }
    }
    else if (msg == WM_DESTROY)
    {
        _remove_tray_icon (app);
        RemoveProp (hwnd, "GF_APP_PTR");
    }

    if (app && app->prev_wnd_proc)
        return CallWindowProc (app->prev_wnd_proc, hwnd, msg, wparam, lparam);

    return DefWindowProc (hwnd, msg, wparam, lparam);
}
#endif

static void
_on_window_realize (GtkWidget *widget, gpointer user_data)
{
    gf_gtk_app_widget_t *app = (gf_gtk_app_widget_t *)user_data;
    GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (widget));
    if (!surface)
        return;

#ifdef _WIN32
    HWND hwnd = GDK_SURFACE_HWND (surface);
    if (hwnd)
    {
        HICON hIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_ICON1));
        if (!hIcon)
        {
            hIcon = (HICON)LoadImageA (NULL, "icons\\gridflux.ico", IMAGE_ICON, 0, 0,
                                       LR_LOADFROMFILE | LR_DEFAULTSIZE);
        }

        if (hIcon)
        {
            SendMessage (hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            SendMessage (hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            g_debug ("Windows icon set successfully");
        }
        else
        {
            g_warning ("Failed to load icon resource");
        }

        _setup_tray_icon (app, hwnd);
    }
#endif

#ifdef __linux__
    if (GDK_IS_X11_SURFACE (surface))
    {
        gdk_x11_surface_set_skip_taskbar_hint (GDK_X11_SURFACE (surface), TRUE);
        gdk_x11_surface_set_skip_pager_hint (GDK_X11_SURFACE (surface), TRUE);
    }
#endif
}

static void
_setup_css_provider (void)
{
    GtkCssProvider *provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_string (provider, ".table-cell {"
                                                 "  border: 1px solid @borders;"
                                                 "  padding: 4px;"
                                                 "  background-color: @theme_base_color;"
                                                 "  color: @theme_text_color;"
                                                 "}"
                                                 ".table-header {"
                                                 "  border: 1px solid @borders;"
                                                 "  padding: 4px;"
                                                 "  background-color: @theme_bg_color;"
                                                 "  color: @theme_fg_color;"
                                                 "  font-weight: bold;"
                                                 "}");

    gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                                GTK_STYLE_PROVIDER (provider),
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref (provider);
}

static void
_setup_window_icon (GtkWidget *window)
{
    gtk_window_set_icon_name (GTK_WINDOW (window), "gridflux");

    GError *error = NULL;
    GdkPixbuf *icon = gdk_pixbuf_new_from_file_at_scale ("icons/gridflux-48.png", 48, 48,
                                                         FALSE, &error);

    if (error)
    {
        g_warning ("Failed to load icon: %s", error->message);
        g_error_free (error);
    }
    else
    {
        g_object_set_data_full (G_OBJECT (window), "window_icon", icon, g_object_unref);
    }

    g_signal_connect (window, "realize", G_CALLBACK (_on_window_realize),
                      g_object_get_data (G_OBJECT (window), "gf_widgets"));
}

#ifdef _WIN32
static gboolean
_on_window_close_request (GtkWindow *window, gpointer user_data)
{
    gtk_widget_set_visible (GTK_WIDGET (window), FALSE);
    return TRUE; // Stop the signal from propagating (prevents closing)
}
#endif

static void
_create_control_panel (GtkWidget *main, gf_gtk_app_widget_t *widgets)
{
    GtkWidget *controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (main), controls);

    widgets->ws_dropdown = _create_dropdown (&widgets->ws_model, DROPDOWN_WIDTH);
    gtk_box_append (GTK_BOX (controls), widgets->ws_dropdown);

    gtk_box_append (GTK_BOX (controls),
                    _create_button ("🔒 Lock", G_CALLBACK (_on_lock_clicked), widgets));
    gtk_box_append (
        GTK_BOX (controls),
        _create_button ("🔓 Unlock", G_CALLBACK (_on_unlock_clicked), widgets));
    gtk_box_append (
        GTK_BOX (controls),
        _create_button ("🔄 Refresh", G_CALLBACK (_on_refresh_clicked), widgets));
    gtk_box_append (
        GTK_BOX (controls),
        _create_button ("⚙️ Config", G_CALLBACK (_on_config_button_clicked), widgets));
}

static void
_create_move_panel (GtkWidget *main, gf_gtk_app_widget_t *widgets)
{
    GtkWidget *move_controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (main), move_controls);

    widgets->window_dropdown
        = _create_dropdown (&widgets->window_model, WINDOW_DROPDOWN_WIDTH);
    gtk_box_append (GTK_BOX (move_controls), widgets->window_dropdown);

    g_signal_connect (widgets->window_dropdown, "notify::selected",
                      G_CALLBACK (_on_window_dropdown_changed), widgets);

    widgets->target_ws_dropdown
        = _create_dropdown (&widgets->target_ws_model, DROPDOWN_WIDTH);
    gtk_box_append (GTK_BOX (move_controls), widgets->target_ws_dropdown);

    GtkWidget *move_btn
        = _create_button ("Move Window", G_CALLBACK (_on_move_clicked), widgets);
    gtk_box_append (GTK_BOX (move_controls), move_btn);
}

static void
_gf_gtk_activate (GtkApplication *app, gpointer user_data)
{
    gf_gtk_app_widget_t *widgets = g_new0 (gf_gtk_app_widget_t, 1);

#ifdef _WIN32
    widgets->operation_in_progress = FALSE;
#endif

    _setup_css_provider ();

    widgets->window = gtk_application_window_new (app);
    gtk_window_set_title (GTK_WINDOW (widgets->window), "GridFlux");
    gtk_window_set_default_size (GTK_WINDOW (widgets->window), WINDOW_WIDTH,
                                 WINDOW_HEIGHT);
    gtk_window_set_resizable (GTK_WINDOW (widgets->window), TRUE);

    g_object_set_data (G_OBJECT (widgets->window), "gf_widgets", widgets);
    _setup_window_icon (widgets->window);

#ifdef _WIN32
    g_signal_connect (widgets->window, "close-request",
                      G_CALLBACK (_on_window_close_request), widgets);
#endif

    GtkWidget *main = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_window_set_child (GTK_WINDOW (widgets->window), main);

    _create_control_panel (main, widgets);
    _create_move_panel (main, widgets);

    widgets->workspace_table = gtk_scrolled_window_new ();
    gtk_widget_set_size_request (widgets->workspace_table, TABLE_MIN_WIDTH,
                                 TABLE_MIN_HEIGHT);
    gtk_box_append (GTK_BOX (main), widgets->workspace_table);

    _refresh_workspaces (widgets);

    gtk_widget_grab_focus (widgets->ws_dropdown);
    gtk_window_present (GTK_WINDOW (widgets->window));
}

int
main (int argc, char **argv)
{
#ifdef _WIN32
    FreeConsole ();
#endif

    GtkApplication *app
        = gtk_application_new ("dev.gridflux.gui", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect (app, "activate", G_CALLBACK (_gf_gtk_activate), NULL);

    int status = g_application_run (G_APPLICATION (app), argc, argv);
    g_object_unref (app);
    return status;
}
