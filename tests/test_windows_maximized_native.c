#include "platform/windows/window.h"
#include <assert.h>
#include <stdio.h>

static BOOL custom_frame, clamp_maximized, handling_size;

static LRESULT CALLBACK
test_window_proc (HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    LRESULT result = DefWindowProc (window, message, wp, lp);
    if (clamp_maximized && !handling_size && message == WM_SIZE && IsZoomed (window))
    {
        MONITORINFO monitor = { .cbSize = sizeof (monitor) };
        RECT rect;
        assert (GetMonitorInfo (MonitorFromWindow (window, MONITOR_DEFAULTTONEAREST),
                                &monitor));
        assert (GetWindowRect (window, &rect));
        UINT dpi = GetDpiForWindow (window);
        int border_x = GetSystemMetricsForDpi (SM_CXSIZEFRAME, dpi)
                       + GetSystemMetricsForDpi (SM_CXPADDEDBORDER, dpi);
        int border_y = GetSystemMetricsForDpi (SM_CYSIZEFRAME, dpi)
                       + GetSystemMetricsForDpi (SM_CXPADDEDBORDER, dpi);
        RECT allowed = monitor.rcWork;
        InflateRect (&allowed, border_x, border_y);
        if (!EqualRect (&rect, &allowed))
        {
            handling_size = TRUE;
            assert (SetWindowPos (window, NULL, allowed.left, allowed.top,
                                  allowed.right - allowed.left,
                                  allowed.bottom - allowed.top,
                                  SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSENDCHANGING));
            handling_size = FALSE;
        }
    }
    if (custom_frame && message == WM_SIZE && IsZoomed (window))
    {
        // Some custom frames clip to rcWork on a resize.
        MONITORINFO monitor = { .cbSize = sizeof (monitor) };
        RECT rect;
        assert (GetMonitorInfo (MonitorFromWindow (window, MONITOR_DEFAULTTONEAREST),
                                &monitor));
        assert (GetWindowRect (window, &rect));
        RECT clip = monitor.rcWork;
        OffsetRect (&clip, -rect.left, -rect.top);
        HRGN region = CreateRectRgnIndirect (&clip);
        assert (region && SetWindowRgn (window, region, FALSE));
    }
    return result;
}

static void
expect_visible_region (HWND window, const RECT *bounds)
{
    RECT region, rect;
    assert (GetWindowRgnBox (window, &region) == SIMPLEREGION);
    assert (GetWindowRect (window, &rect));
    OffsetRect (&region, rect.left, rect.top);
    if (!EqualRect (&region, bounds))
        fprintf (stderr, "Region=%ld,%ld,%ld,%ld expected=%ld,%ld,%ld,%ld clamp=%d\n",
                 region.left, region.top, region.right, region.bottom, bounds->left,
                 bounds->top, bounds->right, bounds->bottom, clamp_maximized);
    assert (EqualRect (&region, bounds));
}

static void
restore_with_launcher (const char *launcher, char *desktop_name)
{
    char command[1024];
    int length
        = snprintf (command, sizeof (command), "\"%s\" --restore-desktop", launcher);
    assert (length > 0 && length < sizeof (command));
    STARTUPINFOA startup = { .cb = sizeof (startup) };
    startup.lpDesktop = desktop_name;
    PROCESS_INFORMATION process = { 0 };
    assert (CreateProcessA (launcher, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL,
                            NULL, &startup, &process));
    CloseHandle (process.hThread);
    ULONGLONG started = GetTickCount64 ();
    for (;;)
    {
        DWORD result
            = MsgWaitForMultipleObjects (1, &process.hProcess, FALSE, 5000, QS_ALLINPUT);
        if (result == WAIT_OBJECT_0)
            break;
        assert (result == WAIT_OBJECT_0 + 1 && GetTickCount64 () - started < 5000);
        // SetWindowPos from the recovery process can send messages to this
        // thread. Pump them while waiting, just like a real application's GUI.
        MSG message;
        while (PeekMessage (&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage (&message);
            DispatchMessage (&message);
        }
    }
    DWORD code;
    assert (GetExitCodeProcess (process.hProcess, &code) && code == 0);
    CloseHandle (process.hProcess);
}

int
main (int argc, char **argv)
{
    // Recovery enumerates its desktop. Isolate the test from the user's apps
    // and taskbars while exercising the real cross-process launcher command.
    char desktop_name[64];
    snprintf (desktop_name, sizeof (desktop_name), "GridFluxMaximizedTest%lu",
              GetCurrentProcessId ());
    HDESK previous = GetThreadDesktop (GetCurrentThreadId ());
    HDESK desktop = CreateDesktopA (desktop_name, NULL, NULL, 0, GENERIC_ALL, NULL);
    assert (desktop && SetThreadDesktop (desktop));
    SetProcessDpiAwarenessContext (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSA cls = { .lpfnWndProc = test_window_proc,
                      .hInstance = GetModuleHandle (NULL),
                      .lpszClassName = "GridFluxMaximizedTest" };
    assert (RegisterClassA (&cls));
    // WS_MAXIMIZE sets native maximize state without displaying the test window.
    HWND window = CreateWindowExA (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName,
                                   "GridFlux maximized bounds test",
                                   WS_OVERLAPPEDWINDOW | WS_MAXIMIZE, 100, 100, 320, 240,
                                   NULL, NULL, cls.hInstance, NULL);
    assert (window && IsZoomed (window) && !IsWindowVisible (window));
    HWND foreground = GetForegroundWindow ();
    WINDOWPLACEMENT original = { .length = sizeof (original) };
    assert (GetWindowPlacement (window, &original));
    MONITORINFO monitor = { .cbSize = sizeof (monitor) };
    assert (
        GetMonitorInfo (MonitorFromWindow (window, MONITOR_DEFAULTTONEAREST), &monitor));
    assert (gf_window_state_apply (window, TRUE));
    assert (IsZoomed (window) && !IsWindowVisible (window));
    assert (GetForegroundWindow () == foreground);
    WINDOWPLACEMENT expanded = { .length = sizeof (expanded) };
    assert (GetWindowPlacement (window, &expanded));
    assert (EqualRect (&original.rcNormalPosition, &expanded.rcNormalPosition));
    RECT rect;
    assert (GetWindowRect (window, &rect));
    assert (rect.left <= monitor.rcMonitor.left && rect.top <= monitor.rcMonitor.top);
    assert (rect.right >= monitor.rcMonitor.right
            && rect.bottom >= monitor.rcMonitor.bottom);
    RECT filled = rect;
    if (argc > 1)
        restore_with_launcher (argv[1], desktop_name);
    else
        assert (gf_window_state_apply (window, FALSE));
    assert (GetWindowRect (window, &rect));
    assert (rect.left - filled.left == monitor.rcWork.left - monitor.rcMonitor.left);
    assert (rect.top - filled.top == monitor.rcWork.top - monitor.rcMonitor.top);
    assert (filled.right - rect.right == monitor.rcMonitor.right - monitor.rcWork.right);
    assert (filled.bottom - rect.bottom
            == monitor.rcMonitor.bottom - monitor.rcWork.bottom);
    assert (!GetPropA (window, GF_WINDOW_STATE_FILL_PROP));
    assert (IsZoomed (window) && !IsWindowVisible (window));
    WINDOWPLACEMENT restored = { .length = sizeof (restored) };
    assert (GetWindowPlacement (window, &restored));
    assert (EqualRect (&original.rcNormalPosition, &restored.rcNormalPosition));
    assert (GetForegroundWindow () == foreground);

    custom_frame = TRUE;
    // Force a real size message, which installs the work-area clipping region.
    SendMessage (window, WM_SIZE, SIZE_MAXIMIZED, 0);
    expect_visible_region (window, &monitor.rcWork);
    assert (gf_window_state_apply (window, TRUE));
    expect_visible_region (window, &monitor.rcMonitor);
    for (int i = 0; i < 30; i++)
        assert (gf_window_state_apply (window, TRUE));
    expect_visible_region (window, &monitor.rcMonitor);
    if (argc > 1)
        restore_with_launcher (argv[1], desktop_name);
    else
        assert (gf_window_state_apply (window, FALSE));
    expect_visible_region (window, &monitor.rcWork);
    assert (IsZoomed (window) && GetForegroundWindow () == foreground);

    // A window procedure that clamps maximized resizes must still fill, retain
    // its saved normal position, and remain hidden throughout recovery.
    clamp_maximized = TRUE;
    assert (gf_window_state_apply (window, TRUE));
    expect_visible_region (window, &monitor.rcMonitor);
    assert (IsZoomed (window) && !IsWindowVisible (window));
    assert (GetWindowPlacement (window, &expanded));
    assert (EqualRect (&original.rcNormalPosition, &expanded.rcNormalPosition));
    if (argc > 1)
        restore_with_launcher (argv[1], desktop_name);
    else
        assert (gf_window_state_apply (window, FALSE));
    expect_visible_region (window, &monitor.rcWork);
    assert (GetWindowPlacement (window, &restored));
    assert (EqualRect (&original.rcNormalPosition, &restored.rcNormalPosition));
    assert (IsZoomed (window) && !IsWindowVisible (window));
    DestroyWindow (window);
    UnregisterClassA (cls.lpszClassName, cls.hInstance);
    assert (SetThreadDesktop (previous));
    assert (CloseDesktop (desktop));
    puts ("Native maximized bounds preserve mode, focus, and normal placement");
    return 0;
}
