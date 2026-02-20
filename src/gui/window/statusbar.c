#include "statusbar.h"

GtkWidget *
gf_gui_statusbar_new (void)
{
    GtkWidget *label = gtk_label_new ("Ready");
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_widget_set_margin_start (label, 4);
    gtk_widget_set_margin_end (label, 4);
    gtk_widget_set_margin_top (label, 4);
    gtk_widget_set_margin_bottom (label, 4);
    return label;
}

void
gf_gui_statusbar_set_text (GtkWidget *statusbar, const char *text)
{
    gtk_label_set_text (GTK_LABEL (statusbar), text);
}
