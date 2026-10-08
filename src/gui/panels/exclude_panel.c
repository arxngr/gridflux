#include "exclude_panel.h"
#include "../../config/excludes.h"
#include "../bridge/ipc_client.h"
#include "panel_common.h"
#include <gtk/gtk.h>
#include <stdio.h>

typedef struct
{
    gf_app_state_t *app;
    GtkWidget *window;
    GtkWidget *app_dropdown;
    GtkStringList *app_model;
    GtkWidget *list_box;
} exclude_ctx_t;

static void refresh_exclude_list (exclude_ctx_t *ctx);

static void
on_remove_exclude (GtkButton *btn, gpointer user_data)
{
    exclude_ctx_t *ctx = user_data;
    const char *wm_class = g_object_get_data (G_OBJECT (btn), "wm_class");
    char command[288];
    snprintf (command, sizeof (command), "exclude remove %s", wm_class);
    gf_run_client_command (command);
    refresh_exclude_list (ctx);
}

static void
on_add_exclude (GtkButton *btn, gpointer user_data)
{
    (void)btn;
    exclude_ctx_t *ctx = user_data;
    GtkStringObject *item = GTK_STRING_OBJECT (
        gtk_drop_down_get_selected_item (GTK_DROP_DOWN (ctx->app_dropdown)));
    if (!item)
        return;

    char wm_class[256];
    g_strlcpy (wm_class, gtk_string_object_get_string (item), sizeof (wm_class));
    gf_panel_extract_wm_class (wm_class);

    char command[288];
    snprintf (command, sizeof (command), "exclude add %s", wm_class);
    gf_ipc_response_t resp = gf_run_client_command (command);
    gf_command_response_t result = { .type = 1, .message = "Invalid IPC reply" };
    gf_parse_command_response (resp.message, sizeof (resp.message), &result);
    gf_command_response_t *cmd_resp = &result;

    if (resp.status == GF_IPC_SUCCESS && cmd_resp->type == 0)
        refresh_exclude_list (ctx);
    else
    {
        GtkAlertDialog *dialog = gtk_alert_dialog_new ("%s", cmd_resp->message);
        gtk_alert_dialog_show (dialog, GTK_WINDOW (ctx->window));
    }
}

static GtkWidget *
build_exclude_row (exclude_ctx_t *ctx, const char *wm_class)
{
    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class (row, "gf-rule-row");

    GdkPaintable *icon = gf_panel_app_icon (ctx->app, wm_class);
    if (icon)
    {
        GtkWidget *img = gtk_image_new_from_paintable (icon);
        gtk_box_append (GTK_BOX (row), img);
        g_object_unref (icon);
    }

    const char *friendly = gf_panel_friendly_name (ctx->app, wm_class);
    GtkWidget *label = gtk_label_new (friendly ? friendly : wm_class);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_widget_set_hexpand (label, TRUE);
    gtk_box_append (GTK_BOX (row), label);

    GtkWidget *remove = gtk_button_new_with_label ("✕");
    gtk_widget_add_css_class (remove, "gf-rule-remove");
    g_object_set_data_full (G_OBJECT (remove), "wm_class", g_strdup (wm_class), g_free);
    g_signal_connect (remove, "clicked", G_CALLBACK (on_remove_exclude), ctx);
    gtk_box_append (GTK_BOX (row), remove);
    return row;
}

static void
refresh_exclude_list (exclude_ctx_t *ctx)
{
    gf_panel_clear_box (ctx->list_box);
    const char *path = gf_config_get_path ();
    if (!path)
        return;

    gf_config_t config = gf_config_load_or_create (path);
    const gf_exclude_list_t *list = &config.excluded_apps;

    if (list->count == 0)
    {
        GtkWidget *empty = gtk_label_new ("No apps excluded yet.");
        gtk_widget_add_css_class (empty, "gf-rule-empty");
        gtk_box_append (GTK_BOX (ctx->list_box), empty);
    }
    else
    {
        for (uint32_t i = 0; i < list->count; i++)
            gtk_box_append (GTK_BOX (ctx->list_box),
                            build_exclude_row (ctx, list->items[i].wm_class));
    }

    gf_config_release (&config);
}

static GtkWidget *
build_add_form (exclude_ctx_t *ctx)
{
    GtkWidget *form = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    ctx->app_dropdown = gf_panel_build_app_dropdown (ctx->app, &ctx->app_model);
    gtk_box_append (GTK_BOX (form), ctx->app_dropdown);

    GtkWidget *add = gtk_button_new_with_label ("Exclude");
    gtk_widget_add_css_class (add, "suggested-action");
    g_signal_connect (add, "clicked", G_CALLBACK (on_add_exclude), ctx);
    gtk_box_append (GTK_BOX (form), add);
    return form;
}

void
gf_gui_on_exclude_button_clicked (GtkButton *btn, gpointer data)
{
    (void)btn;
    gf_app_state_t *app = (gf_app_state_t *)data;
    exclude_ctx_t *ctx = g_new0 (exclude_ctx_t, 1);
    ctx->app = app;

    GtkWidget *window = gtk_window_new ();
    gtk_window_set_title (GTK_WINDOW (window), "Excluded Apps");
    gtk_window_set_default_size (GTK_WINDOW (window), 380, 460);
    gtk_window_set_modal (GTK_WINDOW (window), TRUE);
    gtk_window_set_transient_for (GTK_WINDOW (window), GTK_WINDOW (app->window));
    g_object_set_data_full (G_OBJECT (window), "ctx", ctx, g_free);
    ctx->window = window;

    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start (box, 16);
    gtk_widget_set_margin_end (box, 16);
    gtk_widget_set_margin_top (box, 16);
    gtk_widget_set_margin_bottom (box, 16);
    gtk_window_set_child (GTK_WINDOW (window), box);

    GtkWidget *title = gtk_label_new ("Apps excluded from arrangement");
    gtk_widget_add_css_class (title, "gf-pop-title");
    gtk_widget_set_halign (title, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (box), title);
    gtk_box_append (GTK_BOX (box), build_add_form (ctx));
    gtk_box_append (GTK_BOX (box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));

    GtkWidget *scrolled = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand (scrolled, TRUE);
    gtk_box_append (GTK_BOX (box), scrolled);

    ctx->list_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), ctx->list_box);

    GtkWidget *close = gtk_button_new_with_label ("Close");
    gtk_widget_set_halign (close, GTK_ALIGN_END);
    g_signal_connect_swapped (close, "clicked", G_CALLBACK (gtk_window_destroy), window);
    gtk_box_append (GTK_BOX (box), close);

    refresh_exclude_list (ctx);
    gtk_window_present (GTK_WINDOW (window));
}
