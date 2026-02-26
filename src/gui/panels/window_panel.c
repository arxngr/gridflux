#include "window_panel.h"
#include "../bridge/ipc_client.h"
#include "../bridge/refresh.h"
#include "../panels/rules_panel.h"
#include "../panels/settings_panel.h"
#include "../platform/async.h"
#include "../utils/icon_win32.h"
#include <stdio.h>
#include <string.h>


static void
on_window_dropdown_changed (GtkDropDown *dropdown, GParamSpec *pspec, gpointer data)
{
    gf_app_state_t *app = (gf_app_state_t *)data;
    guint selected = gtk_drop_down_get_selected (dropdown);
    if (selected == GTK_INVALID_LIST_POSITION)
        return;

    GListModel *model = gtk_drop_down_get_model (dropdown);
    GArray *window_data = g_object_get_data (G_OBJECT (model), "window-data");
    if (!window_data || selected >= window_data->len)
        return;

    gf_win_info_t *win = &g_array_index (window_data, gf_win_info_t, selected);
    int current_workspace = win->workspace_id;

    gf_ipc_response_t ws_response = gf_run_client_command ("query workspaces");
    gf_ws_list_t *workspaces = gf_parse_workspace_list (ws_response.message);
    if (!workspaces)
        return;

    GtkStringList *new_target_ws_model = gtk_string_list_new (NULL);
    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        if (workspaces->items[i].id == current_workspace)
            continue;
        char ws_id_str[16];
        snprintf (ws_id_str, sizeof (ws_id_str), "%d", workspaces->items[i].id);
        gtk_string_list_append (new_target_ws_model, ws_id_str);
    }

    gtk_drop_down_set_model (GTK_DROP_DOWN (app->target_ws_dropdown),
                             G_LIST_MODEL (new_target_ws_model));
    app->target_ws_model = new_target_ws_model;
    gf_workspace_list_cleanup (workspaces);
}

static void
on_move_clicked (GtkButton *btn, gpointer data)
{
    gf_app_state_t *app = data;
    guint wsel = gtk_drop_down_get_selected (GTK_DROP_DOWN (app->window_dropdown));
    guint tsel = gtk_drop_down_get_selected (GTK_DROP_DOWN (app->target_ws_dropdown));
    if (wsel == GTK_INVALID_LIST_POSITION || tsel == GTK_INVALID_LIST_POSITION)
        return;

    GListModel *model = gtk_drop_down_get_model (GTK_DROP_DOWN (app->window_dropdown));
    GArray *window_data = g_object_get_data (G_OBJECT (model), "window-data");
    if (!window_data || wsel >= window_data->len)
        return;

    gf_win_info_t *win = &g_array_index (window_data, gf_win_info_t, wsel);
    GtkStringObject *target_item = GTK_STRING_OBJECT (
        gtk_drop_down_get_selected_item (GTK_DROP_DOWN (app->target_ws_dropdown)));
    if (!target_item)
        return;

    char cmd[64];
    snprintf (cmd, sizeof cmd, "move %p %s", (void *)win->id,
              gtk_string_object_get_string (target_item));
    platform_run_command (app, cmd, TRUE, TRUE);
}

static void
window_dropdown_setup (GtkListItemFactory *factory, GtkListItem *list_item, gpointer data)
{
    (void)factory;
    (void)data;
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *image = gtk_image_new ();
    GtkWidget *label = gtk_label_new ("");

    gtk_image_set_pixel_size (GTK_IMAGE (image), 16);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign (label, GTK_ALIGN_START);

    gtk_box_append (GTK_BOX (box), image);
    gtk_box_append (GTK_BOX (box), label);
    gtk_list_item_set_child (list_item, box);
}

static void
window_dropdown_bind (GtkListItemFactory *factory, GtkListItem *list_item, gpointer data)
{
    (void)factory;
    gf_app_state_t *app = data;
    GtkStringObject *item = GTK_STRING_OBJECT (gtk_list_item_get_item (list_item));
    GtkWidget *box = gtk_list_item_get_child (list_item);
    GtkWidget *image = gtk_widget_get_first_child (box);
    GtkWidget *label = gtk_widget_get_next_sibling (image);

    const char *title = gtk_string_object_get_string (item);
    gtk_label_set_text (GTK_LABEL (label), title);

    GListModel *model = gtk_drop_down_get_model (GTK_DROP_DOWN (app->window_dropdown));
    GArray *window_data = g_object_get_data (G_OBJECT (model), "window-data");
    if (!window_data)
        return;

    guint pos = gtk_list_item_get_position (list_item);
    if (pos >= window_data->len)
        return;

    gf_win_info_t *win = &g_array_index (window_data, gf_win_info_t, pos);

#ifdef _WIN32
    GdkPaintable *paintable = gf_get_hwnd_icon ((HWND)win->id);
    if (paintable)
    {
        gtk_image_set_from_paintable (GTK_IMAGE (image), paintable);
        g_object_unref (paintable);
    }
    else
    {
        gtk_image_set_from_icon_name (GTK_IMAGE (image), "application-x-executable");
    }
#else
    gtk_image_set_from_icon_name (GTK_IMAGE (image), "application-x-executable");
#endif
}

GtkWidget *
gf_gui_window_panel_new (gf_app_state_t *app)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);

    app->window_model = gtk_string_list_new (NULL);
    app->window_dropdown = gtk_drop_down_new (G_LIST_MODEL (app->window_model), NULL);

    // Set up custom factory for window list item (Icon + Label)
    GtkListItemFactory *win_factory = gtk_signal_list_item_factory_new ();
    g_signal_connect (win_factory, "setup", G_CALLBACK (window_dropdown_setup), app);
    g_signal_connect (win_factory, "bind", G_CALLBACK (window_dropdown_bind), app);
    gtk_drop_down_set_factory (GTK_DROP_DOWN (app->window_dropdown), win_factory);

    gtk_widget_set_size_request (app->window_dropdown, 200, -1);
    gtk_box_append (GTK_BOX (box), app->window_dropdown);
    g_signal_connect (app->window_dropdown, "notify::selected",
                      G_CALLBACK (on_window_dropdown_changed), app);

    app->target_ws_model = gtk_string_list_new (NULL);
    app->target_ws_dropdown
        = gtk_drop_down_new (G_LIST_MODEL (app->target_ws_model), NULL);
    gtk_widget_set_size_request (app->target_ws_dropdown, 140, -1);
    gtk_box_append (GTK_BOX (box), app->target_ws_dropdown);

    GtkWidget *move_btn = gtk_button_new_with_label ("Move Window");
    gtk_box_append (GTK_BOX (box), move_btn);
    g_signal_connect (move_btn, "clicked", G_CALLBACK (on_move_clicked), app);

    GtkWidget *rules_btn = gtk_button_new_with_label ("📋 Rules");
    gtk_box_append (GTK_BOX (box), rules_btn);
    g_signal_connect (rules_btn, "clicked", G_CALLBACK (on_rules_button_clicked), app);

    return box;
}
