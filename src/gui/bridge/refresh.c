#include "refresh.h"
#include "../platform/async.h"
#include "../widgets/workspace_card.h"
#include "ipc_client.h"

static gboolean
is_window_itself (const gf_win_info_t *win)
{
    if (!win || !win->name[0])
        return FALSE;
    return g_strcmp0 (win->name, "GridFlux") == 0;
}

void
gf_build_workspace_grid (GtkGrid *grid, gf_ws_list_t *workspaces,
                         const gf_win_list_t *windows)
{
    const char *headers[]
        = { "Workspace", "Window Count", "Windows", "Available Slots", "Layout",
            "Status" };
    for (int i = 0; i < 6; i++)
    {
        GtkWidget *h = gtk_label_new (headers[i]);
        gtk_widget_add_css_class (h, "table-header");
        gtk_grid_attach (grid, h, i, 0, 1, 1);
    }
    gtk_grid_attach (grid, gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), 0, 1, 6, 1);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_gui_workspace_card_add_to_grid (grid, &workspaces->items[i], windows, i + 2);
    }
}

void
gf_refresh_workspaces (gf_app_state_t *app)
{
    gf_ipc_response_t ws_resp;
    gf_ipc_response_t win_resp;

#ifdef _WIN32
    gf_ipc_response_t *ws_ptr = g_object_get_data (G_OBJECT (app->window), "ws_response");
    gf_ipc_response_t *win_ptr
        = g_object_get_data (G_OBJECT (app->window), "win_response");

    if (ws_ptr && win_ptr)
    {
        ws_resp = *ws_ptr;
        win_resp = *win_ptr;
        g_object_set_data (G_OBJECT (app->window), "ws_response", NULL);
        g_object_set_data (G_OBJECT (app->window), "win_response", NULL);
    }
    else
    {
        ws_resp = gf_run_client_command ("query workspaces");
        win_resp = gf_run_client_command ("query windows");
    }
#else
    ws_resp = gf_run_client_command ("query workspaces");
    win_resp = gf_run_client_command ("query windows");
#endif

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

    // Update models
    GtkStringList *new_ws_model = gtk_string_list_new (NULL);
    GtkStringList *new_target_ws_model = gtk_string_list_new (NULL);
    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        char id_str[16];
        snprintf (id_str, sizeof (id_str), "%d", workspaces->items[i].id);
        gtk_string_list_append (new_ws_model, id_str);
        gtk_string_list_append (new_target_ws_model, id_str);
    }

    GtkStringList *new_window_model = gtk_string_list_new (NULL);
    GArray *window_data = g_array_new (FALSE, FALSE, sizeof (gf_win_info_t));
    for (uint32_t i = 0; i < windows->count; i++)
    {
        if (is_window_itself (&windows->items[i]) || !windows->items[i].is_valid)
            continue;
        gtk_string_list_append (new_window_model, windows->items[i].name);
        g_array_append_val (window_data, windows->items[i]);
    }

    g_object_set_data_full (G_OBJECT (new_window_model), "window-data", window_data,
                            (GDestroyNotify)g_array_unref);

    gtk_drop_down_set_model (GTK_DROP_DOWN (app->ws_dropdown),
                             G_LIST_MODEL (new_ws_model));
    app->ws_model = new_ws_model;
    gtk_drop_down_set_model (GTK_DROP_DOWN (app->window_dropdown),
                             G_LIST_MODEL (new_window_model));
    app->window_model = new_window_model;
    gtk_drop_down_set_model (GTK_DROP_DOWN (app->target_ws_dropdown),
                             G_LIST_MODEL (new_target_ws_model));
    app->target_ws_model = new_target_ws_model;

    // Update grid
    GtkGrid *grid = GTK_GRID (gtk_grid_new ());
    gtk_grid_set_row_spacing (grid, 5);
    gtk_grid_set_column_spacing (grid, 10);
    gtk_widget_set_margin_start (GTK_WIDGET (grid), 12);
    gtk_widget_set_margin_end (GTK_WIDGET (grid), 12);
    gtk_widget_set_margin_top (GTK_WIDGET (grid), 12);
    gtk_widget_set_margin_bottom (GTK_WIDGET (grid), 12);

    gf_build_workspace_grid (grid, workspaces, windows);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (app->workspace_table),
                                   GTK_WIDGET (grid));

    gf_workspace_list_cleanup (workspaces);
    gf_window_list_cleanup (windows);
}
