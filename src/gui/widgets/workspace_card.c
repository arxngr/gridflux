#include "workspace_card.h"
#include <stdio.h>

void
gf_gui_workspace_card_add_to_grid (GtkGrid *grid, gf_ws_info_t *ws, int row)
{
    char id_str[16]; snprintf (id_str, sizeof(id_str), "%d", ws->id);
    GtkWidget *l1 = gtk_label_new (id_str);
    gtk_widget_add_css_class (l1, "table-cell");
    gtk_grid_attach (grid, l1, 0, row, 1, 1);

    char cnt_str[16]; snprintf (cnt_str, sizeof(cnt_str), "%u", ws->window_count);
    GtkWidget *l2 = gtk_label_new (cnt_str);
    gtk_widget_add_css_class (l2, "table-cell");
    gtk_grid_attach (grid, l2, 1, row, 1, 1);

    GtkWidget *l3;
    if (ws->has_maximized_state) {
        l3 = gtk_label_new ("N/A");
    } else {
        char slot_str[16]; snprintf (slot_str, sizeof(slot_str), "%d", ws->available_space);
        l3 = gtk_label_new (slot_str);
    }
    gtk_widget_add_css_class (l3, "table-cell");
    gtk_grid_attach (grid, l3, 2, row, 1, 1);

    GtkWidget *l4 = gtk_label_new (ws->has_maximized_state ? "Maximized" : "Tiled");
    gtk_widget_add_css_class (l4, "table-cell");
    gtk_grid_attach (grid, l4, 3, row, 1, 1);

    GtkWidget *l5 = gtk_label_new (ws->is_locked ? "🔒 Locked" : "🔓 Unlocked");
    gtk_widget_add_css_class (l5, "table-cell");
    gtk_grid_attach (grid, l5, 4, row, 1, 1);
}
