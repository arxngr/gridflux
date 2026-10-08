#include "gui/app_state.h"
#include <assert.h>
#include <gdk/win32/gdkwin32.h>
#include <shellapi.h>

static HANDLE reply_gate;
static DWORD ui_thread;
static LONG client_calls;
static unsigned refreshes;
static unsigned server_actions;

static BOOL WINAPI
test_notify_icon (DWORD action, PNOTIFYICONDATAW data)
{
    (void)action;
    (void)data;
    return TRUE;
}

gf_ipc_response_t
gf_run_client_command (const char *command)
{
    (void)command;
    assert (GetCurrentThreadId () != ui_thread);
    InterlockedIncrement (&client_calls);
    assert (WaitForSingleObject (reply_gate, INFINITE) == WAIT_OBJECT_0);
    return (gf_ipc_response_t){ .status = GF_IPC_ERROR_TIMEOUT };
}

bool
gf_server_is_running (void)
{
    return true;
}

bool
gf_server_start (void)
{
    server_actions++;
    return true;
}

bool
gf_server_stop (void)
{
    server_actions++;
    return true;
}

gf_gui_platform_t *
gf_gui_platform_create (void)
{
    static gf_gui_platform_t platform;
    return &platform;
}

GtkWidget *
gf_gui_toolbar_new (gf_app_state_t *app)
{
    (void)app;
    return gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
}

GtkWidget *
gf_gui_workspace_panel_new (gf_app_state_t *app)
{
    app->workspace_table = gtk_scrolled_window_new ();
    return app->workspace_table;
}

void
gf_refresh_workspaces (gf_app_state_t *app)
{
    assert (GetCurrentThreadId () == ui_thread);
    assert (g_object_get_data (G_OBJECT (app->window), "ws_response"));
    assert (g_object_get_data (G_OBJECT (app->window), "win_response"));
    g_object_set_data (G_OBJECT (app->window), "ws_response", NULL);
    g_object_set_data (G_OBJECT (app->window), "win_response", NULL);
    refreshes++;
}

// Use real GTK windows, close handling, activation, tray dispatch, and workers.
// Explorer icon calls and server operations are replaced to isolate the test.
#include "../../src/gui/platform/async.c"
#include "../../src/gui/window/main_window.c"
#include "../../src/gui/window/statusbar.c"
#define Shell_NotifyIconW test_notify_icon
#include "../../src/gui/window/tray.c"
#undef Shell_NotifyIconW
#define main test_gui_entry
#include "../../src/gui/gui.c"
#undef main

static void
iterate (void)
{
    for (int i = 0; i < 32 && g_main_context_pending (NULL); i++)
        g_main_context_iteration (NULL, FALSE);
    Sleep (1);
}

static void
expect_visible (gboolean visible)
{
    ULONGLONG start = GetTickCount64 ();
    while (gtk_widget_get_visible (g_widgets->window) != visible)
    {
        assert (GetTickCount64 () - start < 2000);
        iterate ();
    }
    for (int i = 0; i < 5; i++)
        iterate ();
}

static DWORD WINAPI
gui_lifecycle (LPVOID desktop)
{
    assert (SetThreadDesktop ((HDESK)desktop));
    // Keep rendering independent of graphics drivers and the active desktop.
    g_setenv ("GSK_RENDERER", "cairo", TRUE);
    gtk_init ();
    ui_thread = GetCurrentThreadId ();
    reply_gate = CreateEvent (NULL, TRUE, FALSE, NULL);
    assert (reply_gate);
    GtkApplication *application
        = gtk_application_new ("dev.gridflux.gui.test", G_APPLICATION_NON_UNIQUE);
    assert (g_application_register (G_APPLICATION (application), NULL, NULL));
    g_start_minimized = TRUE;
    gtk_activate (application, NULL);
    assert (g_widgets && g_widgets->tray_data
            && !gtk_widget_get_visible (g_widgets->window));
    gf_tray_data_t *tray = g_widgets->tray_data;
    // A second shortcut launch forwards through the real native tray window.
    g_start_minimized = FALSE;
    char *arguments[] = { "gridflux-gui", NULL };
    assert (test_gui_entry (1, arguments) == 0);
    expect_visible (TRUE);
    gtk_window_close (GTK_WINDOW (g_widgets->window));
    expect_visible (FALSE);
    for (unsigned i = 0; i < 20; i++)
    {
        tray_wndproc (tray->msg_hwnd, GF_TRAY_SHOW_MESSAGE, 0, 0);
        expect_visible (TRUE);
        gtk_window_close (GTK_WINDOW (g_widgets->window));
        expect_visible (FALSE);
        // Repeated clicks must show one window instead of toggling it away.
        tray_wndproc (tray->msg_hwnd, WM_TRAY_ICON, 0, WM_LBUTTONUP);
        tray_wndproc (tray->msg_hwnd, WM_TRAY_ICON, 0, WM_LBUTTONUP);
        expect_visible (TRUE);
        gtk_window_close (GTK_WINDOW (g_widgets->window));
        expect_visible (FALSE);
        tray_wndproc (tray->msg_hwnd, WM_COMMAND, ID_TRAY_SHOW, 0);
        expect_visible (TRUE);
        gtk_window_close (GTK_WINDOW (g_widgets->window));
        expect_visible (FALSE);
        gtk_activate (application, NULL);
        expect_visible (TRUE);
        gtk_window_close (GTK_WINDOW (g_widgets->window));
        expect_visible (FALSE);
    }
    assert (client_calls > 0 && refreshes == 0 && server_actions == 0);
    // All that interaction completed while server responses were blocked.
    GtkWidget *bar = g_object_get_data (G_OBJECT (g_widgets->window), "statusbar");
    assert (g_object_get_data (G_OBJECT (bar), "healthcheck-pending"));
    for (int i = 0; i < 10; i++)
        gf_gui_platform_run_refresh (g_widgets);
    assert (SetEvent (reply_gate));
    ULONGLONG start = GetTickCount64 ();
    while (g_widgets->refresh_in_progress
           || g_object_get_data (G_OBJECT (bar), "healthcheck-pending"))
    {
        assert (GetTickCount64 () - start < 2000);
        iterate ();
    }
    assert (refreshes == 1);
    GtkWidget *label = g_object_get_data (G_OBJECT (bar), "label");
    assert (strcmp (gtk_label_get_text (GTK_LABEL (label)), "Not Ready") == 0);
    gf_gui_main_window_present (g_widgets);
    expect_visible (TRUE);
    gtk_window_minimize (GTK_WINDOW (g_widgets->window));
    for (int i = 0; i < 50; i++)
        iterate ();
    tray_wndproc (tray->msg_hwnd, GF_TRAY_SHOW_MESSAGE, 0, 0);
    expect_visible (TRUE);
    for (int i = 0; i < 50; i++)
        iterate ();
    GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (g_widgets->window));
    assert (surface && !IsIconic (GDK_SURFACE_HWND (surface)));
    tray_request_show (tray);
    guint pending = tray->show_source;
    assert (pending);
    // A late IPC result must release its retained window without touching
    // destroyed widgets or starting another refresh during shutdown.
    assert (ResetEvent (reply_gate));
    gf_gui_platform_run_refresh (g_widgets);
    gf_gui_platform_run_command (g_widgets, "query count", TRUE, TRUE);
    gtk_shutdown (application, NULL);
    assert (!g_main_context_find_source_by_id (NULL, pending));
    assert (!g_widgets->tray_data);
    gtk_window_destroy (GTK_WINDOW (g_widgets->window));
    assert (SetEvent (reply_gate));
    start = GetTickCount64 ();
    while (g_widgets->refresh_in_progress || g_widgets->operation_in_progress)
    {
        assert (GetTickCount64 () - start < 2000);
        iterate ();
    }
    assert (refreshes == 1);
    g_application_release (G_APPLICATION (application));
    g_object_unref (application);
    CloseHandle (reply_gate);
    puts ("GUI remains responsive and reopens while server replies are delayed");
    return 0;
}

int
main (void)
{
    char desktop_name[64];
    snprintf (desktop_name, sizeof (desktop_name), "GridFluxGuiTest%lu",
              GetCurrentProcessId ());
    HDESK desktop = CreateDesktopA (desktop_name, NULL, NULL, 0, GENERIC_ALL, NULL);
    assert (desktop);
    // GTK retains native helper windows until its UI thread exits. Let that
    // thread finish before closing the isolated desktop from the test owner.
    HANDLE thread = CreateThread (NULL, 0, gui_lifecycle, desktop, 0, NULL);
    assert (thread && WaitForSingleObject (thread, 25000) == WAIT_OBJECT_0);
    DWORD result;
    assert (GetExitCodeThread (thread, &result) && result == 0);
    CloseHandle (thread);
    assert (CloseDesktop (desktop));
    return 0;
}
