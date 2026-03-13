#ifdef _WIN32

#include "tray.h"
#include "../bridge/process_manager.h"

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
    gboolean server_running;
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

static void
tray_toggle_window (gf_tray_data_t *tray)
{
    if (!tray->app || !tray->app->window)
        return;

    gboolean visible = gtk_widget_get_visible (tray->app->window);
    if (visible)
    {
        gtk_widget_set_visible (tray->app->window, FALSE);
    }
    else
    {
        gtk_widget_set_visible (tray->app->window, TRUE);
        gtk_window_present (GTK_WINDOW (tray->app->window));
    }
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
    if (msg == WM_TRAY_ICON)
    {
        switch (LOWORD (lParam))
        {
        case WM_LBUTTONUP:
            tray_toggle_window (g_tray);
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
            if (g_tray && g_tray->app && g_tray->app->window)
            {
                gtk_widget_set_visible (g_tray->app->window, TRUE);
                gtk_window_present (GTK_WINDOW (g_tray->app->window));
            }
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
    (void)user_data;
    MSG msg;
    while (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage (&msg);
        DispatchMessage (&msg);
    }

    // periodically update tooltip
    if (g_tray)
        tray_update_tooltip (g_tray);

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

    // create hidden message-only window
    tray->msg_hwnd
        = CreateWindowExW (0, L"GridFluxTrayClass", L"GridFluxTray", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, NULL, GetModuleHandle (NULL), NULL);

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

    Shell_NotifyIconW (NIM_ADD, &tray->nid);
    tray_update_tooltip (tray);

    // pump Win32 messages periodically to handle tray events
    tray->pump_timer = g_timeout_add (200, tray_pump_messages, NULL);

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
