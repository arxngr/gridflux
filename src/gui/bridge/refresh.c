#include "refresh.h"
#include "../widgets/workspace_card.h"
#include "ipc_client.h"

static GtkWidget *
build_card_list (gf_ws_list_t *workspaces, const gf_win_list_t *windows,
                 gf_app_state_t *app)
{
    GtkWidget *list = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class (list, "gf-wslist");
    gtk_widget_set_margin_start (list, 12);
    gtk_widget_set_margin_end (list, 12);
    gtk_widget_set_margin_top (list, 12);
    gtk_widget_set_margin_bottom (list, 12);

    for (uint32_t i = 0; i < workspaces->count; i++)
        gtk_box_append (GTK_BOX (list),
                        gf_gui_workspace_card_new (&workspaces->items[i], windows, app));
    return list;
}

static gboolean
fetch_responses (gf_app_state_t *app, gf_ipc_response_t *ws_resp,
                 gf_ipc_response_t *win_resp)
{
#ifdef _WIN32
    gf_ipc_response_t *ws_ptr = g_object_get_data (G_OBJECT (app->window), "ws_response");
    gf_ipc_response_t *win_ptr
        = g_object_get_data (G_OBJECT (app->window), "win_response");
    if (ws_ptr && win_ptr)
    {
        *ws_resp = *ws_ptr;
        *win_resp = *win_ptr;
        g_object_set_data (G_OBJECT (app->window), "ws_response", NULL);
        g_object_set_data (G_OBJECT (app->window), "win_response", NULL);
        return TRUE;
    }
#endif
    (void)app;
    *ws_resp = gf_run_client_command ("query workspaces");
    *win_resp = gf_run_client_command ("query windows");
    return TRUE;
}

void
gf_refresh_workspaces (gf_app_state_t *app)
{
    gf_ipc_response_t ws_resp, win_resp;
    fetch_responses (app, &ws_resp, &win_resp);
    if (ws_resp.status != GF_IPC_SUCCESS || win_resp.status != GF_IPC_SUCCESS)
        return;

    gf_ws_list_t *workspaces = gf_parse_workspace_list (ws_resp.message);
    gf_win_list_t *windows = gf_parse_window_list (win_resp.message);
    if (!workspaces || !windows)
    {
        if (workspaces)
            gf_workspace_list_cleanup (workspaces);
        if (windows)
            gf_window_list_cleanup (windows);
        return;
    }

    GtkWidget *list = build_card_list (workspaces, windows, app);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (app->workspace_table), list);

    gf_workspace_list_cleanup (workspaces);
    gf_window_list_cleanup (windows);
}
