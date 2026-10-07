#ifdef _WIN32

#include "tray.h"
#include "../bridge/process_manager.h"
#include "main_window.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on

#include <gdk/win32/gdkwin32.h>

#define WM_TRAY_ICON (WM_USER + 1)
#define IDI_ICON1 101

#define ID_TRAY_START 4001
#define ID_TRAY_STOP 4002
#define ID_TRAY_SHOW 4003
#define ID_TRAY_QUIT 4004

typedef struct
{
    NOTIFYICONDATAW nid;
    HWND msg_hwnd;
    gf_app_state_t *app;
    guint pump_timer;
    guint show_source;
    gboolean server_running;
    UINT taskbar_created;
    gboolean icon_added;
} gf_tray_data_t;

static gf_tray_data_t *g_tray = NULL;

static void
tray_show_context_menu (gf_tray_data_t *tray)
{
    HMENU menu = CreatePopupMenu ();
    if (!menu)
        return;

    gboolean running = gf_server_is_running ();

    if (running)
    {
        AppendMenuW (menu, MF_STRING, ID_TRAY_STOP, L"\x23F9 Stop");
    }
    else
    {
        AppendMenuW (menu, MF_STRING, ID_TRAY_START, L"\x25B6 Start");
    }

    AppendMenuW (menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW (menu, MF_STRING, ID_TRAY_SHOW, L"Show");
    AppendMenuW (menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW (menu, MF_STRING, ID_TRAY_QUIT, L"Quit");

    // required for popup to dismiss properly
    SetForegroundWindow (tray->msg_hwnd);

    POINT pt;
    GetCursorPos (&pt);
    TrackPopupMenu (menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, tray->msg_hwnd,
                    NULL);

    PostMessage (tray->msg_hwnd, WM_NULL, 0, 0);
    DestroyMenu (menu);
}

static gboolean
tray_present_window (gpointer user_data)
{
    gf_tray_data_t *tray = user_data;
    tray->show_source = 0;
    gf_gui_main_window_present (tray->app);
    return G_SOURCE_REMOVE;
}

static void
tray_request_show (gf_tray_data_t *tray)
{
    if (tray && !tray->show_source)
        tray->show_source = g_idle_add (tray_present_window, tray);
}

static void
tray_update_tooltip (gf_tray_data_t *tray)
{
    gboolean running = gf_server_is_running ();
    tray->server_running = running;

    if (running)
        wcscpy (tray->nid.szTip, L"GridFlux \u2014 Running");
    else
        wcscpy (tray->nid.szTip, L"GridFlux \u2014 Stopped");

    Shell_NotifyIconW (NIM_MODIFY, &tray->nid);
}

static LRESULT CALLBACK
tray_wndproc (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_tray && g_tray->taskbar_created && msg == g_tray->taskbar_created)
    {
        g_tray->icon_added = Shell_NotifyIconW (NIM_ADD, &g_tray->nid);
        return 0;
    }
    if (g_tray && msg == GF_TRAY_SHOW_MESSAGE)
    {
        tray_request_show (g_tray);
        return 0;
    }
    if (msg == WM_TRAY_ICON)
    {
        switch (LOWORD (lParam))
        {
        case WM_LBUTTONUP:
            tray_request_show (g_tray);
            break;
        case WM_RBUTTONUP:
            tray_show_context_menu (g_tray);
            break;
        }
        return 0;
    }

    if (msg == WM_COMMAND)
    {
        switch (LOWORD (wParam))
        {
        case ID_TRAY_START:
            gf_server_start ();
            tray_update_tooltip (g_tray);
            break;
        case ID_TRAY_STOP:
            gf_server_stop ();
            tray_update_tooltip (g_tray);
            break;
        case ID_TRAY_SHOW:
            tray_request_show (g_tray);
            break;
        case ID_TRAY_QUIT:
            gf_server_stop ();
            if (g_tray && g_tray->app && g_tray->app->window)
            {
                GtkApplication *gtk_app
                    = gtk_window_get_application (GTK_WINDOW (g_tray->app->window));
                if (gtk_app)
                    g_application_quit (G_APPLICATION (gtk_app));
            }
            break;
        }
        return 0;
    }

    return DefWindowProcW (hwnd, msg, wParam, lParam);
}

static gboolean
tray_pump_messages (gpointer user_data)
{
    gf_tray_data_t *tray = user_data;
    MSG msg;
    for (int count = 0; count < 32 && PeekMessage (&msg, tray->msg_hwnd, 0, 0, PM_REMOVE);
         count++)
    {
        TranslateMessage (&msg);
        DispatchMessage (&msg);
    }

    // periodically update tooltip
    if (g_tray)
    {
        if (!g_tray->icon_added)
            g_tray->icon_added = Shell_NotifyIconW (NIM_ADD, &g_tray->nid);
        tray_update_tooltip (g_tray);
    }

    return G_SOURCE_CONTINUE;
}

void
gf_gui_tray_init (gf_app_state_t *app)
{
    gf_tray_data_t *tray = g_new0 (gf_tray_data_t, 1);
    tray->app = app;
    g_tray = tray;

    // register window class for tray message handling
    WNDCLASSEXW wcx = { 0 };
    wcx.cbSize = sizeof (wcx);
    wcx.lpfnWndProc = tray_wndproc;
    wcx.hInstance = GetModuleHandle (NULL);
    wcx.lpszClassName = L"GridFluxTrayClass";
    RegisterClassExW (&wcx);

    tray->taskbar_created = RegisterWindowMessageW (L"TaskbarCreated");
    // A hidden top-level window receives Explorer's TaskbarCreated broadcast.
    tray->msg_hwnd = CreateWindowExW (0, L"GridFluxTrayClass", L"GridFluxTray", 0, 0, 0,
                                      0, 0, NULL, NULL, GetModuleHandle (NULL), NULL);
    if (!tray->msg_hwnd)
    {
        g_free (tray);
        g_tray = NULL;
        return;
    }
    ChangeWindowMessageFilterEx (tray->msg_hwnd, GF_TRAY_SHOW_MESSAGE, MSGFLT_ALLOW,
                                 NULL);
    ChangeWindowMessageFilterEx (tray->msg_hwnd, tray->taskbar_created, MSGFLT_ALLOW,
                                 NULL);

    // set up notification icon data
    memset (&tray->nid, 0, sizeof (tray->nid));
    tray->nid.cbSize = sizeof (tray->nid);
    tray->nid.hWnd = tray->msg_hwnd;
    tray->nid.uID = 1;
    tray->nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray->nid.uCallbackMessage = WM_TRAY_ICON;
    tray->nid.hIcon = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_ICON1));

    if (!tray->nid.hIcon)
        tray->nid.hIcon = LoadIcon (NULL, IDI_APPLICATION);

    wcscpy (tray->nid.szTip, L"GridFlux");

    tray->icon_added = Shell_NotifyIconW (NIM_ADD, &tray->nid);
    tray_update_tooltip (tray);

    // pump Win32 messages periodically to handle tray events
    tray->pump_timer = g_timeout_add (200, tray_pump_messages, tray);

    app->tray_data = tray;
}

void
gf_gui_tray_destroy (gf_app_state_t *app)
{
    gf_tray_data_t *tray = (gf_tray_data_t *)app->tray_data;
    if (!tray)
        return;

    if (tray->pump_timer)
        g_source_remove (tray->pump_timer);
    if (tray->show_source)
        g_source_remove (tray->show_source);

    Shell_NotifyIconW (NIM_DELETE, &tray->nid);

    if (tray->msg_hwnd)
        DestroyWindow (tray->msg_hwnd);

    UnregisterClassW (L"GridFluxTrayClass", GetModuleHandle (NULL));

    g_free (tray);
    app->tray_data = NULL;
    g_tray = NULL;
}

void
gf_gui_tray_update_status (gf_app_state_t *app, gboolean server_running)
{
    gf_tray_data_t *tray = (gf_tray_data_t *)app->tray_data;
    if (!tray)
        return;

    tray->server_running = server_running;
    tray_update_tooltip (tray);
}

#endif // _WIN32

#ifdef __linux__
#include "../bridge/process_manager.h"
#include "tray.h"
#include <gio/gio.h>
#include <string.h>

// GTK4 has no GtkStatusIcon. Export the StatusNotifierItem protocol over the
// session bus and register again whenever the desktop's tray host restarts.
typedef struct
{
    GDBusConnection *bus;
    GDBusNodeInfo *info;
    guint object_id;
    guint menu_id;
    guint watcher_id;
    gf_app_state_t *app;
} gf_linux_tray_t;

static const char tray_xml[]
    = "<node><interface name='org.kde.StatusNotifierItem'>"
      "<property name='Category' type='s' access='read'/>"
      "<property name='Id' type='s' access='read'/>"
      "<property name='Title' type='s' access='read'/>"
      "<property name='Status' type='s' access='read'/>"
      "<property name='WindowId' type='u' access='read'/>"
      "<property name='IconName' type='s' access='read'/>"
      "<property name='IconPixmap' type='a(iiay)' access='read'/>"
      "<property name='OverlayIconName' type='s' access='read'/>"
      "<property name='OverlayIconPixmap' type='a(iiay)' access='read'/>"
      "<property name='AttentionIconName' type='s' access='read'/>"
      "<property name='AttentionIconPixmap' type='a(iiay)' access='read'/>"
      "<property name='AttentionMovieName' type='s' access='read'/>"
      "<property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
      "<property name='ItemIsMenu' type='b' access='read'/>"
      "<property name='Menu' type='o' access='read'/>"
      "<method name='Activate'><arg type='i' direction='in'/>"
      "<arg type='i' direction='in'/></method>"
      "<method name='SecondaryActivate'><arg type='i' direction='in'/>"
      "<arg type='i' direction='in'/></method>"
      "<method name='ContextMenu'><arg type='i' direction='in'/>"
      "<arg type='i' direction='in'/></method>"
      "<method name='Scroll'><arg type='i' direction='in'/>"
      "<arg type='s' direction='in'/></method>"
      "</interface><interface name='com.canonical.dbusmenu'>"
      "<property name='Version' type='u' access='read'/>"
      "<property name='TextDirection' type='s' access='read'/>"
      "<property name='Status' type='s' access='read'/>"
      "<method name='GetLayout'><arg type='i' direction='in'/>"
      "<arg type='i' direction='in'/><arg type='as' direction='in'/>"
      "<arg type='u' direction='out'/><arg type='(ia{sv}av)' direction='out'/></method>"
      "<method name='GetGroupProperties'><arg type='ai' direction='in'/>"
      "<arg type='as' direction='in'/><arg type='a(ia{sv})' direction='out'/></method>"
      "<method name='GetProperty'><arg type='i' direction='in'/>"
      "<arg type='s' direction='in'/><arg type='v' direction='out'/></method>"
      "<method name='Event'><arg type='i' direction='in'/>"
      "<arg type='s' direction='in'/><arg type='v' direction='in'/>"
      "<arg type='u' direction='in'/></method>"
      "<method name='AboutToShow'><arg type='i' direction='in'/>"
      "<arg type='b' direction='out'/></method>"
      "</interface></node>";

static GVariant *
menu_properties (int id)
{
    static const char *labels[] = { "", "Show GridFlux", "Start", "Stop", "Quit" };
    GVariantBuilder props;
    g_variant_builder_init (&props, G_VARIANT_TYPE ("a{sv}"));
    if (id > 0 && id <= 4)
    {
        g_variant_builder_add (&props, "{sv}", "label",
                               g_variant_new_string (labels[id]));
        g_variant_builder_add (&props, "{sv}", "enabled", g_variant_new_boolean (TRUE));
        g_variant_builder_add (&props, "{sv}", "visible", g_variant_new_boolean (TRUE));
    }
    else if (id == 0)
        g_variant_builder_add (&props, "{sv}", "children-display",
                               g_variant_new_string ("submenu"));
    return g_variant_builder_end (&props);
}

static GVariant *
menu_layout (int id, int depth)
{
    GVariantBuilder children;
    g_variant_builder_init (&children, G_VARIANT_TYPE ("av"));
    if (id == 0 && depth != 0)
        for (int i = 1; i <= 4; i++)
            g_variant_builder_add (&children, "v", menu_layout (i, 0));
    return g_variant_new ("(i@a{sv}@av)", id, menu_properties (id),
                          g_variant_builder_end (&children));
}

static void
menu_method (GDBusConnection *bus, const gchar *sender, const gchar *path,
             const gchar *interface, const gchar *method, GVariant *parameters,
             GDBusMethodInvocation *invocation, gpointer user_data)
{
    (void)bus;
    (void)sender;
    (void)path;
    (void)interface;
    gf_linux_tray_t *tray = user_data;
    GVariant *reply = NULL;
    if (strcmp (method, "GetLayout") == 0)
    {
        int id, depth;
        g_variant_get_child (parameters, 0, "i", &id);
        g_variant_get_child (parameters, 1, "i", &depth);
        reply = g_variant_new ("(u@(ia{sv}av))", 1u, menu_layout (id, depth));
    }
    else if (strcmp (method, "GetGroupProperties") == 0)
    {
        GVariantBuilder groups;
        g_variant_builder_init (&groups, G_VARIANT_TYPE ("a(ia{sv})"));
        GVariant *ids = g_variant_get_child_value (parameters, 0);
        if (g_variant_n_children (ids) == 0)
        {
            for (int i = 0; i <= 4; i++)
                g_variant_builder_add (&groups, "(i@a{sv})", i, menu_properties (i));
        }
        else
        {
            GVariantIter iter;
            int id;
            g_variant_iter_init (&iter, ids);
            while (g_variant_iter_next (&iter, "i", &id))
                if (id >= 0 && id <= 4)
                    g_variant_builder_add (&groups, "(i@a{sv})", id,
                                           menu_properties (id));
        }
        g_variant_unref (ids);
        reply = g_variant_new ("(@a(ia{sv}))", g_variant_builder_end (&groups));
    }
    else if (strcmp (method, "GetProperty") == 0)
    {
        int id;
        const char *name;
        g_variant_get (parameters, "(i&s)", &id, &name);
        GVariant *props = g_variant_ref_sink (menu_properties (id));
        GVariant *value = g_variant_lookup_value (props, name, NULL);
        if (!value)
        {
            g_variant_unref (props);
            g_dbus_method_invocation_return_dbus_error (
                invocation, "com.canonical.dbusmenu.Error.PropertyNotFound",
                "Unknown menu property");
            return;
        }
        reply = g_variant_new ("(v)", value);
        g_variant_unref (value);
        g_variant_unref (props);
    }
    else if (strcmp (method, "AboutToShow") == 0)
        reply = g_variant_new ("(b)", FALSE);
    else if (strcmp (method, "Event") == 0)
    {
        int id;
        const char *event;
        g_variant_get_child (parameters, 0, "i", &id);
        g_variant_get_child (parameters, 1, "&s", &event);
        if (strcmp (event, "clicked") == 0)
        {
            if (id == 1)
                gtk_window_present (GTK_WINDOW (tray->app->window));
            else if (id == 2)
                gf_server_start ();
            else if (id == 3)
                gf_server_stop ();
            else if (id == 4)
            {
                gf_server_stop ();
                g_application_quit (G_APPLICATION (
                    gtk_window_get_application (GTK_WINDOW (tray->app->window))));
            }
        }
    }
    g_dbus_method_invocation_return_value (invocation, reply);
}

static GVariant *
menu_property (GDBusConnection *bus, const gchar *sender, const gchar *path,
               const gchar *interface, const gchar *property, GError **error,
               gpointer user_data)
{
    (void)bus;
    (void)sender;
    (void)path;
    (void)interface;
    (void)error;
    (void)user_data;
    if (strcmp (property, "Version") == 0)
        return g_variant_new_uint32 (3);
    return g_variant_new_string (strcmp (property, "Status") == 0 ? "normal" : "ltr");
}

static void
tray_method (GDBusConnection *bus, const gchar *sender, const gchar *path,
             const gchar *interface, const gchar *method, GVariant *parameters,
             GDBusMethodInvocation *invocation, gpointer user_data)
{
    (void)bus;
    (void)sender;
    (void)path;
    (void)interface;
    (void)parameters;
    gf_linux_tray_t *tray = user_data;
    if (strcmp (method, "Scroll") != 0)
        gtk_window_present (GTK_WINDOW (tray->app->window));
    g_dbus_method_invocation_return_value (invocation, NULL);
}

static GVariant *
tray_property (GDBusConnection *bus, const gchar *sender, const gchar *path,
               const gchar *interface, const gchar *property, GError **error,
               gpointer user_data)
{
    (void)bus;
    (void)sender;
    (void)path;
    (void)interface;
    (void)error;
    (void)user_data;
    if (strcmp (property, "Category") == 0)
        return g_variant_new_string ("ApplicationStatus");
    if (strcmp (property, "Id") == 0 || strcmp (property, "IconName") == 0)
        return g_variant_new_string ("gridflux");
    if (strcmp (property, "Title") == 0)
        return g_variant_new_string ("GridFlux");
    if (strcmp (property, "Status") == 0)
        return g_variant_new_string ("Active");
    if (strcmp (property, "WindowId") == 0)
        return g_variant_new_uint32 (0);
    if (strcmp (property, "ItemIsMenu") == 0)
        return g_variant_new_boolean (FALSE);
    if (strcmp (property, "Menu") == 0)
        return g_variant_new_object_path ("/StatusNotifierItem");
    if (g_str_has_suffix (property, "Pixmap"))
        return g_variant_new_array (G_VARIANT_TYPE ("(iiay)"), NULL, 0);
    if (strcmp (property, "ToolTip") == 0)
        return g_variant_new ("(s@a(iiay)ss)", "gridflux",
                              g_variant_new_array (G_VARIANT_TYPE ("(iiay)"), NULL, 0),
                              "GridFlux", "Open GridFlux Control Panel");
    return g_variant_new_string ("");
}

static void
tray_watcher_appeared (GDBusConnection *bus, const gchar *name, const gchar *owner,
                       gpointer user_data)
{
    (void)name;
    (void)owner;
    (void)user_data;
    g_dbus_connection_call (bus, "org.kde.StatusNotifierWatcher",
                            "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
                            "RegisterStatusNotifierItem",
                            g_variant_new ("(s)", "/StatusNotifierItem"), NULL,
                            G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

void
gf_gui_tray_init (gf_app_state_t *app)
{
    gf_linux_tray_t *tray = g_new0 (gf_linux_tray_t, 1);
    GError *error = NULL;
    tray->app = app;
    tray->bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
    if (!tray->bus)
    {
        g_warning ("Cannot connect GridFlux tray: %s", error->message);
        g_clear_error (&error);
        g_free (tray);
        return;
    }
    tray->info = g_dbus_node_info_new_for_xml (tray_xml, NULL);
    static const GDBusInterfaceVTable vtable
        = { .method_call = tray_method, .get_property = tray_property };
    tray->object_id = g_dbus_connection_register_object (tray->bus, "/StatusNotifierItem",
                                                         tray->info->interfaces[0],
                                                         &vtable, tray, NULL, &error);
    if (tray->object_id)
    {
        static const GDBusInterfaceVTable menu_vtable
            = { .method_call = menu_method, .get_property = menu_property };
        tray->menu_id = g_dbus_connection_register_object (
            tray->bus, "/StatusNotifierItem", tray->info->interfaces[1], &menu_vtable,
            tray, NULL, &error);
    }
    if (!tray->object_id || !tray->menu_id)
    {
        g_warning ("Cannot register GridFlux tray: %s", error->message);
        g_clear_error (&error);
        if (tray->object_id)
            g_dbus_connection_unregister_object (tray->bus, tray->object_id);
        g_dbus_node_info_unref (tray->info);
        g_object_unref (tray->bus);
        g_free (tray);
        return;
    }
    tray->watcher_id = g_bus_watch_name_on_connection (
        tray->bus, "org.kde.StatusNotifierWatcher", G_BUS_NAME_WATCHER_FLAGS_NONE,
        tray_watcher_appeared, NULL, tray, NULL);
    app->tray_data = tray;
}

void
gf_gui_tray_destroy (gf_app_state_t *app)
{
    gf_linux_tray_t *tray = app->tray_data;
    if (!tray)
        return;
    g_bus_unwatch_name (tray->watcher_id);
    g_dbus_connection_unregister_object (tray->bus, tray->object_id);
    g_dbus_connection_unregister_object (tray->bus, tray->menu_id);
    g_dbus_node_info_unref (tray->info);
    g_object_unref (tray->bus);
    g_free (tray);
    app->tray_data = NULL;
}

void
gf_gui_tray_update_status (gf_app_state_t *app, gboolean server_running)
{
    (void)app;
    (void)server_running;
}
#endif
