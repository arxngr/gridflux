#include "settings_panel.h"

// Keys under which the form widgets are stashed on the dialog window, so the
// Save handler can read them back without a bespoke context struct.
#define K_MAX_WINDOWS "max_windows_spin"
#define K_MAX_WORKSPACES "max_workspaces_spin"
#define K_BORDER_COLOR "border_color_btn"
#define K_ENABLE_BORDERS "enable_borders_switch"

static GtkWidget *
add_spin_row (GtkWidget *form, GtkWidget *store, const char *label, const char *key,
              double lo, double hi, int value)
{
    GtkWidget *l = gtk_label_new (label);
    gtk_widget_set_halign (l, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (form), l);

    GtkWidget *spin = gtk_spin_button_new_with_range (lo, hi, 1);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin), value);
    g_object_set_data (G_OBJECT (store), key, spin);
    gtk_box_append (GTK_BOX (form), spin);
    return spin;
}

static void
apply_border_color (gf_config_t *config, GtkWidget *color_btn)
{
    const GdkRGBA *c
        = gtk_color_dialog_button_get_rgba (GTK_COLOR_DIALOG_BUTTON (color_btn));
    if (!c)
        return;
    uint32_t r = (uint32_t)(c->red * 255.0) & 0xFF;
    uint32_t g = (uint32_t)(c->green * 255.0) & 0xFF;
    uint32_t b = (uint32_t)(c->blue * 255.0) & 0xFF;
    config->border_color = (r << 16) | (g << 8) | b;
}

static void
on_settings_save (GtkButton *btn, gpointer user_data)
{
    (void)btn;
    GtkWidget *win = GTK_WIDGET (user_data);
    const char *path = gf_config_get_path ();
    if (!path)
        return;

    gf_config_t config = load_or_create_config (path);
    GtkWidget *max_win = g_object_get_data (G_OBJECT (win), K_MAX_WINDOWS);
    GtkWidget *max_ws = g_object_get_data (G_OBJECT (win), K_MAX_WORKSPACES);
    GtkWidget *borders = g_object_get_data (G_OBJECT (win), K_ENABLE_BORDERS);
    GtkWidget *color = g_object_get_data (G_OBJECT (win), K_BORDER_COLOR);

    config.max_windows_per_workspace
        = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (max_win));
    config.max_workspaces = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (max_ws));
    config.enable_borders = gtk_switch_get_active (GTK_SWITCH (borders));
    apply_border_color (&config, color);

    gf_config_save (path, &config);
    GtkAlertDialog *alert = gtk_alert_dialog_new ("Configuration saved");
    gtk_alert_dialog_show (alert, GTK_WINDOW (win));
}

static GtkWidget *
build_border_color_button (GtkWidget *store, const gf_config_t *config)
{
    GdkRGBA rgba = { ((config->border_color >> 16) & 0xFF) / 255.0,
                     ((config->border_color >> 8) & 0xFF) / 255.0,
                     (config->border_color & 0xFF) / 255.0, 1.0 };
    GtkColorDialog *dialog = gtk_color_dialog_new ();
    gtk_color_dialog_set_with_alpha (dialog, FALSE);

    // gtk_color_dialog_button_new takes ownership of the dialog; do not unref.
    GtkWidget *btn = gtk_color_dialog_button_new (dialog);
    gtk_color_dialog_button_set_rgba (GTK_COLOR_DIALOG_BUTTON (btn), &rgba);
    gtk_widget_set_halign (btn, GTK_ALIGN_START);
    gtk_widget_set_sensitive (btn, config->enable_borders);
    g_object_set_data (G_OBJECT (store), K_BORDER_COLOR, btn);
    return btn;
}

static GtkWidget *
build_borders_row (GtkWidget *store, GtkWidget *color_btn, bool enabled)
{
    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *label = gtk_label_new ("Enable borders");
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_widget_set_hexpand (label, TRUE);
    gtk_box_append (GTK_BOX (row), label);

    GtkWidget *sw = gtk_switch_new ();
    gtk_switch_set_active (GTK_SWITCH (sw), enabled);
    gtk_widget_set_halign (sw, GTK_ALIGN_END);
    g_object_set_data (G_OBJECT (store), K_ENABLE_BORDERS, sw);
    g_object_bind_property (sw, "active", color_btn, "sensitive", G_BINDING_SYNC_CREATE);
    gtk_box_append (GTK_BOX (row), sw);
    return row;
}

static GtkWidget *
build_button_bar (GtkWidget *window)
{
    GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign (bar, GTK_ALIGN_END);

    GtkWidget *save = gtk_button_new_with_label ("Save");
    gtk_widget_add_css_class (save, "suggested-action");
    g_signal_connect (save, "clicked", G_CALLBACK (on_settings_save), window);
    gtk_box_append (GTK_BOX (bar), save);

    GtkWidget *close = gtk_button_new_with_label ("Close");
    g_signal_connect_swapped (close, "clicked", G_CALLBACK (gtk_window_destroy), window);
    gtk_box_append (GTK_BOX (bar), close);
    return bar;
}

void
on_config_button_clicked (GtkButton *btn, gpointer data)
{
    (void)btn;
    gf_app_state_t *app = (gf_app_state_t *)data;
    const char *path = gf_config_get_path ();
    gf_config_t config = path ? load_or_create_config (path)
                              : (gf_config_t){ .max_windows_per_workspace = 4,
                                               .max_workspaces = 10,
                                               .min_window_size = 10,
                                               .border_color = 100 };

    GtkWidget *window = gtk_window_new ();
    gtk_window_set_title (GTK_WINDOW (window), "Settings");
    gtk_window_set_default_size (GTK_WINDOW (window), 320, 340);
    gtk_window_set_modal (GTK_WINDOW (window), TRUE);
    gtk_window_set_transient_for (GTK_WINDOW (window), GTK_WINDOW (app->window));

    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start (box, 20);
    gtk_widget_set_margin_end (box, 20);
    gtk_widget_set_margin_top (box, 20);
    gtk_widget_set_margin_bottom (box, 20);
    gtk_window_set_child (GTK_WINDOW (window), box);

    GtkWidget *title = gtk_label_new ("Settings");
    gtk_widget_add_css_class (title, "gf-pop-title");
    gtk_widget_set_halign (title, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (box), title);

    GtkWidget *form = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_vexpand (form, TRUE);
    add_spin_row (form, window, "Max windows / workspace", K_MAX_WINDOWS, 1, 20,
                  config.max_windows_per_workspace);
    add_spin_row (form, window, "Max workspaces", K_MAX_WORKSPACES, 1, 50,
                  config.max_workspaces);

    GtkWidget *color_label = gtk_label_new ("Border color");
    gtk_widget_set_halign (color_label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (form), color_label);
    GtkWidget *color_btn = build_border_color_button (window, &config);
    gtk_box_append (GTK_BOX (form), color_btn);
    gtk_box_append (GTK_BOX (form),
                    build_borders_row (window, color_btn, config.enable_borders));
    gtk_box_append (GTK_BOX (box), form);

    gtk_box_append (GTK_BOX (box), build_button_bar (window));
    gtk_window_present (GTK_WINDOW (window));
}
