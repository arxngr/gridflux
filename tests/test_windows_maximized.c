#include "platform/windows/internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static RECT actual[4]
    = { { 0 }, { -8, -8, 1928, 1048 }, { -1292, -172, 12, 836 }, { 100, 100, 600, 500 } };
static RECT frame[4]
    = { { 0 }, { 0, 0, 1920, 1040 }, { -1280, -160, 0, 824 }, { 100, 100, 600, 500 } };
static MONITORINFO monitors[2];
static bool maximized[4] = { false, true, true, false };
static bool minimized[4];
static uint32_t physical[4] = { 0, 0, 1, 0 };
static UINT dpi[4] = { 0, 96, 144, 96 };
static HANDLE properties[4][7];
static uint32_t writes[4];
static bool reject_write, ignore_write;
static RECT clips[4];
static int clip_kind[4];
static uint32_t clip_writes[4];
static bool browser_clip;

static unsigned
index_of (HWND window)
{
    unsigned i = (unsigned)(uintptr_t)window;
    assert (i > 0 && i < 4);
    return i;
}
static unsigned
property_index (LPCSTR name)
{
    const char *keys[7] = { "GridFlux.ExpandedMaximized", "GridFlux.MaximizedDpi",
                            "GridFlux.MaximizedLeft",     "GridFlux.MaximizedTop",
                            "GridFlux.MaximizedRight",    "GridFlux.MaximizedBottom",
                            "GridFlux.MaximizedRegion" };
    for (unsigned i = 0; i < 7; i++)
    {
        if (strcmp (name, keys[i]) == 0)
        {
            return i;
        }
    }
    assert (false);
    return 0;
}
static HANDLE
fake_get_prop (HWND window, LPCSTR name)
{
    return properties[index_of (window)][property_index (name)];
}
static BOOL
fake_set_prop (HWND window, LPCSTR name, HANDLE value)
{
    properties[index_of (window)][property_index (name)] = value;
    return TRUE;
}
static HANDLE
fake_remove_prop (HWND window, LPCSTR name)
{
    HANDLE *value = &properties[index_of (window)][property_index (name)];
    HANDLE previous = *value;
    *value = NULL;
    return previous;
}
static BOOL
fake_window (HWND window)
{
    return window != NULL;
}
static BOOL
fake_zoomed (HWND window)
{
    return maximized[index_of (window)];
}
static BOOL
fake_iconic (HWND window)
{
    return minimized[index_of (window)];
}
static UINT
fake_dpi (HWND window)
{
    return dpi[index_of (window)];
}
static BOOL
fake_rect (HWND window, LPRECT out)
{
    *out = actual[index_of (window)];
    return TRUE;
}
static HMONITOR
fake_monitor (HWND window, DWORD flags)
{
    assert (flags == MONITOR_DEFAULTTONEAREST);
    return (HMONITOR)(uintptr_t)(physical[index_of (window)] + 1);
}
static BOOL
fake_monitor_info (HMONITOR monitor, LPMONITORINFO out)
{
    *out = monitors[(uintptr_t)monitor - 1];
    return TRUE;
}
static HRESULT
fake_frame (HWND window, DWORD attribute, PVOID out, DWORD size)
{
    assert (attribute == DWMWA_EXTENDED_FRAME_BOUNDS && size == sizeof (RECT));
    *(RECT *)out = frame[index_of (window)];
    return S_OK;
}
static int
fake_metrics (int metric, UINT current_dpi)
{
    return metric == SM_CXPADDEDBORDER ? 0 : (int)(8 * current_dpi / 96);
}
static BOOL
fake_position (HWND window, HWND after, int x, int y, int width, int height, UINT flags)
{
    (void)after;
    assert (flags == (SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING));
    unsigned i = index_of (window);
    assert (maximized[i] && !minimized[i]);
    writes[i]++;
    if (reject_write)
    {
        return FALSE;
    }
    if (!ignore_write)
    {
        actual[i] = (RECT){ x, y, x + width, y + height };
        if (browser_clip)
        {
            clips[i] = monitors[physical[i]].rcWork;
            OffsetRect (&clips[i], -x, -y);
            clip_kind[i] = SIMPLEREGION;
        }
    }
    return TRUE;
}
static int
fake_region_box (HWND window, LPRECT rect)
{
    unsigned i = index_of (window);
    *rect = clips[i];
    return clip_kind[i];
}
static int
fake_set_region (HWND window, HRGN region, BOOL redraw)
{
    (void)redraw;
    unsigned i = index_of (window);
    clip_writes[i]++;
    clip_kind[i] = GetRgnBox (region, &clips[i]);
    DeleteObject (region);
    return TRUE;
}
static BOOL
fake_enum_windows (WNDENUMPROC callback, LPARAM context)
{
    for (uintptr_t i = 1; i <= 3; i++)
    {
        assert (callback ((HWND)i, context));
    }
    return TRUE;
}

#define GetPropA fake_get_prop
#define SetPropA fake_set_prop
#define RemovePropA fake_remove_prop
#define IsWindow fake_window
#define IsZoomed fake_zoomed
#define IsIconic fake_iconic
#define GetDpiForWindow fake_dpi
#define GetWindowRect fake_rect
#define MonitorFromWindow fake_monitor
#define GetMonitorInfoA fake_monitor_info
#define DwmGetWindowAttribute fake_frame
#define GetSystemMetricsForDpi fake_metrics
#define SetWindowPos fake_position
#define EnumWindows fake_enum_windows
#define GetWindowRgnBox fake_region_box
#define SetWindowRgn fake_set_region
#include "../src/platform/windows/window_state.c"

static void
expect_rect (unsigned i, LONG left, LONG top, LONG right, LONG bottom)
{
    RECT expected = { left, top, right, bottom };
    assert (EqualRect (&actual[i], &expected));
}
int
main (void)
{
    monitors[0] = (MONITORINFO){ .cbSize = sizeof (MONITORINFO),
                                 .rcMonitor = { 0, 0, 1920, 1080 },
                                 .rcWork = { 0, 0, 1920, 1040 } };
    monitors[1] = (MONITORINFO){ .cbSize = sizeof (MONITORINFO),
                                 .rcMonitor = { -1280, -160, 0, 864 },
                                 .rcWork = { -1280, -160, 0, 824 } };
    HWND main = (HWND)(uintptr_t)1, external = (HWND)(uintptr_t)2;
    assert (gf_window_state_apply (main, TRUE));
    expect_rect (1, -8, -8, 1928, 1088);
    expect_rect (2, -1292, -172, 12, 836);
    for (unsigned i = 0; i < 30; i++)
    {
        // DWM deliberately retains its pre-resize frame: polling must remain stable.
        assert (gf_window_state_apply (main, TRUE));
    }
    assert (writes[1] == 1 && maximized[1] && !minimized[1]);
    assert (gf_window_state_apply (external, TRUE));
    expect_rect (2, -1292, -172, 12, 876);
    dpi[2] = 192;
    assert (gf_window_state_apply (external, TRUE));
    expect_rect (2, -1296, -176, 16, 880);
    assert (gf_window_state_apply ((HWND)(uintptr_t)3, TRUE));
    assert (writes[3] == 0 && !maximized[3]);

    assert (gf_window_state_apply (main, FALSE));
    expect_rect (1, -8, -8, 1928, 1048);
    expect_rect (2, -1296, -176, 16, 880);
    assert (!fake_get_prop (main, GF_WINDOW_STATE_FILL_PROP));
    gf_window_state_restore ();
    expect_rect (2, -1296, -176, 16, 840);
    assert (!fake_get_prop (external, GF_WINDOW_STATE_FILL_PROP));

    // A failed/clamped write must be observable and remain recoverable on Stop.
    reject_write = true;
    assert (!gf_window_state_apply (main, TRUE));
    reject_write = false;
    ignore_write = true;
    assert (!gf_window_state_apply (main, TRUE));
    ignore_write = false;
    gf_window_state_restore ();
    expect_rect (1, -8, -8, 1928, 1048);
    assert (!fake_get_prop (main, GF_WINDOW_STATE_FILL_PROP));

    // Cleanup never revives iconified apps or resizes a user's restored normal app.
    assert (gf_window_state_apply (main, TRUE));
    minimized[1] = true;
    uint32_t previous_writes = writes[1];
    gf_window_state_restore ();
    assert (writes[1] == previous_writes && minimized[1]);
    assert (!fake_get_prop (main, GF_WINDOW_STATE_FILL_PROP));
    minimized[1] = false;
    assert (gf_window_state_apply (main, TRUE));
    maximized[1] = false;
    actual[1] = (RECT){ 100, 100, 600, 500 };
    previous_writes = writes[1];
    gf_window_state_restore ();
    expect_rect (1, 100, 100, 600, 500);
    assert (writes[1] == previous_writes);

    // An app can report the full HWND rectangle while still visibly clipping
    // out the taskbar gap. Handle re-clipping without another geometry write.
    maximized[1] = true;
    actual[1] = (RECT){ -8, -8, 1928, 1048 };
    frame[1] = (RECT){ 0, 0, 1920, 1040 };
    browser_clip = true;
    assert (gf_window_state_apply (main, TRUE));
    RECT full_clip = { 8, 8, 1928, 1088 };
    RECT work_clip = { 8, 8, 1928, 1048 };
    assert (EqualRect (&clips[1], &full_clip));
    uint32_t geometry_writes = writes[1], region_writes = clip_writes[1];
    for (unsigned i = 0; i < 30; i++)
        assert (gf_window_state_apply (main, TRUE));
    assert (writes[1] == geometry_writes && clip_writes[1] == region_writes);
    clips[1] = work_clip;
    assert (gf_window_state_apply (main, TRUE));
    assert (writes[1] == geometry_writes && EqualRect (&clips[1], &full_clip));
    gf_window_state_restore ();
    assert (EqualRect (&clips[1], &work_clip));
    assert (!fake_get_prop (main, GF_WINDOW_STATE_REGION_PROP));

    // Custom shapes and unrelated rectangular masks belong to the app.
    browser_clip = false;
    clip_kind[1] = COMPLEXREGION;
    region_writes = clip_writes[1];
    assert (gf_window_state_apply (main, TRUE));
    assert (clip_writes[1] == region_writes);
    clip_kind[1] = SIMPLEREGION;
    clips[1] = (RECT){ 1, 2, 300, 400 };
    assert (gf_window_state_apply (main, TRUE));
    assert (clip_writes[1] == region_writes);
    gf_window_state_restore ();
    puts ("Maximized gap fill, DPI, monitor isolation, and Stop recovery passed");
    return 0;
}
