#include "async.h"
#include "../bridge/ipc_client.h"
#include "../bridge/refresh.h"
#include <string.h>

#ifdef _WIN32
static gboolean
handle_command_response (gpointer user_data)
{
    gf_cmd_result_t *data = (gf_cmd_result_t *)user_data;
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

        GtkWidget *dialog = gtk_window_new ();
        gtk_window_set_title (GTK_WINDOW (dialog), "GridFlux");
        gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
        gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (data->app->window));
        gtk_window_set_default_size (GTK_WINDOW (dialog), 300, 100);

        GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
        gtk_widget_set_margin_start(box, 20); gtk_widget_set_margin_end(box, 20);
        gtk_widget_set_margin_top(box, 20); gtk_widget_set_margin_bottom(box, 20);

        GtkWidget *label = gtk_label_new (text);
        gtk_label_set_wrap (GTK_LABEL (label), TRUE);
        gtk_box_append (GTK_BOX (box), label);

        GtkWidget *button = gtk_button_new_with_label ("OK");
        gtk_widget_set_halign (button, GTK_ALIGN_CENTER);
        g_signal_connect_swapped (button, "clicked", G_CALLBACK (gtk_window_destroy), dialog);
        gtk_box_append (GTK_BOX (box), button);

        gtk_window_set_child (GTK_WINDOW (dialog), box);
        gtk_window_present (GTK_WINDOW (dialog));
    }

    if (data->should_refresh)
    {
        platform_run_refresh (data->app);
    }

    g_free (data);
    return G_SOURCE_REMOVE;
}

static gpointer
run_command_thread (gpointer user_data)
{
    gf_cmd_task_t *data = (gf_cmd_task_t *)user_data;
    gf_ipc_response_t response = gf_run_client_command (data->command);

    gf_cmd_result_t *resp_data = g_new0 (gf_cmd_result_t, 1);
    resp_data->app = data->app;
    resp_data->response = response;
    resp_data->should_refresh = data->should_refresh;
    resp_data->show_dialog = data->show_dialog;

    g_idle_add ((GSourceFunc)handle_command_response, resp_data);

    g_free (data->command);
    g_free (data);
    return NULL;
}

static gboolean
handle_refresh_response (gpointer user_data)
{
    gf_refresh_task_t *data = (gf_refresh_task_t *)user_data;
    gf_refresh_workspaces (data->app);
    g_free (data);
    return G_SOURCE_REMOVE;
}

static gpointer
run_refresh_thread (gpointer user_data)
{
    gf_refresh_task_t *data = (gf_refresh_task_t *)user_data;
    g_usleep (200000);

    gf_ipc_response_t ws_resp = gf_run_client_command ("query workspaces");
    gf_ipc_response_t win_resp = gf_run_client_command ("query windows");

    g_object_set_data_full (G_OBJECT (data->app->window), "ws_response", g_memdup2 (&ws_resp, sizeof(ws_resp)), g_free);
    g_object_set_data_full (G_OBJECT (data->app->window), "win_response", g_memdup2 (&win_resp, sizeof(win_resp)), g_free);

    gf_refresh_task_t *res = g_new0 (gf_refresh_task_t, 1);
    res->app = data->app;
    g_idle_add ((GSourceFunc)handle_refresh_response, res);

    g_free (data);
    return NULL;
}
#endif

void
platform_run_command (gf_app_state_t *app, const char *command, gboolean refresh, gboolean dialog)
{
#ifdef _WIN32
    if (app->operation_in_progress) return;
    app->operation_in_progress = TRUE;

    gf_cmd_task_t *task = g_new0 (gf_cmd_task_t, 1);
    task->app = app;
    task->command = g_strdup (command);
    task->should_refresh = refresh;
    task->show_dialog = dialog;

    g_thread_new ("ipc-command", run_command_thread, task);
#else
    gf_ipc_response_t resp = gf_run_client_command (command);
    if (dialog)
    {
        gf_command_response_t cmd_resp;
        memset (&cmd_resp, 0, sizeof (cmd_resp));
        memcpy (&cmd_resp, resp.message, sizeof (cmd_resp));
        cmd_resp.message[sizeof (cmd_resp.message) - 1] = '\0';

        const char *text = cmd_resp.message[0] ? cmd_resp.message
                                               : (resp.status == GF_IPC_SUCCESS
                                                      ? "Success"
                                                      : "Failed");
        GtkAlertDialog *ad = gtk_alert_dialog_new ("%s", text);
        gtk_alert_dialog_show (ad, GTK_WINDOW (app->window));
    }
    if (refresh)
    {
        gf_refresh_workspaces (app);
    }
#endif
}

void
platform_run_refresh (gf_app_state_t *app)
{
#ifdef _WIN32
    gf_refresh_task_t *task = g_new0 (gf_refresh_task_t, 1);
    task->app = app;
    g_thread_new ("ipc-refresh", run_refresh_thread, task);
#else
    gf_refresh_workspaces (app);
#endif
}
