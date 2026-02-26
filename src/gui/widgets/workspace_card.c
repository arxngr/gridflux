#include "workspace_card.h"
#include <stdio.h>
#include <string.h>

#include <ctype.h>

static GdkPaintable *
get_app_icon_for_card (const char *wm_class)
{
    if (!wm_class || wm_class[0] == '\0')
        return NULL;

    GtkIconTheme *theme
        = gtk_icon_theme_get_for_display (gdk_display_get_default ());

    char lower[256];
    size_t i;
    for (i = 0; i < sizeof (lower) - 1 && wm_class[i] != '\0'; i++)
    {
        lower[i] = tolower ((unsigned char)wm_class[i]);
    }
    lower[i] = '\0';

    const char *tries[] = { lower, wm_class, "application-x-executable", NULL };

    for (int t = 0; tries[t]; t++)
    {
        if (gtk_icon_theme_has_icon (theme, tries[t]))
        {
            GtkIconPaintable *icon
                = gtk_icon_theme_lookup_icon (theme, tries[t], NULL, 16, 1,
                                              GTK_TEXT_DIR_NONE, 0);
            if (icon)
                return GDK_PAINTABLE (icon);
        }
    }

    GtkIconPaintable *fallback = gtk_icon_theme_lookup_icon (
        theme, "application-x-executable", NULL, 16, 1, GTK_TEXT_DIR_NONE, 0);
    return fallback ? GDK_PAINTABLE (fallback) : NULL;
}

void
gf_gui_workspace_card_add_to_grid (GtkGrid *grid, gf_ws_info_t *ws,
                                    const gf_win_list_t *windows, int row)
{
    char id_str[16];
    snprintf (id_str, sizeof (id_str), "%d", ws->id);
    GtkWidget *l1 = gtk_label_new (id_str);
    gtk_widget_add_css_class (l1, "table-cell");
    gtk_grid_attach (grid, l1, 0, row, 1, 1);

    char cnt_str[16];
    snprintf (cnt_str, sizeof (cnt_str), "%u", ws->window_count);
    GtkWidget *l2 = gtk_label_new (cnt_str);
    gtk_widget_add_css_class (l2, "table-cell");
    gtk_grid_attach (grid, l2, 1, row, 1, 1);

    // Build scrollable window class list for this workspace
    GtkWidget *scrolled = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (scrolled), TRUE);
    gtk_scrolled_window_set_max_content_height (GTK_SCROLLED_WINDOW (scrolled), 150);
    
    // Explicitly demand width from the grid
    gtk_widget_set_size_request (scrolled, 250, -1);
    gtk_widget_set_hexpand (scrolled, TRUE);

    GtkWidget *listbox = gtk_list_box_new ();
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (listbox), GTK_SELECTION_NONE);

    bool has_windows = false;
    if (windows)
    {
        for (uint32_t i = 0; i < windows->count; i++)
        {
            if (windows->items[i].workspace_id != ws->id)
                continue;
            if (!windows->items[i].is_valid)
                continue;

            const char *name = windows->items[i].name;
            if (!name || name[0] == '\0')
                continue;

            GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
            
            GdkPaintable *icon = get_app_icon_for_card (name);
            if (icon)
            {
                GtkWidget *img = gtk_image_new_from_paintable (icon);
                gtk_widget_set_size_request (img, 16, 16);
                gtk_box_append (GTK_BOX (box), img);
            }
            else
            {
                gtk_box_append (GTK_BOX (box), gtk_label_new ("📦"));
            }

            GtkWidget *lbl = gtk_label_new (name);
            gtk_widget_set_halign (lbl, GTK_ALIGN_START);
            gtk_widget_set_hexpand (lbl, TRUE);
            gtk_label_set_ellipsize (GTK_LABEL (lbl), PANGO_ELLIPSIZE_END);
            gtk_label_set_max_width_chars (GTK_LABEL (lbl), 55);
            gtk_box_append (GTK_BOX (box), lbl);

            gtk_list_box_append (GTK_LIST_BOX (listbox), box);
            has_windows = true;
        }
    }

    if (!has_windows)
    {
        GtkWidget *lbl = gtk_label_new ("-");
        gtk_widget_set_halign (lbl, GTK_ALIGN_START);
        gtk_list_box_append (GTK_LIST_BOX (listbox), lbl);
    }

    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), listbox);
    gtk_grid_attach (grid, scrolled, 2, row, 1, 1);

    GtkWidget *l3;
    if (ws->has_maximized_state)
    {
        l3 = gtk_label_new ("N/A");
    }
    else
    {
        char slot_str[16];
        snprintf (slot_str, sizeof (slot_str), "%d", ws->available_space);
        l3 = gtk_label_new (slot_str);
    }
    gtk_widget_add_css_class (l3, "table-cell");
    gtk_grid_attach (grid, l3, 3, row, 1, 1);

    GtkWidget *l4 = gtk_label_new (ws->has_maximized_state ? "Maximized" : "Tiled");
    gtk_widget_add_css_class (l4, "table-cell");
    gtk_grid_attach (grid, l4, 4, row, 1, 1);

    GtkWidget *l5 = gtk_label_new (ws->is_locked ? "🔒 Locked" : "🔓 Unlocked");
    gtk_widget_add_css_class (l5, "table-cell");
    gtk_grid_attach (grid, l5, 5, row, 1, 1);
}
