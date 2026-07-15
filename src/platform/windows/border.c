#include "../../utils/logger.h"
#include "internal.h"
#include "platform.h"
#include "platform/windows/internal.h"
#include <stdlib.h>

static LRESULT CALLBACK _border_wnd_proc (HWND hwnd, UINT msg, WPARAM wparam,
                                          LPARAM lparam);
static void _border_update_overlay (gf_border_t *b, const RECT *gui_rects, int gui_count);

HWND
create_border_overlay (HWND target)
{
    HINSTANCE hInst = GetModuleHandle (NULL);

    WNDCLASSEXA wc = { .cbSize = sizeof (WNDCLASSEXA),
                       .lpfnWndProc = _border_wnd_proc,
                       .hInstance = hInst,
                       .lpszClassName = "GridFluxBorder" };

    RegisterClassExA (&wc);

    HWND overlay = CreateWindowExA (WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW
                                        | WS_EX_TOPMOST,
                                    "GridFluxBorder", "GridFlux Border", WS_POPUP, 0, 0,
                                    0, 0, NULL, NULL, hInst, NULL);

    if (overlay)
    {
        SetPropA (overlay, "TargetWindow", (HANDLE)target);
        SetLayeredWindowAttributes (overlay, 0, 255, LWA_ALPHA);
        ShowWindow (overlay, SW_SHOWNOACTIVATE);
    }

    return overlay;
}

// Overlay geometry in Win32 coordinates (the space SetWindowPos operates in).
typedef struct
{
    int x, y, w, h; // overlay position and size, expanded by border thickness
    RECT rect;      // same bounds as a RECT, for intersection tests
    RECT visible;   // DWM extended frame bounds, used as the change-detect key
} border_layout_t;

// True if the target should currently show a border (visible, not cloaked,
// minimized, or maximized).
static bool
_border_target_visible (gf_border_t *b)
{
    if (!IsWindow (b->target) || !IsWindow (b->overlay))
        return false;

    int cloaked = 0;
    DwmGetWindowAttribute (b->target, DWMWA_CLOAKED, &cloaked, sizeof (cloaked));

    return IsWindowVisible (b->target) && !cloaked && !IsIconic (b->target)
           && !IsZoomed (b->target);
}

// Compute the overlay rect from the target's DWM and Win32 bounds. The shadow
// inset converts DWM coords (visible frame) into Win32 coords (incl. shadow).
static bool
_border_compute_layout (gf_border_t *b, border_layout_t *out)
{
    RECT d_rect, w_rect;
    if (!SUCCEEDED (DwmGetWindowAttribute (b->target, DWMWA_EXTENDED_FRAME_BOUNDS,
                                           &d_rect, sizeof (d_rect))))
        return false;
    if (!GetWindowRect (b->target, &w_rect))
        return false;

    int t = b->thickness;
    out->x = d_rect.left - t;
    out->y = d_rect.top - t;
    out->w = (d_rect.right - d_rect.left) + 2 * t;
    out->h = (d_rect.bottom - d_rect.top) + 2 * t;
    out->rect = (RECT){ out->x, out->y, out->x + out->w, out->y + out->h };
    out->visible = d_rect;
    return true;
}

// Collect intersections of the border rect with the GUI windows, capped at max.
static int
_border_find_intersections (const RECT *border_rect, const RECT *gui_rects, int gui_count,
                            RECT *out, int max)
{
    int n = 0;
    for (int i = 0; i < gui_count && n < max; i++)
    {
        RECT intersect;
        if (IntersectRect (&intersect, border_rect, &gui_rects[i]))
            out[n++] = intersect;
    }
    return n;
}

// True if the border geometry or its GUI intersections differ from the cache.
static bool
_border_shape_changed (gf_border_t *b, const RECT *visible, const RECT *intersections,
                       int count)
{
    if (memcmp (visible, &b->last_rect, sizeof (RECT)) != 0)
        return true;
    if (count != b->last_intersect_count)
        return true;
    for (int k = 0; k < count; k++)
        if (memcmp (&intersections[k], &b->last_intersections[k], sizeof (RECT)) != 0)
            return true;
    return false;
}

// Build the hollow ring region (minus GUI intersections) and cache the layout.
static void
_border_apply_region (gf_border_t *b, const border_layout_t *lay,
                      const RECT *intersections, int count)
{
    int t = b->thickness;
    HRGN full_rgn = CreateRectRgn (0, 0, lay->w, lay->h);
    HRGN hollow_rgn = CreateRectRgn (t, t, lay->w - t, lay->h - t);
    CombineRgn (full_rgn, full_rgn, hollow_rgn, RGN_DIFF);
    DeleteObject (hollow_rgn);

    // Subtract GUI intersections (converted to overlay-local coords)
    for (int i = 0; i < count; i++)
    {
        HRGN ir = CreateRectRgn (
            intersections[i].left - lay->x, intersections[i].top - lay->y,
            intersections[i].right - lay->x, intersections[i].bottom - lay->y);
        CombineRgn (full_rgn, full_rgn, ir, RGN_DIFF);
        DeleteObject (ir);
    }

    SetWindowRgn (b->overlay, full_rgn, TRUE);

    b->last_rect = lay->visible;
    b->last_intersect_count = count;
    if (count > 0)
        memcpy (b->last_intersections, intersections, count * sizeof (RECT));
}

static void
_border_update_overlay (gf_border_t *b, const RECT *gui_rects, int gui_count)
{
    if (!b || !b->target || !b->overlay)
        return;

    if (!_border_target_visible (b))
    {
        if (IsWindow (b->overlay))
            ShowWindow (b->overlay, SW_HIDE);
        return;
    }

    // Detect if the overlay was previously hidden — we'll need to force a
    // full reposition including Z-order when transitioning back to visible.
    bool was_hidden = !IsWindowVisible (b->overlay);

    border_layout_t lay;
    if (!_border_compute_layout (b, &lay))
        return;

    RECT intersections[16];
    int count
        = _border_find_intersections (&lay.rect, gui_rects, gui_count, intersections, 16);

    bool shape_changed = _border_shape_changed (b, &lay.visible, intersections, count);

    // Always re-assert HWND_TOPMOST to fix async Z-order inconsistency.
    // When a window is re-tiled (e.g. after another app closes), or gains focus,
    // the OS can push it above the overlay asynchronously, making the border invisible.
    // We force TOPMOST every time to prevent this.
    UINT swp_flags = SWP_NOACTIVATE | SWP_NOREDRAW;
    if (!shape_changed && !was_hidden)
        swp_flags |= SWP_NOMOVE | SWP_NOSIZE;
    SetWindowPos (b->overlay, HWND_TOPMOST, lay.x, lay.y, lay.w, lay.h, swp_flags);

    if (shape_changed || was_hidden)
        _border_apply_region (b, &lay, intersections, count);

    // Force a full repaint after re-showing to ensure the border color is drawn —
    // the cached DC content may be stale after being hidden.
    if (was_hidden)
        ShowWindow (b->overlay, SW_SHOWNOACTIVATE);
    InvalidateRect (b->overlay, NULL, TRUE);
    if (was_hidden)
        UpdateWindow (b->overlay);
}

// Paint the four border edges of the overlay using its cached props.
static void
_border_paint (HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint (hwnd, &ps);

    HWND target = (HWND)GetPropA (hwnd, "TargetWindow");
    if (target)
    {
        RECT rect;
        GetClientRect (hwnd, &rect);

        int t = (int)(INT_PTR)GetPropA (hwnd, "BorderThickness");
        if (t <= 0)
            t = 3;

        UINT32 color = (UINT32)(INT_PTR)GetPropA (hwnd, "BorderColor");
        COLORREF c = RGB ((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
        HBRUSH brush = CreateSolidBrush (c);

        RECT edges[4] = {
            { 0, 0, t, rect.bottom },                            // left
            { rect.right - t, 0, rect.right, rect.bottom },      // right
            { t, 0, rect.right - t, t },                         // top
            { t, rect.bottom - t, rect.right - t, rect.bottom }, // bottom
        };
        for (int i = 0; i < 4; i++)
            FillRect (hdc, &edges[i], brush);

        DeleteObject (brush);
    }

    EndPaint (hwnd, &ps);
}

static LRESULT CALLBACK
_border_wnd_proc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_PAINT)
    {
        _border_paint (hwnd);
        return 0;
    }
    return DefWindowProc (hwnd, msg, wparam, lparam);
}

// True if a border is already tracked for this target window.
static bool
_border_exists (gf_windows_platform_data_t *data, gf_handle_t window)
{
    for (int i = 0; i < data->border_count; i++)
        if (data->borders[i] && data->borders[i]->target == window)
            return true;
    return false;
}

// Allocate and initialise a border, stashing its props on the overlay window.
static gf_border_t *
_border_alloc (HWND overlay, gf_handle_t window, gf_color_t color, int thickness,
               RECT rect)
{
    gf_border_t *b = malloc (sizeof (gf_border_t));
    if (!b)
        return NULL;

    b->target = window;
    b->overlay = overlay;
    b->color = color;
    b->thickness = thickness;
    b->last_rect = rect;

    SetPropA (overlay, "BorderThickness", (HANDLE)(INT_PTR)thickness);
    SetPropA (overlay, "BorderColor", (HANDLE)(INT_PTR)color);
    return b;
}

void
gf_border_add (gf_platform_t *platform, gf_handle_t window, gf_color_t color,
               int thickness)
{
    if (!platform || !platform->platform_data || !window)
        return;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    if (_border_exists (data, window))
    {
        GF_LOG_DEBUG ("Border already exists for window %p", window);
        return;
    }

    RECT rect;
    if (!SUCCEEDED (DwmGetWindowAttribute (window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect,
                                           sizeof (rect))))
    {
        GF_LOG_WARN ("Failed to get window rect for border");
        return;
    }

    HWND overlay = create_border_overlay (window);
    if (!overlay)
    {
        GF_LOG_WARN ("Failed to create border overlay");
        return;
    }

    gf_border_t *b = _border_alloc (overlay, window, color, thickness, rect);
    if (!b)
    {
        DestroyWindow (overlay);
        GF_LOG_ERROR ("Failed to allocate border structure");
        return;
    }

    data->borders[data->border_count++] = b;

    // Force square corners so the rectangular border overlay sits flush
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute (window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner,
                           sizeof (corner));

    GF_LOG_INFO ("Added border for window %p (color=0x%08X, thickness=%d, count=%d)",
                 window, color, thickness, data->border_count);

    _border_update_overlay (b, NULL, 0);
}

// Collect the frame rects of all visible, border-excluded (GUI) windows so the
// overlay can be clipped around them. Returns the number gathered (up to max).
static int
_border_collect_gui_rects (RECT *out, int max)
{
    int count = 0;
    HWND hwnd = GetTopWindow (NULL);
    while (hwnd && count < max)
    {
        if (IsWindowVisible (hwnd) && window_is_border_excluded (hwnd)
            && (SUCCEEDED (DwmGetWindowAttribute (hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                                  &out[count], sizeof (RECT)))
                || GetWindowRect (hwnd, &out[count])))
        {
            count++;
        }
        hwnd = GetNextWindow (hwnd, GW_HWNDNEXT);
    }
    return count;
}

void
gf_border_update (gf_platform_t *platform, const gf_config_t *config)
{
    if (!platform || !platform->platform_data || !config)
        return;

    RECT gui_rects[16];
    int gui_count = _border_collect_gui_rects (gui_rects, 16);

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    for (int i = 0; i < data->border_count; i++)
    {
        gf_border_t *b = data->borders[i];

        if (b->color != config->border_color)
        {
            b->color = config->border_color;
            if (b->overlay && IsWindow (b->overlay))
            {
                SetPropA (b->overlay, "BorderColor", (HANDLE)(INT_PTR)b->color);
                InvalidateRect (b->overlay, NULL, TRUE);
            }
        }
        _border_update_overlay (b, gui_rects, gui_count);
    }

    // Process messages for border windows (they are created on this thread)
    // Limit to 10 messages per poll to avoid infinite spinning
    MSG msg;
    while (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage (&msg);
        DispatchMessage (&msg);
    }
}

void
gf_border_set_color (gf_platform_t *platform, gf_color_t color)
{
    if (!platform || !platform->platform_data)
        return;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    // Update existing borders
    for (int i = 0; i < data->border_count; i++)
    {
        gf_border_t *b = data->borders[i];
        if (b && b->overlay && IsWindow (b->overlay))
        {
            b->color = color;
            SetPropA (b->overlay, "BorderColor", (HANDLE)(INT_PTR)color);
            InvalidateRect (b->overlay, NULL, TRUE);
        }
    }
}

void
gf_border_remove (gf_platform_t *platform, gf_handle_t window)
{
    if (!platform || !platform->platform_data || !window)
        return;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    for (int i = 0; i < data->border_count; i++)
    {
        if (data->borders[i] && data->borders[i]->target == window)
        {
            gf_border_t *b = data->borders[i];

            // Destroy overlay window
            if (b->overlay && IsWindow (b->overlay))
            {
                DestroyWindow (b->overlay);
            }

            //  Restore default corner rounding when the border is removed
            DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_DEFAULT;
            DwmSetWindowAttribute (window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner,
                                   sizeof (corner));

            free (b);

            // Shift remaining borders
            for (int j = i; j < data->border_count - 1; j++)
                data->borders[j] = data->borders[j + 1];

            data->border_count--;

            GF_LOG_DEBUG ("Removed border for window %p", window);
            return;
        }
    }
}

void
gf_border_cleanup (gf_platform_t *platform)
{
    if (!platform || !platform->platform_data)
        return;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    for (int i = 0; i < data->border_count; i++)
    {
        if (data->borders[i])
        {
            if (data->borders[i]->overlay && IsWindow (data->borders[i]->overlay))
            {
                DestroyWindow (data->borders[i]->overlay);
            }
            free (data->borders[i]);
        }
    }
    data->border_count = 0;
}
