#include "../../utils/logger.h"
#include "internal.h"
#include "platform.h"
#include "platform/windows/internal.h"
#include <stdlib.h>

HWND
_create_border_overlay (HWND target)
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

void
_update_border (gf_border_t *b, const RECT *gui_rects, int gui_count)
{
    if (!b || !b->target || !b->overlay)
        return;

    if (!IsWindow (b->target) || !IsWindow (b->overlay))
        return;

    // Hide border if target is not visible, minimized, or maximized
    if (!IsWindowVisible (b->target) || IsIconic (b->target) || IsZoomed (b->target))
    {
        ShowWindow (b->overlay, SW_HIDE);
        return;
    }

    RECT rect;
    if (SUCCEEDED (DwmGetWindowAttribute (b->target, DWMWA_EXTENDED_FRAME_BOUNDS, &rect,
                                          sizeof (rect)))
        || GetWindowRect (b->target, &rect))
    {
        int t = b->thickness;
        int win_x = rect.left - t;
        int win_y = rect.top - t;
        int win_w = (rect.right - rect.left) + 2 * t;
        int win_h = (rect.bottom - rect.top) + 2 * t;

        RECT border_rect = { win_x, win_y, win_x + win_w, win_y + win_h };

        // Find intersections with GUI windows
        RECT intersections[16];
        int intersect_count = 0;

        for (int i = 0; i < gui_count && intersect_count < 16; i++)
        {
            RECT intersect;
            if (IntersectRect (&intersect, &border_rect, &gui_rects[i]))
            {
                intersections[intersect_count++] = intersect;
            }
        }

        bool geom_changed = memcmp (&rect, &b->last_rect, sizeof (RECT)) != 0;
        bool shape_changed = geom_changed || (intersect_count != b->last_intersect_count);

        if (!shape_changed && intersect_count > 0)
        {
            for (int k = 0; k < intersect_count; k++)
            {
                if (memcmp (&intersections[k], &b->last_intersections[k], sizeof (RECT))
                    != 0)
                {
                    shape_changed = true;
                    break;
                }
            }
        }

        if (shape_changed)
        {
            MoveWindow (b->overlay, win_x, win_y, win_w, win_h, TRUE);

            // Apply region to exclude intersections
            HRGN full_rgn = CreateRectRgn (0, 0, win_w, win_h);

            // Subtract middle (the target window itself)
            HRGN hollow_rgn = CreateRectRgn (t, t, win_w - t, win_h - t);
            CombineRgn (full_rgn, full_rgn, hollow_rgn, RGN_DIFF);
            DeleteObject (hollow_rgn);

            // Subtract GUI intersections
            for (int i = 0; i < intersect_count; i++)
            {
                HRGN intersect_rgn = CreateRectRgn (
                    intersections[i].left - win_x, intersections[i].top - win_y,
                    intersections[i].right - win_x, intersections[i].bottom - win_y);
                CombineRgn (full_rgn, full_rgn, intersect_rgn, RGN_DIFF);
                DeleteObject (intersect_rgn);
            }

            SetWindowRgn (b->overlay, full_rgn, TRUE);

            b->last_rect = rect;
            b->last_intersect_count = intersect_count;
            if (intersect_count > 0)
            {
                memcpy (b->last_intersections, intersections,
                        intersect_count * sizeof (RECT));
            }
        }

        if (!IsWindowVisible (b->overlay))
            ShowWindow (b->overlay, SW_SHOWNOACTIVATE);

        InvalidateRect (b->overlay, NULL, TRUE);
    }
}

LRESULT CALLBACK
_border_wnd_proc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_PAINT:
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

            RECT r;
            // Left
            r.left = 0;
            r.top = 0;
            r.right = t;
            r.bottom = rect.bottom;
            FillRect (hdc, &r, brush);
            // Right
            r.left = rect.right - t;
            r.top = 0;
            r.right = rect.right;
            r.bottom = rect.bottom;
            FillRect (hdc, &r, brush);
            // Top
            r.left = t;
            r.top = 0;
            r.right = rect.right - t;
            r.bottom = t;
            FillRect (hdc, &r, brush);
            // Bottom
            r.left = t;
            r.top = rect.bottom - t;
            r.right = rect.right - t;
            r.bottom = rect.bottom;
            FillRect (hdc, &r, brush);

            DeleteObject (brush);
        }

        EndPaint (hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc (hwnd, msg, wparam, lparam);
}

void
gf_border_add (gf_platform_t *platform, gf_handle_t window, gf_color_t color,
               int thickness)
{
    if (!platform || !platform->platform_data || !window)
        return;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    // Check if already exists
    for (int i = 0; i < data->border_count; i++)
    {
        if (data->borders[i] && data->borders[i]->target == window)
        {
            GF_LOG_DEBUG ("Border already exists for window %p", window);
            return;
        }
    }

    RECT rect;
    if (FAILED (DwmGetWindowAttribute (window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect,
                                       sizeof (rect)))
        && !GetWindowRect (window, &rect))
    {
        GF_LOG_WARN ("Failed to get window rect for border");
        return;
    }

    HWND overlay = _create_border_overlay (window);
    if (!overlay)
    {
        GF_LOG_WARN ("Failed to create border overlay");
        return;
    }

    gf_border_t *b = malloc (sizeof (gf_border_t));
    if (!b)
    {
        DestroyWindow (overlay);
        GF_LOG_ERROR ("Failed to allocate border structure");
        return;
    }

    b->target = window;
    b->overlay = overlay;
    b->color = color;
    b->thickness = thickness;
    b->last_rect = rect;

    SetPropA (overlay, "BorderThickness", (HANDLE)(INT_PTR)thickness);
    SetPropA (overlay, "BorderColor", (HANDLE)(INT_PTR)color);

    data->borders[data->border_count++] = b;

    GF_LOG_INFO ("Added border for window %p (color=0x%08X, thickness=%d, count=%d)",
                 window, color, thickness, data->border_count);

    _update_border (b, NULL, 0);
}

void
gf_border_update (gf_platform_t *platform, const gf_config_t *config)
{
    if (!platform || !platform->platform_data || !config)
        return;

    // Find all GUI windows geometries
    RECT gui_rects[16];
    int gui_count = 0;

    HWND hwnd = GetTopWindow (NULL);
    while (hwnd && gui_count < 16)
    {
        if (_window_excluded_border (hwnd))
        {
            if (SUCCEEDED (DwmGetWindowAttribute (hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                                  &gui_rects[gui_count], sizeof (RECT)))
                || GetWindowRect (hwnd, &gui_rects[gui_count]))
            {
                gui_count++;
            }
        }
        hwnd = GetNextWindow (hwnd, GW_HWNDNEXT);
    }

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    // Update all borders and prune dead ones
    for (int i = 0; i < data->border_count;)
    {
        gf_border_t *b = data->borders[i];

        if (!b->target || !IsWindow (b->target) || _window_excluded_border (b->target))
        {
            if (b->overlay && IsWindow (b->overlay))
            {
                DestroyWindow (b->overlay);
            }
            free (b);

            for (int j = i; j < data->border_count - 1; j++)
                data->borders[j] = data->borders[j + 1];

            data->border_count--;
        }
        else
        {
            if (b->color != config->border_color)
            {
                b->color = config->border_color;
                if (b->overlay && IsWindow (b->overlay))
                {
                    SetPropA (b->overlay, "BorderColor", (HANDLE)(INT_PTR)b->color);
                    InvalidateRect (b->overlay, NULL, TRUE);
                }
            }
            _update_border (b, gui_rects, gui_count);
            i++;
        }
    }

    // Process messages for border windows (they are created on this thread)
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
