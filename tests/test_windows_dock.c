#include "platform/windows/internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// Run the production Windows taskbar controller against a deterministic shell.
// No real Explorer taskbar, pointer, or Windows setting is changed by this test.
static bool visible[4] = { false, true, true, true };
static bool valid[4] = { false, true, true, true };
static HANDLE marker[4];
static RECT rects[4]
    = { { 0 }, { 0, 1040, 1920, 1080 }, { -1280, 824, 0, 864 }, { 0, 1040, 1920, 1080 } };
static HWND primary = (HWND)(uintptr_t)1;
static POINT pointer = { 100, 200 };
static HWND foreground, menu_owner;
static ULONGLONG now = 1000;
static UINT native_state;
static uint32_t shows[4];
static bool win_key;

static unsigned
index_of (HWND window)
{
    unsigned index = (unsigned)(uintptr_t)window;
    assert (index > 0 && index < 4);
    return index;
}
static HWND
fake_find_window (LPCSTR cls, LPCSTR title)
{
    (void)title;
    assert (strcmp (cls, "Shell_TrayWnd") == 0);
    return primary;
}
static HWND
fake_find_window_ex (HWND parent, HWND after, LPCSTR cls, LPCSTR title)
{
    (void)parent;
    (void)title;
    assert (strcmp (cls, "Shell_SecondaryTrayWnd") == 0);
    return after ? NULL : (HWND)(uintptr_t)2;
}
static BOOL
fake_visible (HWND window)
{
    return visible[index_of (window)];
}
static BOOL
fake_is_window (HWND window)
{
    return valid[index_of (window)];
}
static BOOL
fake_show (HWND window, int command)
{
    assert (command == SW_HIDE || command == SW_SHOWNOACTIVATE);
    unsigned i = index_of (window);
    BOOL previous = visible[i];
    visible[i] = command != SW_HIDE;
    shows[i]++;
    return previous;
}
static BOOL
fake_set_prop (HWND window, LPCSTR name, HANDLE value)
{
    assert (strcmp (name, "GridFlux.ManagedTaskbar") == 0);
    marker[index_of (window)] = value;
    return TRUE;
}
static HANDLE
fake_get_prop (HWND window, LPCSTR name)
{
    (void)name;
    return marker[index_of (window)];
}
static HANDLE
fake_remove_prop (HWND window, LPCSTR name)
{
    (void)name;
    unsigned i = index_of (window);
    HANDLE previous = marker[i];
    marker[i] = NULL;
    return previous;
}
static UINT_PTR
fake_appbar (DWORD message, PAPPBARDATA data)
{
    (void)data;
    // The controller must never change the global taskbar auto-hide preference.
    assert (message == ABM_GETSTATE);
    return native_state;
}
static BOOL
fake_rect (HWND window, LPRECT out)
{
    *out = rects[index_of (window)];
    return TRUE;
}
static BOOL
fake_cursor (LPPOINT out)
{
    *out = pointer;
    return TRUE;
}
static ULONGLONG
fake_tick (void)
{
    return now;
}
static HWND
fake_foreground (void)
{
    return foreground;
}
static HWND
fake_ancestor (HWND window, UINT flags)
{
    assert (flags == GA_ROOTOWNER);
    return window;
}
static DWORD
fake_thread (HWND window, LPDWORD pid)
{
    (void)window;
    (void)pid;
    return 1; // Taskbars may share an Explorer thread across monitors.
}
static BOOL
fake_gui_thread (DWORD thread, PGUITHREADINFO out)
{
    (void)thread;
    out->flags = menu_owner ? GUI_INMENUMODE : 0;
    out->hwndMenuOwner = menu_owner;
    return TRUE;
}
static SHORT
fake_key (int key)
{
    (void)key;
    return win_key ? (SHORT)0x8000 : 0;
}
static BOOL
fake_enum_windows (WNDENUMPROC callback, LPARAM context)
{
    (void)callback;
    (void)context;
    return TRUE;
}
gf_monitor_id_t
gf_monitor_from_window (gf_platform_t *platform, gf_handle_t window)
{
    (void)platform;
    return index_of (window) == 2 ? 1 : 0;
}

#define FindWindowA fake_find_window
#define FindWindowExA fake_find_window_ex
#define IsWindowVisible fake_visible
#define IsWindow fake_is_window
#define ShowWindow fake_show
#define SetPropA fake_set_prop
#define GetPropA fake_get_prop
#define RemovePropA fake_remove_prop
#define SHAppBarMessage fake_appbar
#define GetWindowRect fake_rect
#define GetCursorPos fake_cursor
#define GetTickCount64 fake_tick
#define GetForegroundWindow fake_foreground
#define GetAncestor fake_ancestor
#define GetWindowThreadProcessId fake_thread
#define GetGUIThreadInfo fake_gui_thread
#define GetAsyncKeyState fake_key
#define EnumWindows fake_enum_windows
#include "../src/platform/windows/dock.c"
#include "../src/platform/windows/taskbar.c"
#include "../src/platform/windows/window_state.c"

int
main (void)
{
    gf_windows_platform_data_t data = { .enumerated_monitor_count = 2 };
    data.monitors[0].full_bounds = (gf_rect_t){ 0, 0, 1920, 1080 };
    data.monitors[1].full_bounds = (gf_rect_t){ -1280, -160, 1280, 1024 };
    gf_platform_t platform = { .platform_data = &data };
    bool hide[2] = { true, false };
    gf_dock_sync (&platform, hide, 2);
    assert (!visible[1] && visible[2] && marker[1] && !marker[2]);
    uint32_t original_shows = shows[1];
    for (unsigned i = 0; i < 10; i++)
        gf_dock_sync (&platform, hide, 2);
    assert (shows[1] == original_shows);

    pointer.y = 1079;
    gf_dock_sync (&platform, hide, 2);
    assert (visible[1] && visible[2]);
    pointer.y = 200;
    now += 200;
    gf_dock_sync (&platform, hide, 2);
    assert (visible[1]);
    now += 301;
    gf_dock_sync (&platform, hide, 2);
    assert (!visible[1]);

    hide[0] = false;
    hide[1] = true;
    gf_dock_sync (&platform, hide, 2);
    assert (visible[1] && !visible[2] && !marker[1] && marker[2]);
    pointer = (POINT){ -500, 863 };
    gf_dock_sync (&platform, hide, 2);
    assert (visible[2]);
    pointer = (POINT){ 100, 200 };
    now += 400;
    gf_dock_sync (&platform, hide, 2);
    assert (!visible[2]);
    foreground = (HWND)(uintptr_t)2;
    gf_dock_sync (&platform, hide, 2);
    assert (visible[2]);
    foreground = NULL;
    menu_owner = (HWND)(uintptr_t)2;
    hide[0] = true;
    now += 400;
    gf_dock_sync (&platform, hide, 2);
    assert (!visible[1] && visible[2]);
    menu_owner = NULL;

    // Existing native auto-hide stays under Explorer's control.
    native_state = ABS_AUTOHIDE | ABS_ALWAYSONTOP;
    gf_dock_sync (&platform, hide, 2);
    assert (visible[1] && visible[2] && !marker[1] && !marker[2]);
    assert (data.taskbar_count == 0 && native_state == (ABS_AUTOHIDE | ABS_ALWAYSONTOP));
    native_state = ABS_ALWAYSONTOP;

    // Stop/crash recovery works without the terminated manager's platform data.
    gf_dock_sync (&platform, hide, 2);
    assert (!visible[1] && !visible[2]);
    gf_taskbar_restore_all ();
    assert (visible[1] && visible[2] && !marker[1] && !marker[2]);
    data.taskbar_count = 0;

    // An Explorer restart invalidates its old HWND and must adopt the new bar.
    gf_dock_sync (&platform, hide, 2);
    valid[1] = false;
    marker[1] = NULL;
    primary = (HWND)(uintptr_t)3;
    gf_dock_sync (&platform, hide, 2);
    assert (!visible[3] && marker[3] && data.taskbar_count == 2);
    gf_dock_restore (&platform);
    assert (visible[3] && visible[2] && data.taskbar_count == 0);

    // Restore never revives a bar that was hidden before GridFlux managed it.
    visible[2] = false;
    hide[0] = false;
    gf_dock_sync (&platform, hide, 2);
    gf_dock_restore (&platform);
    assert (!visible[2] && !marker[2]);
    visible[2] = true;

    // Edge detection includes left/top taskbars and negative monitor origins.
    rects[2] = (RECT){ -1280, -160, -1240, 864 };
    gf_dock_sync (&platform, hide, 2);
    pointer = (POINT){ -1279, 200 };
    gf_dock_sync (&platform, hide, 2);
    assert (visible[2]);
    gf_dock_restore (&platform);
    rects[2] = (RECT){ -1280, -160, 0, -120 };
    pointer = (POINT){ 100, 200 };
    gf_dock_sync (&platform, hide, 2);
    pointer = (POINT){ -500, -159 };
    gf_dock_sync (&platform, hide, 2);
    assert (visible[2]);
    gf_dock_restore (&platform);
    puts ("Monitor-local taskbar auto-hide and recovery regressions passed");
    return 0;
}
