#include "rules_panel.h"
#include "../../config/rules.h"
#include "../bridge/ipc_client.h"
#include "../platform/gui_platform.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    gf_app_state_t *app;
    GtkWidget *window;
    GtkWidget *app_dropdown;
    GtkStringList *app_model;
    GtkWidget *ws_spin;
    GtkWidget *list_box; // vertical box holding the grouped rules
} rules_ctx_t;

static void refresh_rules_list (rules_ctx_t *ctx);

static GdkPaintable *
rule_app_icon (gf_app_state_t *app, const char *wm_class)
{
    if (!app || !app->platform || !app->platform->get_app_icon)
        return NULL;
    return app->platform->get_app_icon (app->platform, wm_class);
}

static void
clear_box (GtkWidget *box)
{
    GtkWidget *child = gtk_widget_get_first_child (box);
    while (child)
    {
        GtkWidget *next = gtk_widget_get_next_sibling (child);
        gtk_box_remove (GTK_BOX (box), child);
        child = next;
    }
}

// Strip an "app-id [wm_class]" dropdown entry down to just the wm_class.
static void
extract_wm_class (char *buf)
{
    char *open = strchr (buf, '[');
    char *close = strchr (buf, ']');
    if (open && close && close > open)
    {
        *close = '\0';
        memmove (buf, open + 1, strlen (open + 1) + 1);
    }
}

static void
on_remove_rule (GtkButton *btn, gpointer user_data)
{
    rules_ctx_t *ctx = user_data;
    const char *wm_class = g_object_get_data (G_OBJECT (btn), "wm_class");
    char command[256];
    snprintf (command, sizeof (command), "rule remove %s", wm_class);
    gf_run_client_command (command);
    refresh_rules_list (ctx);
}

static void
on_add_rule (GtkButton *btn, gpointer user_data)
{
    (void)btn;
    rules_ctx_t *ctx = user_data;
    GtkStringObject *item = GTK_STRING_OBJECT (
        gtk_drop_down_get_selected_item (GTK_DROP_DOWN (ctx->app_dropdown)));
    if (!item)
        return;

    char wm_class[256];
    g_strlcpy (wm_class, gtk_string_object_get_string (item), sizeof (wm_class));
    extract_wm_class (wm_class);
    int ws = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (ctx->ws_spin));

    char command[256];
    snprintf (command, sizeof (command), "rule add %s %d", wm_class, ws);
    gf_ipc_response_t resp = gf_run_client_command (command);
    gf_command_response_t *cmd_resp = (gf_command_response_t *)resp.message;

    if (resp.status == GF_IPC_SUCCESS && cmd_resp->type == 0)
        refresh_rules_list (ctx);
    else
    {
        GtkAlertDialog *dialog = gtk_alert_dialog_new ("%s", cmd_resp->message);
        gtk_alert_dialog_show (dialog, GTK_WINDOW (ctx->window));
    }
}

static bool
rule_less (const gf_config_t *cfg, int a, int b)
{
    const gf_window_rule_t *ra = &cfg->window_rules[a];
    const gf_window_rule_t *rb = &cfg->window_rules[b];
    if (ra->workspace_id != rb->workspace_id)
        return ra->workspace_id < rb->workspace_id;
    return strcmp (ra->wm_class, rb->wm_class) < 0;
}

// Insertion sort of rule indices by (workspace, class) — n <= GF_MAX_RULES.
static void
sort_rule_order (const gf_config_t *cfg, int *order, uint32_t n)
{
    for (uint32_t i = 1; i < n; i++)
    {
        int key = order[i];
        uint32_t j = i;
        while (j > 0 && rule_less (cfg, key, order[j - 1]))
        {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = key;
    }
}

static GtkWidget *
build_group_header (gf_ws_id_t ws)
{
    char buf[32];
    snprintf (buf, sizeof (buf), "Workspace %d", ws);
    GtkWidget *label = gtk_label_new (buf);
    gtk_widget_add_css_class (label, "gf-rule-group");
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    return label;
}

static GtkWidget *
build_rule_row (rules_ctx_t *ctx, const gf_window_rule_t *rule)
{
    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class (row, "gf-rule-row");

    GdkPaintable *icon = rule_app_icon (ctx->app, rule->wm_class);
    if (icon)
    {
        GtkWidget *img = gtk_image_new_from_paintable (icon);
        gtk_box_append (GTK_BOX (row), img);
        g_object_unref (icon);
    }

    const char *friendly = NULL;
    if (ctx->app->platform && ctx->app->platform->get_friendly_name)
        friendly
            = ctx->app->platform->get_friendly_name (ctx->app->platform, rule->wm_class);
    GtkWidget *label = gtk_label_new (friendly ? friendly : rule->wm_class);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_widget_set_hexpand (label, TRUE);
    gtk_box_append (GTK_BOX (row), label);

    GtkWidget *remove = gtk_button_new_with_label ("✕");
    gtk_widget_add_css_class (remove, "gf-rule-remove");
    g_object_set_data_full (G_OBJECT (remove), "wm_class", g_strdup (rule->wm_class),
                            g_free);
    g_signal_connect (remove, "clicked", G_CALLBACK (on_remove_rule), ctx);
    gtk_box_append (GTK_BOX (row), remove);
    return row;
}

static void
refresh_rules_list (rules_ctx_t *ctx)
{
    clear_box (ctx->list_box);
    const char *path = gf_config_get_path ();
    if (!path)
        return;

    gf_config_t config = load_or_create_config (path);
    uint32_t n = config.window_rules_count;
    if (n == 0)
    {
        GtkWidget *empty = gtk_label_new ("No rules configured yet.");
        gtk_widget_add_css_class (empty, "gf-rule-empty");
        gtk_box_append (GTK_BOX (ctx->list_box), empty);
        return;
    }

    int order[GF_MAX_RULES];
    for (uint32_t i = 0; i < n; i++)
        order[i] = (int)i;
    sort_rule_order (&config, order, n);

    gf_ws_id_t current_ws = -1;
    for (uint32_t i = 0; i < n; i++)
    {
        const gf_window_rule_t *rule = &config.window_rules[order[i]];
        if (rule->workspace_id != current_ws)
        {
            current_ws = rule->workspace_id;
            gtk_box_append (GTK_BOX (ctx->list_box), build_group_header (current_ws));
        }
        gtk_box_append (GTK_BOX (ctx->list_box), build_rule_row (ctx, rule));
    }
}

static void
setup_app_item (GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data)
{
    (void)factory;
    (void)data;
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (box), gtk_image_new ());
    gtk_box_append (GTK_BOX (box), gtk_label_new (""));
    gtk_list_item_set_child (item, box);
}

static void
bind_app_item (GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data)
{
    (void)factory;
    GtkWidget *box = gtk_list_item_get_child (item);
    if (!box)
        return;
    GtkWidget *icon = gtk_widget_get_first_child (box);
    GtkWidget *label = gtk_widget_get_next_sibling (icon);
    gpointer obj = gtk_list_item_get_item (item);
    if (!obj || !GTK_IS_STRING_OBJECT (obj))
        return;

    const char *str = gtk_string_object_get_string (GTK_STRING_OBJECT (obj));
    gtk_label_set_text (GTK_LABEL (label), str);
    GdkPaintable *icon_p = rule_app_icon ((gf_app_state_t *)data, str);
    if (icon_p)
    {
        gtk_image_set_from_paintable (GTK_IMAGE (icon), icon_p);
        g_object_unref (icon_p);
    }
    else
        gtk_image_clear (GTK_IMAGE (icon));
}

static void
build_app_dropdown (rules_ctx_t *ctx)
{
    ctx->app_model = gtk_string_list_new (NULL);
    if (ctx->app->platform && ctx->app->platform->populate_app_dropdown)
        ctx->app->platform->populate_app_dropdown (ctx->app->platform, ctx->app_model);

    GtkExpression *expr
        = gtk_property_expression_new (GTK_TYPE_STRING_OBJECT, NULL, "string");
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
    g_signal_connect (factory, "setup", G_CALLBACK (setup_app_item), NULL);
    g_signal_connect (factory, "bind", G_CALLBACK (bind_app_item), ctx->app);

    ctx->app_dropdown = gtk_drop_down_new (G_LIST_MODEL (ctx->app_model), expr);
    gtk_drop_down_set_factory (GTK_DROP_DOWN (ctx->app_dropdown), factory);
    gtk_drop_down_set_list_factory (GTK_DROP_DOWN (ctx->app_dropdown), factory);
    gtk_drop_down_set_enable_search (GTK_DROP_DOWN (ctx->app_dropdown), TRUE);
    gtk_widget_set_hexpand (ctx->app_dropdown, TRUE);
    g_object_unref (factory);
}

static GtkWidget *
build_add_form (rules_ctx_t *ctx)
{
    GtkWidget *form = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    build_app_dropdown (ctx);
    gtk_box_append (GTK_BOX (form), ctx->app_dropdown);

    ctx->ws_spin = gtk_spin_button_new_with_range (1, GF_MAX_WORKSPACES, 1);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (ctx->ws_spin), 1);
    gtk_box_append (GTK_BOX (form), ctx->ws_spin);

    GtkWidget *add = gtk_button_new_with_label ("Add");
    gtk_widget_add_css_class (add, "suggested-action");
    g_signal_connect (add, "clicked", G_CALLBACK (on_add_rule), ctx);
    gtk_box_append (GTK_BOX (form), add);
    return form;
}

void
on_rules_button_clicked (GtkButton *btn, gpointer data)
{
    (void)btn;
    gf_app_state_t *app = (gf_app_state_t *)data;
    rules_ctx_t *ctx = g_new0 (rules_ctx_t, 1);
    ctx->app = app;

    GtkWidget *window = gtk_window_new ();
    gtk_window_set_title (GTK_WINDOW (window), "Window Rules");
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

    GtkWidget *title = gtk_label_new ("Window rules");
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

    refresh_rules_list (ctx);
    gtk_window_present (GTK_WINDOW (window));
}
