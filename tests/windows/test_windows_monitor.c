#include "platform/windows/internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// Fake only the display enumeration and clock. The watcher window, broadcast
// handling, device identity registry, and topology settling are production code.
static ULONGLONG now = 1000;
static bool connected[2] = { true, true };
static unsigned order[2] = { 0, 1 };
static unsigned generation, devices[2] = { 1, 2 };
static gf_rect_t areas[2] = { { 0, 0, 1920, 1080 }, { 1920, 0, 1280, 1024 } };
static unsigned window_monitor;
static bool capture_failed;

static HMONITOR
monitor_handle (unsigned i)
{
    return (HMONITOR)(uintptr_t)(generation * 100 + i + 1);
}
static BOOL
fake_monitor_info (HMONITOR monitor, LPMONITORINFO out)
{
    unsigned i = ((unsigned)(uintptr_t)monitor - 1) % 100;
    assert (i < 2);
    gf_rect_t *area = &areas[i];
    out->rcMonitor = (RECT){ area->x, area->y, area->x + (LONG)area->width,
                             area->y + (LONG)area->height };
    out->rcWork = out->rcMonitor;
    out->rcWork.bottom -= 40;
    out->dwFlags = i == 0 ? MONITORINFOF_PRIMARY : 0;
    if (out->cbSize == sizeof (MONITORINFOEXA))
        snprintf (((MONITORINFOEXA *)out)->szDevice, CCHDEVICENAME, "DISPLAY%u",
                  devices[i]);
    return TRUE;
}
static BOOL
fake_display_devices (LPCSTR name, DWORD index, PDISPLAY_DEVICEA out, DWORD flags)
{
    assert (index == 0 && flags == EDD_GET_DEVICE_INTERFACE_NAME);
    for (unsigned i = 0; i < 2; i++)
    {
        char expected[CCHDEVICENAME];
        snprintf (expected, sizeof (expected), "DISPLAY%u", devices[i]);
        if (strcmp (expected, name) == 0)
        {
            snprintf (out->DeviceID, sizeof (out->DeviceID), "monitor-interface-%u", i);
            return TRUE;
        }
    }
    assert (false);
    return FALSE;
}
static BOOL
fake_enumerate (HDC dc, LPCRECT clip, MONITORENUMPROC callback, LPARAM context)
{
    (void)dc;
    (void)clip;
    if (capture_failed)
        return FALSE;
    for (unsigned slot = 0; slot < 2; slot++)
        if (connected[order[slot]]
            && !callback (monitor_handle (order[slot]), NULL, NULL, context))
            return FALSE;
    return TRUE;
}
static HMONITOR
fake_from_window (HWND window, DWORD flags)
{
    (void)window;
    assert (flags == MONITOR_DEFAULTTONEAREST);
    return monitor_handle (window_monitor);
}
static ULONGLONG
fake_tick_count (void)
{
    return now;
}

#define GetMonitorInfoA fake_monitor_info
#define EnumDisplayDevicesA fake_display_devices
#define EnumDisplayMonitors fake_enumerate
#define MonitorFromWindow fake_from_window
#define GetTickCount64 fake_tick_count
#include "../../src/platform/windows/workspace.c"

int
main (void)
{
    char name[64];
    snprintf (name, sizeof (name), "GridFluxMonitorTest%lu", GetCurrentProcessId ());
    HDESK previous = GetThreadDesktop (GetCurrentThreadId ());
    HDESK desktop = CreateDesktopA (name, NULL, NULL, 0, GENERIC_ALL, NULL);
    assert (desktop && SetThreadDesktop (desktop));
    gf_windows_platform_data_t data = { 0 };
    gf_platform_t platform = { .platform_data = &data };
    assert (gf_monitor_init (&platform) == GF_SUCCESS);
    assert (gf_monitor_poll (&platform));
    assert (data.monitor_count == 2 && data.enumerated_monitor_count == 2);
    order[0] = 1;
    order[1] = 0;
    generation++;
    devices[0] = 7;
    devices[1] = 3;
    assert (gf_monitor_poll (&platform));
    for (unsigned i = 0; i < 2; i++)
    {
        window_monitor = i;
        assert (gf_monitor_from_window (&platform, (HWND)(uintptr_t)1) == i);
        gf_rect_t bounds;
        assert (gf_screen_get_bounds_for_monitor (NULL, i, &bounds) == GF_SUCCESS);
        assert (bounds.x == areas[i].x && bounds.width == areas[i].width);
    }
    // Power events preserve the accepted topology while a display reconnects.
    data.resize_state.phase = GF_RESIZE_ACTIVE;
    data.resize_state.pending = true;
    SendMessage (data.monitor_window, WM_POWERBROADCAST, PBT_APMSUSPEND, 0);
    assert (data.resize_state.phase == GF_RESIZE_IDLE && !data.resize_state.pending);
    assert (!gf_monitor_poll (&platform));
    connected[1] = false;
    SendMessage (data.monitor_window, WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, 0);
    assert (!gf_monitor_poll (&platform));
    for (unsigned step = 0; step < 5; step++)
    {
        now += 1500;
        assert (!gf_monitor_poll (&platform));
        assert (data.monitor_count == 2);
    }
    connected[1] = true;
    generation++;
    data.resize_state.phase = GF_RESIZE_COMPLETE;
    data.resize_state.pending = true;
    assert (!gf_monitor_poll (&platform));
    now += 1001;
    assert (gf_monitor_poll (&platform));
    assert (data.resize_state.phase == GF_RESIZE_IDLE && !data.resize_state.pending);
    assert (data.monitor_snapshot.handles[1] == monitor_handle (1));
    // Coordinate changes keep identity, including negative monitor origins.
    areas[0].x = -1920;
    areas[1].x = 0;
    assert (!gf_monitor_poll (&platform));
    now += 1001;
    assert (gf_monitor_poll (&platform));
    assert (data.monitors[0].bounds.x == -1920 && data.monitors[1].bounds.x == 0);
    // An actual unplug eventually resumes; its slot is reserved for reconnect.
    connected[0] = false;
    assert (!gf_monitor_poll (&platform));
    for (unsigned step = 0; step < 3; step++)
    {
        now += 1001;
        bool ready = gf_monitor_poll (&platform);
        assert (ready == (step == 2));
    }
    assert (data.monitor_count == 1 && data.enumerated_monitor_count == 2);
    gf_rect_t bounds;
    assert (gf_screen_get_bounds_for_monitor (NULL, 0, &bounds)
            == GF_ERROR_DISPLAY_CONNECTION);
    window_monitor = 1;
    assert (gf_monitor_from_window (&platform, (HWND)(uintptr_t)1) == 1);
    connected[0] = true;
    assert (!gf_monitor_poll (&platform));
    now += 1001;
    assert (gf_monitor_poll (&platform));
    assert (data.monitor_snapshot.count == 2);
    // Zero monitors or failed enumeration must never commit a primary fallback.
    capture_failed = true;
    assert (!gf_monitor_poll (&platform));
    assert (data.monitor_count == 2);
    capture_failed = false;
    assert (!gf_monitor_poll (&platform));
    now += 1001;
    assert (gf_monitor_poll (&platform));
    // Standby without a broadcast is detected before any arrangement resumes.
    now += 60000;
    assert (!gf_monitor_poll (&platform));
    now += 1001;
    assert (gf_monitor_poll (&platform));
    gf_monitor_cleanup (&platform);
    assert (!data.monitor_window && !monitor_data);
    assert (SetThreadDesktop (previous) && CloseDesktop (desktop));
    puts ("Windows monitor identity and sleep/wake regression tests passed");
    return 0;
}
