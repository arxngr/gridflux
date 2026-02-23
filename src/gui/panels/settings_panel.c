#include "settings_panel.h"

static void
on_config_save_clicked (GtkButton *btn, gpointer data)
{
    GtkWidget *config_window = g_object_get_data (G_OBJECT (btn), "config_window");
    const char *config_path = gf_config_get_path ();
    if (!config_path)
        return;

    gf_config_t config = load_or_create_config (config_path);

    GtkWidget *max_windows_spin
        = g_object_get_data (G_OBJECT (config_window), "max_windows_spin");
    GtkWidget *max_workspaces_spin
        = g_object_get_data (G_OBJECT (config_window), "max_workspaces_spin");
    GtkWidget *default_padding_spin
        = g_object_get_data (G_OBJECT (config_window), "default_padding_spin");
    GtkWidget *min_window_size_spin
        = g_object_get_data (G_OBJECT (config_window), "min_window_size_spin");
    GtkWidget *border_color_btn
        = g_object_get_data (G_OBJECT (config_window), "border_color_btn");

    config.max_windows_per_workspace
        = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (max_windows_spin));
    config.max_workspaces
        = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (max_workspaces_spin));
    config.default_padding
        = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (default_padding_spin));
    config.min_window_size
        = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (min_window_size_spin));

    GdkRGBA color;
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (border_color_btn), &color);
    uint32_t r = (uint32_t)(color.red * 255.0) & 0xFF;
    uint32_t g = (uint32_t)(color.green * 255.0) & 0xFF;
    uint32_t b = (uint32_t)(color.blue * 255.0) & 0xFF;
    config.border_color = (r << 16) | (g << 8) | b;

    gf_config_save (config_path, &config);

    GtkAlertDialog *dialog = gtk_alert_dialog_new ("Configuration saved successfully!");
    gtk_alert_dialog_show (dialog, GTK_WINDOW (config_window));
}

void
on_config_button_clicked (GtkButton *btn, gpointer data)
{
    gf_app_state_t *app = (gf_app_state_t *)data;
    GtkWidget *config_window = gtk_window_new ();
    gtk_window_set_title (GTK_WINDOW (config_window), "GridFlux Configuration");
    gtk_window_set_default_size (GTK_WINDOW (config_window), 400, 450);
    gtk_window_set_modal (GTK_WINDOW (config_window), TRUE);
    gtk_window_set_transient_for (GTK_WINDOW (config_window), GTK_WINDOW (app->window));

    GtkWidget *main = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start (main, 20);
    gtk_widget_set_margin_end (main, 20);
    gtk_widget_set_margin_top (main, 20);
    gtk_widget_set_margin_bottom (main, 20);
    gtk_window_set_child (GTK_WINDOW (config_window), main);

    const char *config_path = gf_config_get_path ();
    gf_config_t config = config_path ? load_or_create_config (config_path)
                                     : (gf_config_t){ 4, 10, 10, 100 };

    GtkWidget *form = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append (GTK_BOX (main), form);

    const char *labels[] = { "Max Windows per Workspace:", "Max Workspaces:",
                             "Default Padding:", "Min Window Size:" };
    int values[] = { config.max_windows_per_workspace, config.max_workspaces,
                     config.default_padding, config.min_window_size };
    int ranges[][3] = { { 1, 20, 1 }, { 1, 50, 1 }, { 0, 50, 1 }, { 50, 500, 10 } };
    const char *data_keys[] = { "max_windows_spin", "max_workspaces_spin",
                                "default_padding_spin", "min_window_size_spin" };

    for (int i = 0; i < 4; i++)
    {
        GtkWidget *l = gtk_label_new (labels[i]);
        gtk_widget_set_halign (l, GTK_ALIGN_START);
        gtk_box_append (GTK_BOX (form), l);
        GtkWidget *s
            = gtk_spin_button_new_with_range (ranges[i][0], ranges[i][1], ranges[i][2]);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (s), values[i]);
        g_object_set_data (G_OBJECT (config_window), data_keys[i], s);
        gtk_box_append (GTK_BOX (form), s);
    }

    GtkWidget *color_label = gtk_label_new ("Border Color:");
    gtk_widget_set_halign (color_label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (form), color_label);

    GdkRGBA rgba;
    rgba.red = ((config.border_color >> 16) & 0xFF) / 255.0;
    rgba.green = ((config.border_color >> 8) & 0xFF) / 255.0;
    rgba.blue = (config.border_color & 0xFF) / 255.0;
    rgba.alpha = 1.0;
    GtkWidget *color_btn = gtk_color_button_new_with_rgba (&rgba);
    gtk_widget_set_halign (color_btn, GTK_ALIGN_START);
    g_object_set_data (G_OBJECT (config_window), "border_color_btn", color_btn);
    gtk_box_append (GTK_BOX (form), color_btn);

    GtkWidget *bb = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (main), bb);

    GtkWidget *save = gtk_button_new_with_label ("Save");
    g_object_set_data (G_OBJECT (save), "config_window", config_window);
    g_signal_connect (save, "clicked", G_CALLBACK (on_config_save_clicked), NULL);
    gtk_box_append (GTK_BOX (bb), save);

    GtkWidget *close = gtk_button_new_with_label ("Close");
    g_signal_connect_swapped (close, "clicked", G_CALLBACK (gtk_window_destroy),
                              config_window);
    gtk_box_append (GTK_BOX (bb), close);

    gtk_window_present (GTK_WINDOW (config_window));
}
