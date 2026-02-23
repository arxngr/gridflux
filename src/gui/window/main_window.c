#include "main_window.h"
#include "../panels/window_panel.h"
#include "../panels/workspace_panel.h"
#include "statusbar.h"
#include "toolbar.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

#ifdef __linux__
#include <gdk/x11/gdkx.h>
#endif
#ifdef _WIN32
#include <gdk/gdk.h>
#include <gdk/win32/gdkwin32.h>
#define IDI_ICON1 101
#endif

/* Resolve resource paths for dev mode (relative) and production (installed) */
#ifdef GF_DEV_MODE
#define GF_LOGO_PATH "icons/gf_logo.png"
#define GF_ICON_THEME_PATH "icons"
#else
#define GF_LOGO_PATH GF_DATADIR "/icons/gf_logo.png"
#define GF_ICON_THEME_PATH GF_ICONDIR
#endif

static void
on_window_realize (GtkWidget *widget, gpointer user_data)
{
    GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (widget));
    if (!surface)
        return;

#ifdef _WIN32
    HWND hwnd = GDK_SURFACE_HWND (surface);
    if (hwnd)
    {
        HICON hIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_ICON1));
        if (hIcon)
        {
            SendMessage (hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            SendMessage (hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        }
    }
#endif

#ifdef __linux__
    if (GDK_IS_X11_SURFACE (surface))
    {
        gdk_x11_surface_set_skip_taskbar_hint (GDK_X11_SURFACE (surface), TRUE);
        gdk_x11_surface_set_skip_pager_hint (GDK_X11_SURFACE (surface), TRUE);
    }
#endif
}

void
gf_gui_main_window_init (gf_app_state_t *widgets, GtkApplication *app)
{
    // CSS Provider
    GtkCssProvider *provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_string (
        provider,
        /* Existing table styles */
        ".table-cell { border: 1px solid @borders; padding: 4px; background-color: "
        "@theme_bg_color; color: @theme_fg_color; }"
        ".table-header { border: 1px solid @borders; padding: 4px; background-color: "
        "@theme_base_color; color: @theme_fg_color; font-weight: bold; }"

        /* Gradient background (cyan to blue) */
        "window.background { background: linear-gradient(180deg, #032045, #020912); }"

        /* Statusbar styles */
        ".statusbar { padding: 4px 8px; border-top: 1px solid rgba(255,255,255,0.15); }"
        ".status-indicator { font-size: 8px; }"
        ".status-ready { color: #22c55e; }"
        ".status-not-ready { color: #ef4444; }"
        ".status-text-ready { color: #bbf7d0; font-weight: bold; }"
        ".status-text-not-ready { color: #fca5a5; font-weight: bold; }"

        /* Make text visible on gradient */
        "label { color: #f0f9ff; }"
        "button { }"
        "dropdownbutton { }");
    gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                                GTK_STYLE_PROVIDER (provider),
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref (provider);

    widgets->window = gtk_application_window_new (app);
    gtk_window_set_title (GTK_WINDOW (widgets->window), "GridFlux");
    gtk_window_set_default_size (GTK_WINDOW (widgets->window), 700, 500);
    gtk_window_set_icon_name (GTK_WINDOW (widgets->window), "gridflux");

    /* Add icon theme search path so gtk_window_set_icon_name("gridflux")
       resolves to our icons for alt+tab, taskbar, etc.
       Dev mode:  uses local "icons/" directory (hicolor structure)
       Production: uses system icon dir (e.g. /usr/local/share/icons) */
    GtkIconTheme *icon_theme
        = gtk_icon_theme_get_for_display (gdk_display_get_default ());
    gtk_icon_theme_add_search_path (icon_theme, GF_ICON_THEME_PATH);

    GError *error = NULL;
    GdkPixbuf *icon
        = gdk_pixbuf_new_from_file_at_scale (GF_LOGO_PATH, 256, 256, FALSE, &error);
    if (!error)
        g_object_set_data_full (G_OBJECT (widgets->window), "window_icon", icon,
                                g_object_unref);
    else
    {
        g_warning ("Failed to load logo from %s: %s", GF_LOGO_PATH, error->message);
        g_error_free (error);
    }

    g_signal_connect (widgets->window, "realize", G_CALLBACK (on_window_realize), NULL);

    GtkWidget *main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_window_set_child (GTK_WINDOW (widgets->window), main_box);

    gtk_box_append (GTK_BOX (main_box), gf_gui_toolbar_new (widgets));
    gtk_box_append (GTK_BOX (main_box), gf_gui_window_panel_new (widgets));
    gtk_box_append (GTK_BOX (main_box), gf_gui_workspace_panel_new (widgets));

    GtkWidget *sb = gf_gui_statusbar_new ();
    g_object_set_data (G_OBJECT (widgets->window), "statusbar", sb);
    gtk_box_append (GTK_BOX (main_box), sb);
    gf_gui_statusbar_start_healthcheck (sb);

    gtk_widget_grab_focus (widgets->ws_dropdown);
    gtk_window_present (GTK_WINDOW (widgets->window));
}
