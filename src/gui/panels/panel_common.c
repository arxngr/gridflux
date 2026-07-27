#include "panel_common.h"
#include <string.h>

void
gf_panel_clear_box (GtkWidget *box)
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
void
gf_panel_extract_wm_class (char *buf)
{
    char *open = strchr (buf, '[');
    char *close = strchr (buf, ']');
    if (open && close && close > open)
    {
        *close = '\0';
        memmove (buf, open + 1, strlen (open + 1) + 1);
    }
}

GdkPaintable *
gf_panel_app_icon (gf_app_state_t *app, const char *wm_class)
{
    if (!app || !app->platform || !app->platform->get_app_icon)
        return NULL;
    return app->platform->get_app_icon (app->platform, wm_class);
}

const char *
gf_panel_friendly_name (gf_app_state_t *app, const char *wm_class)
{
    if (!app || !app->platform || !app->platform->get_friendly_name)
        return NULL;
    return app->platform->get_friendly_name (app->platform, wm_class);
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
    GdkPaintable *icon_p = gf_panel_app_icon ((gf_app_state_t *)data, str);
    if (icon_p)
    {
        gtk_image_set_from_paintable (GTK_IMAGE (icon), icon_p);
        g_object_unref (icon_p);
    }
    else
        gtk_image_clear (GTK_IMAGE (icon));
}

GtkWidget *
gf_panel_build_app_dropdown (gf_app_state_t *app, GtkStringList **out_model)
{
    GtkStringList *model = gtk_string_list_new (NULL);
    if (app->platform && app->platform->populate_app_dropdown)
        app->platform->populate_app_dropdown (app->platform, model);

    GtkExpression *expr
        = gtk_property_expression_new (GTK_TYPE_STRING_OBJECT, NULL, "string");
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
    g_signal_connect (factory, "setup", G_CALLBACK (setup_app_item), NULL);
    g_signal_connect (factory, "bind", G_CALLBACK (bind_app_item), app);

    GtkWidget *dropdown = gtk_drop_down_new (G_LIST_MODEL (model), expr);
    gtk_drop_down_set_factory (GTK_DROP_DOWN (dropdown), factory);
    gtk_drop_down_set_list_factory (GTK_DROP_DOWN (dropdown), factory);
    gtk_drop_down_set_enable_search (GTK_DROP_DOWN (dropdown), TRUE);
    gtk_widget_set_hexpand (dropdown, TRUE);
    g_object_unref (factory);

    if (out_model)
        *out_model = model;
    return dropdown;
}
