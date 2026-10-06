#include "window_state.h"

#include <dwmapi.h>

static const char *const gf_window_state_inset_props[4]
    = { "GridFlux.MaximizedLeft", "GridFlux.MaximizedTop", "GridFlux.MaximizedRight",
        "GridFlux.MaximizedBottom" };

void
gf_window_state_reset (HWND window)
{
    RemovePropA (window, GF_WINDOW_STATE_FILL_PROP);
    RemovePropA (window, GF_WINDOW_STATE_DPI_PROP);
    RemovePropA (window, GF_WINDOW_STATE_REGION_PROP);
    for (int i = 0; i < 4; i++)
    {
        RemovePropA (window, gf_window_state_inset_props[i]);
    }
}

static BOOL
gf_window_state_clip (HWND window, const MONITORINFO *monitor, const RECT *rect,
                      const RECT *previous, BOOL fill_monitor)
{
    // Custom frames can keep clipping to rcWork after their HWND
    // expands. Only replace that exact rectangular work-area mask; preserve
    // application-defined shapes and windows without a custom region.
    RECT current;
    int kind = GetWindowRgnBox (window, &current);
    BOOL owned = GetPropA (window, GF_WINDOW_STATE_REGION_PROP) != NULL;
    RECT work = monitor->rcWork, full = monitor->rcMonitor;
    RECT old_work = work, old_full = full;
    OffsetRect (&work, -rect->left, -rect->top);
    OffsetRect (&full, -rect->left, -rect->top);
    OffsetRect (&old_work, -previous->left, -previous->top);
    OffsetRect (&old_full, -previous->left, -previous->top);
    // A procedure may leave its existing region unchanged during the resize.
    // Its coordinates then still refer to the previous window origin.
    if (kind != SIMPLEREGION
        || (!EqualRect (&current, &work) && !EqualRect (&current, &old_work)
            && !(owned
                 && (EqualRect (&current, &full) || EqualRect (&current, &old_full)))))
    {
        RemovePropA (window, GF_WINDOW_STATE_REGION_PROP);
        return TRUE;
    }
    if (!fill_monitor && !owned)
        return TRUE;
    const RECT *target = fill_monitor ? &full : &work;
    if (EqualRect (&current, target))
        return TRUE;
    HRGN region = CreateRectRgnIndirect (target);
    if (!region)
        return FALSE;
    if (fill_monitor
        && !SetPropA (window, GF_WINDOW_STATE_REGION_PROP, (HANDLE)(INT_PTR)1))
    {
        DeleteObject (region);
        return FALSE;
    }
    // SetWindowRgn owns the region on success, including across processes.
    if (!SetWindowRgn (window, region, TRUE))
    {
        DeleteObject (region);
        return FALSE;
    }
    return GetWindowRgnBox (window, &current) == SIMPLEREGION
           && EqualRect (&current, target);
}

static BOOL
gf_window_state_frame (HWND window, const RECT *rect, const MONITORINFO *monitor,
                       int *insets)
{
    UINT dpi = GetDpiForWindow (window);
    if (!dpi)
    {
        dpi = 96;
    }
    UINT saved_dpi = (UINT)(UINT_PTR)GetPropA (window, GF_WINDOW_STATE_DPI_PROP);
    if (saved_dpi)
    {
        for (int i = 0; i < 4; i++)
        {
            INT_PTR value = (INT_PTR)GetPropA (window, gf_window_state_inset_props[i]);
            if (!value)
            {
                return FALSE;
            }
            insets[i] = MulDiv ((int)value - 1, (int)dpi, (int)saved_dpi);
        }
        return TRUE;
    }

    // Capture the invisible maximized frame once. DWM updates asynchronously;
    // re-reading its old bounds after a resize would repeatedly enlarge the app.
    RECT visible;
    BOOL have_frame = SUCCEEDED (DwmGetWindowAttribute (
        window, DWMWA_EXTENDED_FRAME_BOUNDS, &visible, sizeof (visible)));
    int border_x = GetSystemMetricsForDpi (SM_CXSIZEFRAME, dpi)
                   + GetSystemMetricsForDpi (SM_CXPADDEDBORDER, dpi);
    int border_y = GetSystemMetricsForDpi (SM_CYSIZEFRAME, dpi)
                   + GetSystemMetricsForDpi (SM_CXPADDEDBORDER, dpi);
    int observed[4] = { have_frame ? visible.left - rect->left : border_x,
                        have_frame ? visible.top - rect->top : border_y,
                        have_frame ? rect->right - visible.right : border_x,
                        have_frame ? rect->bottom - visible.bottom : border_y };
    // A native maximized placement surrounds rcWork with its frame padding.
    // This is more reliable than DWM bounds for custom non-client frames.
    int work_insets[4]
        = { monitor->rcWork.left - rect->left, monitor->rcWork.top - rect->top,
            rect->right - monitor->rcWork.right, rect->bottom - monitor->rcWork.bottom };
    BOOL native_placement = TRUE;
    for (int i = 0; i < 4; i++)
    {
        int border = i % 2 ? border_y : border_x;
        if (work_insets[i] < 0 || work_insets[i] > border + 1)
            native_placement = FALSE;
    }
    for (int i = 0; i < 4; i++)
    {
        int fallback = i % 2 ? border_y : border_x;
        insets[i] = native_placement                         ? work_insets[i]
                    : observed[i] >= 0 && observed[i] <= 128 ? observed[i]
                                                             : fallback;
        if (!SetPropA (window, gf_window_state_inset_props[i],
                       (HANDLE)(INT_PTR)(insets[i] + 1)))
        {
            gf_window_state_reset (window);
            return FALSE;
        }
    }
    if (!SetPropA (window, GF_WINDOW_STATE_DPI_PROP, (HANDLE)(UINT_PTR)dpi))
    {
        gf_window_state_reset (window);
        return FALSE;
    }
    return TRUE;
}

static BOOL
gf_window_state_resize (HWND window, const RECT *target, RECT *previous)
{
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING;
    if (!SetWindowPos (window, NULL, target->left, target->top,
                       target->right - target->left, target->bottom - target->top, flags))
        return FALSE;
    RECT actual;
    if (!GetWindowRect (window, &actual))
        return FALSE;
    if (EqualRect (&actual, target))
        return TRUE;
    *previous = actual;

    // Some window procedures re-clamp any maximized resize to rcWork. Resize
    // with the maximize style temporarily cleared, then retain the native
    // maximize state and repair its saved normal placement without activation.
    if (IsIconic (window) || !IsZoomed (window))
        return FALSE;
    WINDOWPLACEMENT placement = { .length = sizeof (placement) };
    if (!GetWindowPlacement (window, &placement))
        return FALSE;
    BOOL visible = IsWindowVisible (window);
    LONG_PTR style = GetWindowLongPtr (window, GWL_STYLE);
    if (!SetWindowLongPtr (window, GWL_STYLE, style & ~WS_MAXIMIZE))
        return FALSE;
    BOOL resized = SetWindowPos (window, NULL, target->left, target->top,
                                 target->right - target->left,
                                 target->bottom - target->top, flags);
    BOOL maximized = SetWindowLongPtr (window, GWL_STYLE,
                                       GetWindowLongPtr (window, GWL_STYLE) | WS_MAXIMIZE)
                     != 0;
    // SW_SHOWNA retains the current show state; SW_SHOWNOACTIVATE would restore
    // a maximized app. Hidden windows must remain hidden during recovery.
    placement.showCmd = visible ? SW_SHOWNA : SW_HIDE;
    BOOL saved = SetWindowPlacement (window, &placement);
    return resized && maximized && saved && IsZoomed (window)
           && GetWindowRect (window, &actual) && EqualRect (&actual, target);
}

BOOL
gf_window_state_apply (HWND window, BOOL fill_monitor)
{
    BOOL owned = GetPropA (window, GF_WINDOW_STATE_FILL_PROP) != NULL;
    if (!fill_monitor && !owned)
    {
        return TRUE;
    }
    if (!IsWindow (window) || IsIconic (window) || !IsZoomed (window))
    {
        if (!fill_monitor)
        {
            gf_window_state_reset (window);
        }
        return TRUE;
    }
    MONITORINFO monitor = { .cbSize = sizeof (monitor) };
    HMONITOR destination = MonitorFromWindow (window, MONITOR_DEFAULTTONEAREST);
    RECT current;
    if (!GetMonitorInfo (destination, &monitor) || !GetWindowRect (window, &current))
    {
        return FALSE;
    }
    const RECT *bounds = fill_monitor ? &monitor.rcMonitor : &monitor.rcWork;
    if (fill_monitor && !owned && EqualRect (&monitor.rcMonitor, &monitor.rcWork))
    {
        return TRUE;
    }
    int insets[4];
    if (!gf_window_state_frame (window, &current, &monitor, insets))
    {
        return FALSE;
    }
    RECT target = { bounds->left - insets[0], bounds->top - insets[1],
                    bounds->right + insets[2], bounds->bottom + insets[3] };
    if (fill_monitor && !SetPropA (window, GF_WINDOW_STATE_FILL_PROP, (HANDLE)(INT_PTR)1))
    {
        gf_window_state_reset (window);
        return FALSE;
    }
    if (!EqualRect (&current, &target))
    {
        if (!gf_window_state_resize (window, &target, &current))
        {
            return FALSE;
        }
    }
    if (!gf_window_state_clip (window, &monitor, &target, &current, fill_monitor))
        return FALSE;
    RECT actual;
    if (!GetWindowRect (window, &actual) || !EqualRect (&actual, &target))
        return FALSE;
    if (!fill_monitor)
    {
        gf_window_state_reset (window);
    }
    return TRUE;
}

static BOOL CALLBACK
gf_window_state_restore_callback (HWND window, LPARAM context)
{
    (void)context;
    if (GetPropA (window, GF_WINDOW_STATE_FILL_PROP))
    {
        gf_window_state_apply (window, FALSE);
    }
    return TRUE;
}

void
gf_window_state_restore (void)
{
    EnumWindows (gf_window_state_restore_callback, 0);
}
