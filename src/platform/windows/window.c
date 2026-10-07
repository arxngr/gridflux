#include "window.h"
#include "../../utils/logger.h"
#include "../../utils/memory.h"
#include "internal.h"
#include <ctype.h>
#include <stdio.h>
#include <time.h>

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
window_state_clip (HWND window, const MONITORINFO *monitor, const RECT *rect,
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
window_state_frame (HWND window, const RECT *rect, const MONITORINFO *monitor,
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
window_state_resize (HWND window, const RECT *target, RECT *previous)
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
    if (!window_state_frame (window, &current, &monitor, insets))
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
        if (!window_state_resize (window, &target, &current))
        {
            return FALSE;
        }
    }
    if (!window_state_clip (window, &monitor, &target, &current, fill_monitor))
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
window_state_restore_callback (HWND window, LPARAM context)
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
    EnumWindows (window_state_restore_callback, 0);
}

#define MAX_WINDOWS 1024

static const char *
strcasestr (const char *haystack, const char *needle)
{
    if (!haystack || !needle)
        return NULL;
    if (*needle == '\0')
        return haystack;

    for (; *haystack != '\0'; haystack++)
    {
        if (tolower ((unsigned char)*haystack) == tolower ((unsigned char)*needle))
        {
            const char *h = haystack;
            const char *n = needle;
            while (*h != '\0' && *n != '\0'
                   && tolower ((unsigned char)*h) == tolower ((unsigned char)*n))
            {
                h++;
                n++;
            }
            if (*n == '\0')
                return haystack;
        }
    }
    return NULL;
}

// Resolve the executable file name (e.g. "code.exe") for a process id.
// Writes an empty string on failure.
static void
pid_get_exe_name (DWORD pid, char *out, size_t out_size)
{
    out[0] = '\0';

    HANDLE proc = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc)
        return;

    char path[MAX_PATH] = { 0 };
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameA (proc, 0, path, &size))
    {
        char *filename = strrchr (path, '\\');
        filename = filename ? filename + 1 : path;
        strncpy (out, filename, out_size - 1);
        out[out_size - 1] = '\0';
    }
    CloseHandle (proc);
}

// True if any of the needles occurs (case-insensitively) in haystack.
static bool
str_contains_any (const char *haystack, const char *const *needles, int count)
{
    for (int i = 0; i < count; i++)
        if (strcasestr (haystack, needles[i]) != NULL)
            return true;
    return false;
}

// Case-insensitive check that `s` ends with `suffix`.
static bool
str_ends_with_ci (const char *s, const char *suffix)
{
    size_t ls = strlen (s), lf = strlen (suffix);
    if (lf > ls)
        return false;

    const char *tail = s + (ls - lf);
    for (size_t i = 0; i < lf; i++)
        if (tolower ((unsigned char)tail[i]) != tolower ((unsigned char)suffix[i]))
            return false;
    return true;
}

static bool
window_class_is_installer (const char *class_name)
{
    static const char *const exact[] = {
        "MsiDialogCloseClass", // Windows Installer / MSI / WiX
        "TWizardForm",         // Inno Setup wizard
        "TSetupLdrWindow",     // Inno Setup loader
    };
    for (int i = 0; i < 3; i++)
        if (strcmp (class_name, exact[i]) == 0)
            return true;

    return strcasestr (class_name, "InstallShield") != NULL // InstallShield
           || strcasestr (class_name, "Nullsoft") != NULL;  // NSIS
}

static bool
window_title_is_installer (const char *title)
{
    if (str_ends_with_ci (title, " setup") || str_ends_with_ci (title, " installer"))
        return true;

    static const char *const phrases[]
        = { "setup wizard", "install wizard", "installshield", "uninstall" };
    return str_contains_any (title, phrases, 4);
}

bool
gf_window_is_installer (HWND window)
{
    char class_name[256] = { 0 };
    GetClassNameA (window, class_name, sizeof (class_name));

    char title[256] = { 0 };
    GetWindowTextA (window, title, sizeof (title) - 1);

    if (window_class_is_installer (class_name))
        return true;

    DWORD pid = 0;
    GetWindowThreadProcessId (window, &pid);
    char exe_name[MAX_PATH] = { 0 };
    pid_get_exe_name (pid, exe_name, sizeof (exe_name));

    static const char *const exe_kw[] = { "setup", "install", "uninst", "msiexec" };
    if (exe_name[0] != '\0' && str_contains_any (exe_name, exe_kw, 4))
        return true;

    return window_title_is_installer (title);
}

static bool
window_fill_info (HWND hwnd, gf_win_info_t *info)
{
    RECT rect;
    if (FAILED (DwmGetWindowAttribute (hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect,
                                       sizeof (rect)))
        && !GetWindowRect (hwnd, &rect))
        return false;

    // Enumeration allocates an uninitialised array; clear every tracked field
    // so newly added core state (and minimized/restore fields) starts defined.
    *info = (gf_win_info_t){ 0 };

    if (GetWindowTextA (hwnd, info->name, sizeof (info->name)))
        info->name[sizeof (info->name) - 1] = '\0';
    else
        info->name[0] = '\0';

    info->id = (gf_handle_t)hwnd;
    info->workspace_id = GF_FIRST_WORKSPACE_ID;
    info->geometry.x = rect.left;
    info->geometry.y = rect.top;
    info->geometry.width = (gf_dimension_t)(rect.right - rect.left);
    info->geometry.height = (gf_dimension_t)(rect.bottom - rect.top);
    info->is_maximized = gf_window_is_maximized (NULL, hwnd);
    info->is_minimized = IsIconic (hwnd);
    info->is_valid = true;
    info->last_modified = time (NULL);
    info->monitor_id = 0;
    return true;
}

gf_err_t
gf_platform_get_windows (gf_display_t display, gf_ws_id_t *workspace_id,
                         gf_win_info_t **windows, uint32_t *count)
{
    (void)display;
    (void)workspace_id;

    if (!windows || !count)
        return GF_ERROR_INVALID_PARAMETER;

    gf_win_info_t *window_list = gf_malloc (MAX_WINDOWS * sizeof (gf_win_info_t));
    if (!window_list)
        return GF_ERROR_MEMORY_ALLOCATION;

    uint32_t found_count = 0;
    HWND hwnd = GetTopWindow (NULL);

    while (hwnd && found_count < MAX_WINDOWS)
    {
        // NOTE: always advance hwnd. The previous version did `continue` on a
        // geometry-lookup failure before advancing, which could spin forever.
        if (gf_window_is_app (hwnd) && window_fill_info (hwnd, &window_list[found_count]))
            found_count++;
        hwnd = GetNextWindow (hwnd, GW_HWNDNEXT);
    }

    *windows = window_list;
    *count = found_count;

    return GF_SUCCESS;
}

gf_err_t
gf_window_get_geometry (gf_display_t display, gf_handle_t window, gf_rect_t *geometry)
{
    (void)display;

    if (!gf_window_validate (window) || !geometry)
        return GF_ERROR_INVALID_PARAMETER;

    RECT rect;
    if (SUCCEEDED (DwmGetWindowAttribute (window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect,
                                          sizeof (rect)))
        || GetWindowRect (window, &rect))
    {
        geometry->x = rect.left;
        geometry->y = rect.top;
        geometry->width = (gf_dimension_t)(rect.right - rect.left);
        geometry->height = (gf_dimension_t)(rect.bottom - rect.top);
        return GF_SUCCESS;
    }

    return GF_ERROR_PLATFORM_ERROR;
}

gf_err_t
gf_window_set_geometry (gf_display_t display, gf_handle_t window,
                        const gf_rect_t *geometry, gf_geom_flags_t flags,
                        gf_config_t *cfg)
{
    (void)display;
    (void)flags;
    (void)cfg;

    if (!gf_window_validate (window) || !geometry)
        return GF_ERROR_INVALID_PARAMETER;

    // A maximize can occur between the core's state check and this write.
    // Geometry updates must never undo it or revive an iconified window.
    if (IsZoomed (window) || IsIconic (window))
        return GF_SUCCESS;

    int new_x = geometry->x;
    int new_y = geometry->y;
    int new_w = geometry->width;
    int new_h = geometry->height;

    // Compensate for the invisible DWM shadow/border that shifts the window rect.
    // NOTE (DPI): DWMWA_EXTENDED_FRAME_BOUNDS is always in physical pixels, while
    // GetWindowRect is DPI-virtualized under system-DPI awareness. The delta below
    // is only correct when both are in the SAME coordinate space, which holds under
    // Per-Monitor-V2 awareness (enable via the app manifest <dpiAwareness> /
    // SetProcessDpiAwarenessContext in the server entry point — outside this file).
    // Until then, on monitors whose DPI differs from the system DPI this
    // subtraction mixes physical and virtualized coordinates and needs per-monitor
    // DPI conversion.
    RECT d_rect, w_rect;
    if (SUCCEEDED (DwmGetWindowAttribute (window, DWMWA_EXTENDED_FRAME_BOUNDS, &d_rect,
                                          sizeof (d_rect)))
        && GetWindowRect (window, &w_rect))
    {
        int left_border = d_rect.left - w_rect.left;
        int top_border = d_rect.top - w_rect.top;
        int right_border = w_rect.right - d_rect.right;
        int bottom_border = w_rect.bottom - d_rect.bottom;

        new_x -= left_border;
        new_y -= top_border;
        new_w += left_border + right_border;
        new_h += top_border + bottom_border;
    }

    // Use SetWindowPos with SWP_NOSENDCHANGING so that apps like Discord
    // (CEF/Electron) cannot intercept the resize via WM_WINDOWPOSCHANGING
    // and silently enforce their own minimum size.
    UINT swp_flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING;
    if (SetWindowPos (window, NULL, new_x, new_y, new_w, new_h, swp_flags) == 0)
        return GF_ERROR_PLATFORM_ERROR;

    return GF_SUCCESS;
}

bool
gf_window_is_valid (gf_display_t display, gf_handle_t window)
{
    (void)display;
    return gf_window_validate (window);
}

// True if the window's title marks it as a system/utility window to ignore.
static bool
window_title_excluded (HWND window)
{
    char title[MAX_TITLE_LENGTH];
    int len = GetWindowTextA (window, title, sizeof (title) - 1);
    if (len <= 0)
        return false;
    title[len] = '\0';

    static const char *const exact[] = { "DWM Notification Window", "GridFlux" };
    for (int i = 0; i < 2; i++)
        if (strcmp (title, exact[i]) == 0)
            return true;

    static const char *const substr[] = { "Snipping Tool", "Game Bar", "Screen Sketch" };
    for (int i = 0; i < 3; i++)
        if (strstr (title, substr[i]) != NULL)
            return true;

    return false;
}

bool
gf_window_is_excluded (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!gf_window_validate (window))
        return true;

    if (GetWindow (window, GW_OWNER) != NULL)
        return true;

    if (gf_window_is_excluded_style (window))
        return true;

    if (gf_window_is_native_fullscreen (window))
        return true;

    if (gf_window_is_cloaked (window))
        return true;

    if (gf_window_is_notification_center (window))
        return true;

    if (gf_window_is_self (display, window))
        return true;

    if (gf_window_is_installer ((HWND)window))
        return true;

    if (window_title_excluded ((HWND)window))
        return true;

    char class_name[MAX_CLASS_NAME_LENGTH];
    if (GetClassNameA (window, class_name, sizeof (class_name)))
    {
        if (gf_window_is_excluded_class (class_name))
            return true;

        char dbg_title[128] = { 0 };
        GetWindowTextA ((HWND)window, dbg_title, sizeof (dbg_title) - 1);
        GF_LOG_DEBUG ("Managing window: class='%s' title='%s'", class_name, dbg_title);
    }

    return false;
}

bool
gf_window_is_fullscreen (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!gf_window_validate (window))
        return false;

    // Reuse the monitor-aware detection in internal.c, which compares the window
    // against its actual monitor (MonitorFromWindow + GetMonitorInfo) and checks
    // GetWindowRect's return, instead of the primary monitor's GetSystemMetrics.
    return gf_window_is_native_fullscreen ((HWND)window);
}

gf_handle_t
gf_window_get_focused (gf_display_t display)
{
    (void)display;

    HWND hwnd = GetForegroundWindow ();
    if (gf_window_validate (hwnd) && gf_window_is_app (hwnd))
        return hwnd;

    return 0;
}

gf_err_t
gf_window_minimize (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!gf_window_validate (window))
        return GF_ERROR_INVALID_PARAMETER;

    if (!IsIconic (window))
        ShowWindow (window, SW_SHOWMINNOACTIVE);

    return GF_SUCCESS;
}

gf_err_t
gf_window_unminimize (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!gf_window_validate (window))
        return GF_ERROR_INVALID_PARAMETER;

    if (!IsIconic (window))
        return GF_SUCCESS;

    // Windows honors WPF_RESTORETOMAXIMIZED when restoring without activation.
    // SW_SHOWMAXIMIZED would steal an Alt-Tab selection on another monitor.
    ShowWindow (window, SW_SHOWNOACTIVATE);
    return GF_SUCCESS;
}

// Activation is explicit and is never part of restoring sibling tiles.
gf_err_t
gf_window_set_maximized (gf_display_t display, gf_handle_t window, bool maximized)
{
    (void)display;
    if (!gf_window_validate (window) || IsIconic (window))
        return GF_ERROR_INVALID_PARAMETER;

    // Restoring a transferred maximized window may use a saved rectangle on
    // its old display. Preserve the destination before touching placement.
    HMONITOR destination = MonitorFromWindow (window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor = { .cbSize = sizeof (monitor) };
    if (!GetMonitorInfo (destination, &monitor))
        return GF_ERROR_PLATFORM_ERROR;
    // A monitor transfer can change DPI and the native frame. Capture fresh
    // insets after the native restore/maximize sequence on the destination.
    gf_window_state_reset (window);
    if (IsZoomed (window))
        ShowWindow (window, SW_SHOWNOACTIVATE);

    RECT rect;
    if (!GetWindowRect (window, &rect))
        return GF_ERROR_PLATFORM_ERROR;
    if (MonitorFromWindow (window, MONITOR_DEFAULTTONEAREST) != destination)
    {
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        int available_width = monitor.rcWork.right - monitor.rcWork.left;
        int available_height = monitor.rcWork.bottom - monitor.rcWork.top;
        if (width > available_width)
            width = available_width;
        if (height > available_height)
            height = available_height;
        if (!SetWindowPos (window, NULL, monitor.rcWork.left, monitor.rcWork.top, width,
                           height, SWP_NOZORDER | SWP_NOACTIVATE))
            return GF_ERROR_PLATFORM_ERROR;
    }
    if (maximized)
        ShowWindow (window, SW_SHOWMAXIMIZED);
    return GF_SUCCESS;
}

gf_err_t
gf_window_focus (gf_display_t display, gf_handle_t window)
{
    (void)display;
    if (!gf_window_validate (window))
        return GF_ERROR_INVALID_PARAMETER;
    return SetForegroundWindow (window) ? GF_SUCCESS : GF_ERROR_PLATFORM_ERROR;
}

gf_err_t
gf_window_fill_maximized (gf_display_t display, gf_handle_t window, bool fill_monitor)
{
    (void)display;
    return gf_window_state_apply (window, fill_monitor) ? GF_SUCCESS
                                                        : GF_ERROR_PLATFORM_ERROR;
}

gf_err_t
gf_window_restore_monitor (gf_platform_t *platform, const gf_win_info_t *win,
                           const gf_rect_t *previous_bounds)
{
    if (!platform || !platform->platform_data || !win || !previous_bounds
        || !gf_window_validate (win->id))
        return GF_ERROR_INVALID_PARAMETER;
    gf_windows_platform_data_t *data = platform->platform_data;
    if (win->monitor_id >= data->monitor_snapshot.count)
        return GF_ERROR_DISPLAY_CONNECTION;
    HMONITOR destination = data->monitor_snapshot.handles[win->monitor_id];
    MONITORINFO monitor = { .cbSize = sizeof (monitor) };
    if (!destination || !GetMonitorInfo (destination, &monitor))
        return GF_ERROR_DISPLAY_CONNECTION;
    bool iconic = IsIconic (win->id) != FALSE;
    gf_rect_t bounds = data->monitors[win->monitor_id].bounds;
    if (!iconic && MonitorFromWindow (win->id, MONITOR_DEFAULTTONEAREST) == destination
        && memcmp (&bounds, previous_bounds, sizeof (bounds)) == 0)
        return GF_SUCCESS;

    gf_rect_t target = win->geometry;
    target.x += bounds.x - previous_bounds->x;
    target.y += bounds.y - previous_bounds->y;
    if (!target.width || target.width > bounds.width)
        target.width = bounds.width;
    if (!target.height || target.height > bounds.height)
        target.height = bounds.height;
    if (target.x < bounds.x)
        target.x = bounds.x;
    if (target.y < bounds.y)
        target.y = bounds.y;
    if (target.x + (int32_t)target.width > bounds.x + (int32_t)bounds.width)
        target.x = bounds.x + (int32_t)(bounds.width - target.width);
    if (target.y + (int32_t)target.height > bounds.y + (int32_t)bounds.height)
        target.y = bounds.y + (int32_t)(bounds.height - target.height);

    if (iconic)
    {
        WINDOWPLACEMENT placement = { .length = sizeof (placement) };
        if (!GetWindowPlacement (win->id, &placement))
            return GF_ERROR_PLATFORM_ERROR;
        placement.rcNormalPosition
            = (RECT){ target.x, target.y, target.x + (int32_t)target.width,
                      target.y + (int32_t)target.height };
        // WINDOWPLACEMENT uses workspace coordinates for ordinary app windows.
        if (!(GetWindowLongPtr (win->id, GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
            OffsetRect (&placement.rcNormalPosition,
                        monitor.rcMonitor.left - monitor.rcWork.left,
                        monitor.rcMonitor.top - monitor.rcWork.top);
        placement.showCmd = SW_SHOWMINNOACTIVE;
        return SetWindowPlacement (win->id, &placement) ? GF_SUCCESS
                                                        : GF_ERROR_PLATFORM_ERROR;
    }
    if (IsZoomed (win->id))
    {
        // Move a native maximized window without a restore/maximize sequence
        // that can activate it or overwrite the saved workspace selection.
        gf_window_state_reset (win->id);
        return SetWindowPos (win->id, NULL, bounds.x, bounds.y, (int)bounds.width,
                             (int)bounds.height, SWP_NOZORDER | SWP_NOACTIVATE)
                   ? GF_SUCCESS
                   : GF_ERROR_PLATFORM_ERROR;
    }
    return gf_window_set_geometry (NULL, win->id, &target, GF_GEOMETRY_CHANGE_ALL, NULL);
}

bool
gf_window_was_moved (gf_display_t display, gf_handle_t window)
{
    (void)display;
    bool moved = GetPropA (window, GF_WINDOW_MOVED_PROP) != NULL;
    if (moved)
        RemovePropA (window, GF_WINDOW_MOVED_PROP);
    return moved;
}

// Resolve the owning process id for a window. UWP host windows
// (ApplicationFrameWindow) proxy a child process, so dig into the child.
static DWORD
window_resolve_pid (HWND window, const char *class_name)
{
    DWORD pid = 0;
    GetWindowThreadProcessId (window, &pid);

    if (strcmp (class_name, "ApplicationFrameWindow") != 0)
        return pid;

    HWND child = FindWindowExA (window, NULL, "Windows.UI.Core.CoreWindow", NULL);
    if (child)
    {
        GetWindowThreadProcessId (child, &pid);
        return pid;
    }

    // Fallback: first child window with a different PID
    for (HWND c = GetWindow (window, GW_CHILD); c; c = GetWindow (c, GW_HWNDNEXT))
    {
        DWORD child_pid = 0;
        GetWindowThreadProcessId (c, &child_pid);
        if (child_pid != 0 && child_pid != pid)
            return child_pid;
    }
    return pid;
}

void
gf_window_get_class (gf_display_t display, gf_handle_t window, char *buffer,
                     size_t bufsize)
{
    (void)display;

    if (!window || !buffer || bufsize == 0)
        return;

    buffer[0] = '\0';

    if (!gf_window_validate (window))
        return;

    char class_name[128] = { 0 };
    if (!GetClassNameA ((HWND)window, class_name, sizeof (class_name)))
        return;

    // Append the executable name so rules can match against the .exe
    DWORD pid = window_resolve_pid ((HWND)window, class_name);
    char exe_name[MAX_PATH] = { 0 };
    pid_get_exe_name (pid, exe_name, sizeof (exe_name));

    if (exe_name[0] != '\0')
    {
        snprintf (buffer, bufsize, "%s|%s", class_name, exe_name);
    }
    else
    {
        strncpy (buffer, class_name, bufsize - 1);
        buffer[bufsize - 1] = '\0';
    }
}

bool
gf_platform_window_minimized (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!gf_window_validate (window))
        return false;

    return IsIconic ((HWND)window);
}

bool
gf_platform_window_hidden (gf_display_t display, gf_handle_t window)
{
    (void)display;

    if (!gf_window_validate (window))
        return false;

    // Window is hidden if it's not visible AND not minimized to taskbar
    // This catches windows that are closed to system tray
    return !IsWindowVisible ((HWND)window) && !IsIconic ((HWND)window);
}

bool
gf_window_is_maximized (gf_display_t display, gf_handle_t window)
{
    (void)display;
    if (!gf_window_validate ((HWND)window))
        return false;
    if (IsZoomed (window))
        return true;
    WINDOWPLACEMENT placement = { .length = sizeof (placement) };
    return IsIconic (window) && GetWindowPlacement (window, &placement)
           && (placement.flags & WPF_RESTORETOMAXIMIZED);
}
