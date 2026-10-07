#include "../../utils/logger.h"
#include "internal.h"
#include <string.h>

uint32_t
gf_workspace_get_count (gf_display_t display)
{
    (void)display;
    return 1;
}

gf_err_t
gf_screen_get_bounds (gf_display_t display, gf_rect_t *bounds)
{
    (void)display;

    if (!bounds)
        return GF_ERROR_INVALID_PARAMETER;

    // Use the system work area — Win32 already excludes the taskbar,
    // auto-hide bars, and accounts for DPI scaling.
    RECT wa;
    if (SystemParametersInfo (SPI_GETWORKAREA, 0, &wa, 0))
    {
        bounds->x = wa.left;
        bounds->y = wa.top;
        bounds->width = (gf_dimension_t)(wa.right - wa.left);
        bounds->height = (gf_dimension_t)(wa.bottom - wa.top);
        return GF_SUCCESS;
    }

    // Last-resort fallback: raw virtual screen with no taskbar compensation
    bounds->x = GetSystemMetrics (SM_XVIRTUALSCREEN);
    bounds->y = GetSystemMetrics (SM_YVIRTUALSCREEN);
    bounds->width = (gf_dimension_t)GetSystemMetrics (SM_CXVIRTUALSCREEN);
    bounds->height = (gf_dimension_t)GetSystemMetrics (SM_CYVIRTUALSCREEN);
    return GF_SUCCESS;
}

// ── Monitor enumeration callback ──

// Reserve device identities independently of enumeration order and coordinates.
static gf_windows_platform_data_t *monitor_data;

static BOOL CALLBACK
monitor_enum_proc (HMONITOR hmon, HDC hdc, LPRECT lprc, LPARAM lparam)
{
    (void)hdc;
    (void)lprc;
    gf_monitor_snapshot_t *snapshot = (gf_monitor_snapshot_t *)lparam;
    MONITORINFOEXA mi = { .cbSize = sizeof (mi) };
    if (!GetMonitorInfoA (hmon, (MONITORINFO *)&mi))
        return TRUE;
    DISPLAY_DEVICEA device = { .cb = sizeof (device) };
    char identity[128] = { 0 };
    if (EnumDisplayDevicesA (mi.szDevice, 0, &device, EDD_GET_DEVICE_INTERFACE_NAME)
        && device.DeviceID[0])
        memcpy (identity, device.DeviceID, sizeof (identity));
    else
    {
        for (uint32_t i = 0; i < snapshot->count; i++)
            if (lstrcmpiA (snapshot->devices[i], mi.szDevice) == 0)
                memcpy (identity, snapshot->identities[i], sizeof (identity));
        if (!identity[0])
            memcpy (identity, mi.szDevice, sizeof (mi.szDevice));
    }
    identity[sizeof (identity) - 1] = '\0';
    uint32_t id = 0;
    while (id < snapshot->count && lstrcmpiA (snapshot->identities[id], identity))
        id++;
    if (id == GF_MAX_MONITORS)
        return FALSE;
    if (id == snapshot->count)
    {
        memcpy (snapshot->identities[id], identity, sizeof (identity));
        snapshot->count++;
    }
    snapshot->handles[id] = hmon;
    memcpy (snapshot->devices[id], mi.szDevice, sizeof (mi.szDevice));
    snapshot->monitors[id] = (gf_monitor_t){
        .id = id,
        .bounds = { mi.rcWork.left, mi.rcWork.top,
                    (gf_dimension_t)(mi.rcWork.right - mi.rcWork.left),
                    (gf_dimension_t)(mi.rcWork.bottom - mi.rcWork.top) },
        .full_bounds = { mi.rcMonitor.left, mi.rcMonitor.top,
                         (gf_dimension_t)(mi.rcMonitor.right - mi.rcMonitor.left),
                         (gf_dimension_t)(mi.rcMonitor.bottom - mi.rcMonitor.top) },
        .is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0
    };
    snapshot->connected++;
    return TRUE;
}

static bool
monitor_capture (gf_windows_platform_data_t *data, gf_monitor_snapshot_t *snapshot)
{
    *snapshot = data->monitor_snapshot;
    snapshot->connected = 0;
    memset (snapshot->handles, 0, sizeof (snapshot->handles));
    memset (snapshot->monitors, 0, sizeof (snapshot->monitors));
    for (uint32_t i = 0; i < snapshot->count; i++)
        snapshot->monitors[i].id = i;
    return EnumDisplayMonitors (NULL, NULL, monitor_enum_proc, (LPARAM)snapshot)
           && snapshot->connected > 0;
}

static bool
monitor_snapshot_equal (const gf_monitor_snapshot_t *a, const gf_monitor_snapshot_t *b)
{
    if (a->count != b->count || a->connected != b->connected
        || memcmp (a->identities, b->identities, sizeof (a->identities)) != 0)
        return false;
    for (uint32_t i = 0; i < a->count; i++)
        if (a->monitors[i].is_primary != b->monitors[i].is_primary
            || memcmp (&a->monitors[i].bounds, &b->monitors[i].bounds, sizeof (gf_rect_t))
                   != 0
            || memcmp (&a->monitors[i].full_bounds, &b->monitors[i].full_bounds,
                       sizeof (gf_rect_t))
                   != 0)
            return false;
    return true;
}

static void
monitor_commit (gf_windows_platform_data_t *data, const gf_monitor_snapshot_t *snapshot)
{
    data->monitor_snapshot = *snapshot;
    memcpy (data->monitors, snapshot->monitors, sizeof (data->monitors));
    data->enumerated_monitor_count = snapshot->count;
    data->monitor_count = (int)snapshot->connected;
    monitor_data = data;
}

static LRESULT CALLBACK
monitor_window_proc (HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE)
        SetWindowLongPtr (window, GWLP_USERDATA,
                          (LONG_PTR)((CREATESTRUCT *)lparam)->lpCreateParams);
    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)GetWindowLongPtr (window, GWLP_USERDATA);
    if (data && (message == WM_DISPLAYCHANGE || message == WM_POWERBROADCAST))
    {
        ULONGLONG now = GetTickCount64 ();
        if (message == WM_DISPLAYCHANGE)
        {
            data->monitor_pending = true;
            data->monitor_settle_until = now + 1000;
            if (!data->monitor_reconnect_until)
                data->monitor_reconnect_until = now + 3000;
        }
        else if (wparam == PBT_APMSUSPEND)
        {
            memset (&data->resize_state, 0, sizeof (data->resize_state));
            data->monitor_suspended = true;
            data->monitor_pending = true;
        }
        else if (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND)
        {
            data->monitor_suspended = false;
            data->monitor_pending = true;
            data->monitor_settle_until = now + 1000;
            data->monitor_reconnect_until = now + 10000;
        }
        if (message == WM_POWERBROADCAST)
            return TRUE;
    }
    return DefWindowProc (window, message, wparam, lparam);
}

gf_err_t
gf_monitor_init (gf_platform_t *platform)
{
    gf_windows_platform_data_t *data = platform->platform_data;
    WNDCLASSA cls = { .lpfnWndProc = monitor_window_proc,
                      .hInstance = GetModuleHandle (NULL),
                      .lpszClassName = "GridFluxMonitor" };
    if (!RegisterClassA (&cls) && GetLastError () != ERROR_CLASS_ALREADY_EXISTS)
        return GF_ERROR_PLATFORM_ERROR;
    // A hidden top-level window receives broadcasts even without borders or
    // keyboard hooks. Message-only windows don't receive these broadcasts.
    data->monitor_window = CreateWindowExA (0, cls.lpszClassName, "", WS_POPUP, 0, 0, 0,
                                            0, NULL, NULL, cls.hInstance, data);
    gf_monitor_snapshot_t snapshot;
    if (!data->monitor_window || !monitor_capture (data, &snapshot))
    {
        gf_monitor_cleanup (platform);
        return GF_ERROR_DISPLAY_CONNECTION;
    }
    monitor_commit (data, &snapshot);
    data->monitor_last_poll = GetTickCount64 ();
    return GF_SUCCESS;
}

void
gf_monitor_cleanup (gf_platform_t *platform)
{
    gf_windows_platform_data_t *data = platform->platform_data;
    if (data->monitor_window)
        DestroyWindow (data->monitor_window);
    data->monitor_window = NULL;
    if (monitor_data == data)
        monitor_data = NULL;
}

bool
gf_monitor_poll (gf_platform_t *platform)
{
    gf_windows_platform_data_t *data = platform->platform_data;
    MSG message;
    for (int i = 0; i < 32 && PeekMessage (&message, NULL, 0, 0, PM_REMOVE); i++)
    {
        TranslateMessage (&message);
        DispatchMessage (&message);
    }
    ULONGLONG now = GetTickCount64 ();
    // Cover standby implementations which omit a power broadcast.
    if (data->monitor_last_poll && now - data->monitor_last_poll > 2000)
    {
        data->monitor_pending = true;
        data->monitor_settle_until = now + 1000;
        data->monitor_reconnect_until = now + 10000;
    }
    data->monitor_last_poll = now;
    if (data->monitor_suspended)
        return false;
    gf_monitor_snapshot_t snapshot;
    if (!monitor_capture (data, &snapshot))
    {
        data->monitor_pending = true;
        data->monitor_settle_until = now + 1000;
        return false;
    }
    bool changed = !monitor_snapshot_equal (&snapshot, &data->monitor_snapshot);
    if (changed && !data->monitor_pending)
    {
        data->monitor_pending = true;
        data->monitor_settle_until = now + 1000;
        data->monitor_reconnect_until = now + 3000;
    }
    if (!data->monitor_pending)
    {
        monitor_commit (data, &snapshot); // Refresh changed HMONITOR handles too.
        return true;
    }
    if (!monitor_snapshot_equal (&snapshot, &data->pending_monitors))
    {
        data->pending_monitors = snapshot;
        data->monitor_settle_until = now + 1000;
    }
    bool missing = false;
    for (uint32_t i = 0; i < data->monitor_snapshot.count; i++)
        if (data->monitor_snapshot.handles[i] && !snapshot.handles[i])
            missing = true;
    if (now < data->monitor_settle_until
        || (missing && now < data->monitor_reconnect_until))
        return false;
    GUITHREADINFO interaction = { .cbSize = sizeof (interaction) };
    if (!GetGUIThreadInfo (0, &interaction) || !(interaction.flags & GUI_INMOVESIZE))
        memset (&data->resize_state, 0, sizeof (data->resize_state));
    monitor_commit (data, &snapshot);
    data->monitor_pending = false;
    data->monitor_reconnect_until = 0;
    return true;
}

uint32_t
gf_monitor_get_count (gf_platform_t *platform)
{
    if (!platform || !platform->platform_data)
        return 0;
    gf_windows_platform_data_t *data = platform->platform_data;
    return data->enumerated_monitor_count;
}

gf_err_t
gf_monitor_enumerate (gf_platform_t *platform, gf_monitor_t *monitors, uint32_t *count)
{
    if (!platform || !platform->platform_data || !monitors || !count)
        return GF_ERROR_INVALID_PARAMETER;
    gf_windows_platform_data_t *data = platform->platform_data;
    if (!data->enumerated_monitor_count || *count < data->enumerated_monitor_count)
        return GF_ERROR_DISPLAY_CONNECTION;
    *count = data->enumerated_monitor_count;
    memcpy (monitors, data->monitors, *count * sizeof (*monitors));
    return GF_SUCCESS;
}

gf_monitor_id_t
gf_monitor_from_window (gf_platform_t *platform, gf_handle_t window)
{
    if (!platform || !platform->platform_data || !window)
        return GF_MONITOR_SHARED;
    gf_windows_platform_data_t *data = platform->platform_data;
    HMONITOR monitor = MonitorFromWindow (window, MONITOR_DEFAULTTONEAREST);
    for (uint32_t i = 0; i < data->monitor_snapshot.count; i++)
        if (monitor && data->monitor_snapshot.handles[i] == monitor)
            return i;
    return GF_MONITOR_SHARED; // Unknown must never silently become monitor 0.
}

gf_err_t
gf_screen_get_bounds_for_monitor (gf_display_t display, gf_monitor_id_t monitor_id,
                                  gf_rect_t *bounds)
{
    (void)display;
    if (!bounds || !monitor_data || monitor_id >= monitor_data->enumerated_monitor_count
        || !monitor_data->monitor_snapshot.handles[monitor_id])
        return GF_ERROR_DISPLAY_CONNECTION;
    *bounds = monitor_data->monitors[monitor_id].bounds;
    return GF_SUCCESS;
}
