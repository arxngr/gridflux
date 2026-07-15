#include "main_window.h"
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

// Resolve resource paths for dev mode (relative) and production (installed)
#if defined(_WIN32) || defined(GF_DEV_MODE)
#define GF_LOGO_PATH "icons/gf_logo.png"
#define GF_ICON_THEME_PATH "icons"
#else
#define GF_LOGO_PATH GF_DATADIR "/icons/gf_logo.png"
#define GF_ICON_THEME_PATH GF_ICONDIR
#endif

// Window opens at this fraction of the primary monitor (width x height).
#define GF_WIN_WIDTH_FRACTION 0.20
#define GF_WIN_HEIGHT_FRACTION 0.30
#define GF_WIN_FALLBACK_W 900
#define GF_WIN_FALLBACK_H 600

/* Teal-on-navy stylesheet — palette defined once, referenced everywhere. */
static const char k_app_css[]
    = "@define-color gf_top #08111f; @define-color gf_bottom #03070e;"
      "@define-color gf_acc #2ee6c9; @define-color gf_acc2 #5ff0dc;"
      "@define-color gf_text #dce6f2; @define-color gf_muted #8296ac;"
      "@define-color gf_dim #5f7387; @define-color gf_panel #0a1526;"
      /* Window + containers */
      "window.background { background: linear-gradient(180deg, @gf_top, @gf_bottom); }"
      "scrolledwindow, viewport, flowbox, .gf-wslist { background: transparent; }"
      /* Consistent type scale — 14px base, deliberate overrides below */
      "label { color: @gf_text; font-size: 14px; }"
      /* Header */
      "headerbar { background: alpha(@gf_acc,0.05); color: @gf_text; box-shadow: none;"
      " border-bottom: 1px solid alpha(@gf_acc,0.16); }"
      ".gf-brand { font-weight: bold; font-size: 15px; color: @gf_acc2; }"
      ".gf-server { border-radius: 999px; padding: 5px 13px; font-weight: bold;"
      " font-size: 14px; background: alpha(white,0.03); border: 1px solid "
      "alpha(white,0.09); }"
      ".gf-server.on { color: #8ff0c4; } .gf-server.off { color: @gf_muted; }"
      /* Flat header action buttons (Rules / Settings / Refresh) */
      ".gf-hbtn { background: transparent; box-shadow: none; }"
      ".gf-hbtn > button, button.gf-hbtn { background: alpha(white,0.03); min-height: 0;"
      " border: 1px solid alpha(white,0.09); border-radius: 8px; color: @gf_text;"
      " padding: 5px 11px; font-size: 14px; }"
      ".gf-hbtn > button:hover, button.gf-hbtn:hover {"
      " background: alpha(@gf_acc,0.10); border-color: alpha(@gf_acc,0.40); }"
      /* Workspace card */
      ".gf-wscard { background: alpha(white,0.03); border: 1px solid alpha(white,0.09);"
      " border-radius: 12px; padding: 12px 14px; }"
      ".gf-wscard:hover { border-color: alpha(@gf_acc,0.40); }"
      ".gf-wsnum { color: @gf_acc2; font-size: 26px; font-weight: bold; }"
      ".gf-wsnum-cap { color: @gf_dim; font-size: 10px; }"
      ".gf-wsname { color: @gf_text; font-size: 15px; font-weight: bold; }"
      /* Chips */
      ".gf-chip { background: alpha(white,0.05); border: 1px solid alpha(white,0.09);"
      " border-radius: 6px; padding: 2px 8px; color: #c3d2e2; }"
      ".gf-chip:hover { border-color: alpha(@gf_acc,0.50); background: "
      "alpha(@gf_acc,0.10); }"
      ".gf-chip label { font-size: 13px; }"
      ".gf-chip-more { background: transparent; border: 1px dashed alpha(@gf_acc,0.40);"
      " color: @gf_acc2; border-radius: 6px; padding: 2px 8px; min-height: 0; }"
      ".gf-chip-empty { color: @gf_dim; }"
      /* Tiling mini-map */
      ".gf-tile { min-width: 9px; min-height: 9px; border-radius: 2px;"
      " background: alpha(white,0.04); border: 1px solid alpha(white,0.14); }"
      ".gf-tile.on { background: alpha(@gf_acc,0.50); border: 1px solid "
      "alpha(@gf_acc,0.55); }"
      ".gf-tile.max { min-width: 30px; min-height: 24px;"
      " background: alpha(#f6be00,0.40); border: 1px solid alpha(#f6be00,0.50); }"
      /* Pills + lock */
      ".gf-pill { border-radius: 999px; padding: 3px 11px; font-size: 11px; font-weight: "
      "bold; }"
      ".gf-pill.tiled { background: alpha(@gf_acc,0.16); color: #9ff0e4; }"
      ".gf-pill.max { background: alpha(#f6be00,0.16); color: #f6cf5a; }"
      ".gf-lock { border-radius: 999px; padding: 2px 10px; font-size: 11px; font-weight: "
      "bold;"
      " min-height: 0; color: @gf_muted; background: alpha(white,0.04);"
      " border: 1px solid alpha(white,0.09); }"
      ".gf-lock:hover { border-color: alpha(@gf_acc,0.45); color: @gf_text; }"
      ".gf-lock.on { background: alpha(#ff8080,0.14); border-color: alpha(#ff8080,0.40);"
      " color: #ff9d9d; }"
      /* Popovers + forms */
      "popover > contents { background: @gf_panel; color: @gf_text;"
      " border: 1px solid alpha(@gf_acc,0.30); border-radius: 12px; }"
      ".gf-pop-title { font-weight: bold; font-size: 15px; }"
      ".gf-rule-group { color: @gf_acc2; font-size: 11px; font-weight: bold; }"
      ".gf-rule-remove { min-height: 0; min-width: 0; padding: 2px 8px; }"
      ".gf-rule-empty { color: @gf_dim; }"
      "dropdown > button, spinbutton, entry { background: alpha(white,0.05);"
      " border: 1px solid alpha(white,0.12); color: @gf_text; border-radius: 6px; }"
      /* Status bar */
      ".statusbar { padding: 4px 8px; border-top: 1px solid alpha(white,0.15); }"
      ".status-indicator { font-size: 8px; }"
      ".status-ready { color: #22c55e; } .status-not-ready { color: #ef4444; }"
      ".status-text-ready { color: #bbf7d0; font-weight: bold; }"
      ".status-text-not-ready { color: #fca5a5; font-weight: bold; }";

static void
on_window_realize (GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
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
        gdk_x11_surface_set_skip_taskbar_hint (GDK_X11_SURFACE (surface), FALSE);
        gdk_x11_surface_set_skip_pager_hint (GDK_X11_SURFACE (surface), FALSE);
    }
#endif
}

#ifdef _WIN32
static gboolean
on_close_request_hide (GtkWindow *window, gpointer user_data)
{
    (void)user_data;
    gtk_widget_set_visible (GTK_WIDGET (window), FALSE);
    return TRUE; // suppress default destroy
}
#endif

static void
apply_window_css (void)
{
    GtkCssProvider *provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_string (provider, k_app_css);
    gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                                GTK_STYLE_PROVIDER (provider),
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref (provider);
}

static void
compute_default_size (int *width, int *height)
{
    *width = GF_WIN_FALLBACK_W;
    *height = GF_WIN_FALLBACK_H;

    GdkDisplay *display = gdk_display_get_default ();
    if (!display)
        return;
    GListModel *monitors = gdk_display_get_monitors (display);
    GdkMonitor *monitor = monitors ? g_list_model_get_item (monitors, 0) : NULL;
    if (!monitor)
        return;

    GdkRectangle geo;
    gdk_monitor_get_geometry (monitor, &geo);
    if (geo.width > 0 && geo.height > 0)
    {
        *width = (int)(geo.width * GF_WIN_WIDTH_FRACTION);
        *height = (int)(geo.height * GF_WIN_HEIGHT_FRACTION);
    }
    g_object_unref (monitor);
}

static void
load_window_icon (GtkWidget *window)
{
    GError *error = NULL;
    GdkPixbuf *icon
        = gdk_pixbuf_new_from_file_at_scale (GF_LOGO_PATH, 256, 256, FALSE, &error);
    if (error)
    {
        g_clear_error (&error);
        icon = gdk_pixbuf_new_from_file_at_scale ("../" GF_LOGO_PATH, 256, 256, FALSE,
                                                  &error);
    }
    if (!error && icon)
        g_object_set_data_full (G_OBJECT (window), "window_icon", icon, g_object_unref);
    else if (error)
    {
        g_warning ("Failed to load logo: %s", error->message);
        g_error_free (error);
    }
}

static void
setup_main_window (gf_app_state_t *widgets, GtkApplication *app)
{
    widgets->window = gtk_application_window_new (app);
    gtk_window_set_title (GTK_WINDOW (widgets->window), "GridFlux");
    gtk_window_set_icon_name (GTK_WINDOW (widgets->window), "gridflux");

    int width, height;
    compute_default_size (&width, &height);
    gtk_window_set_default_size (GTK_WINDOW (widgets->window), width, height);

    GtkIconTheme *icon_theme
        = gtk_icon_theme_get_for_display (gdk_display_get_default ());
    gtk_icon_theme_add_search_path (icon_theme, GF_ICON_THEME_PATH);
    load_window_icon (widgets->window);

    g_signal_connect (widgets->window, "realize", G_CALLBACK (on_window_realize), NULL);
#ifdef _WIN32
    g_signal_connect (widgets->window, "close-request",
                      G_CALLBACK (on_close_request_hide), NULL);
#endif
}

static void
assemble_window_widgets (gf_app_state_t *widgets)
{
    gtk_window_set_titlebar (GTK_WINDOW (widgets->window), gf_gui_toolbar_new (widgets));

    GtkWidget *main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child (GTK_WINDOW (widgets->window), main_box);

    GtkWidget *table = gf_gui_workspace_panel_new (widgets);
    gtk_widget_set_vexpand (table, TRUE);
    gtk_box_append (GTK_BOX (main_box), table);

    GtkWidget *sb = gf_gui_statusbar_new ();
    g_object_set_data (G_OBJECT (widgets->window), "statusbar", sb);
    gtk_box_append (GTK_BOX (main_box), sb);
    gf_gui_statusbar_start_healthcheck (sb);
}

void
gf_gui_main_window_init (gf_app_state_t *widgets, GtkApplication *app)
{
    g_object_set (gtk_settings_get_default (), "gtk-application-prefer-dark-theme", TRUE,
                  NULL);
    apply_window_css ();
    setup_main_window (widgets, app);
    assemble_window_widgets (widgets);
    gtk_window_present (GTK_WINDOW (widgets->window));
}
