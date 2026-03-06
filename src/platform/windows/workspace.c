#include "../../utils/logger.h"
#include "internal.h"


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

    int x = GetSystemMetrics (SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics (SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics (SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics (SM_CYVIRTUALSCREEN);

    int panel_left, panel_right, panel_top, panel_bottom;
    _get_taskbar_dimensions (&panel_left, &panel_right, &panel_top, &panel_bottom);

    bounds->x = x + panel_left;
    bounds->y = y + panel_top;
    bounds->width = (gf_dimension_t)(width - panel_left - panel_right);
    bounds->height = (gf_dimension_t)(height - panel_top - panel_bottom);

    return GF_SUCCESS;
}

/* ── Monitor enumeration callback ── */

typedef struct
{
    gf_monitor_t *monitors;
    uint32_t count;
    uint32_t max;
} _enum_mon_ctx_t;

static BOOL CALLBACK
_enum_monitor_proc (HMONITOR hmon, HDC hdc, LPRECT lprc, LPARAM lparam)
{
    (void)hdc;
    (void)lprc;

    _enum_mon_ctx_t *ctx = (_enum_mon_ctx_t *)lparam;
    if (ctx->count >= ctx->max)
        return FALSE;

    MONITORINFO mi = { .cbSize = sizeof (mi) };
    if (!GetMonitorInfo (hmon, &mi))
        return TRUE;

    gf_monitor_t *m = &ctx->monitors[ctx->count];
    m->id = ctx->count;
    m->bounds.x = mi.rcWork.left;
    m->bounds.y = mi.rcWork.top;
    m->bounds.width = (gf_dimension_t)(mi.rcWork.right - mi.rcWork.left);
    m->bounds.height = (gf_dimension_t)(mi.rcWork.bottom - mi.rcWork.top);
    m->full_bounds.x = mi.rcMonitor.left;
    m->full_bounds.y = mi.rcMonitor.top;
    m->full_bounds.width = (gf_dimension_t)(mi.rcMonitor.right - mi.rcMonitor.left);
    m->full_bounds.height = (gf_dimension_t)(mi.rcMonitor.bottom - mi.rcMonitor.top);
    m->is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

    ctx->count++;
    return TRUE;
}

uint32_t
gf_monitor_get_count (gf_platform_t *platform)
{
    (void)platform;
    return (uint32_t)GetSystemMetrics (SM_CMONITORS);
}

gf_err_t
gf_monitor_enumerate (gf_platform_t *platform, gf_monitor_t *monitors, uint32_t *count)
{
    if (!platform || !monitors || !count)
        return GF_ERROR_INVALID_PARAMETER;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    _enum_mon_ctx_t ctx = { .monitors = monitors, .count = 0, .max = *count };

    EnumDisplayMonitors (NULL, NULL, _enum_monitor_proc, (LPARAM)&ctx);

    *count = ctx.count;

    /* Cache in platform data for later lookups */
    for (uint32_t i = 0; i < ctx.count && i < GF_MAX_MONITORS; i++)
        data->monitors[i] = monitors[i];
    data->enumerated_monitor_count = ctx.count;

    GF_LOG_INFO ("Enumerated %u monitors", ctx.count);
    for (uint32_t i = 0; i < ctx.count; i++)
    {
        GF_LOG_INFO ("  Monitor %u: %dx%d at (%d,%d)%s", monitors[i].id,
                     monitors[i].bounds.width, monitors[i].bounds.height,
                     monitors[i].bounds.x, monitors[i].bounds.y,
                     monitors[i].is_primary ? " [PRIMARY]" : "");
    }

    return GF_SUCCESS;
}

gf_monitor_id_t
gf_monitor_from_window (gf_platform_t *platform, gf_handle_t window)
{
    if (!platform || !window)
        return 0;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;

    HMONITOR hmon = MonitorFromWindow (window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { .cbSize = sizeof (mi) };
    if (!GetMonitorInfo (hmon, &mi))
        return 0;

    /* Match the HMONITOR work-area to our cached monitors */
    for (uint32_t i = 0; i < data->enumerated_monitor_count; i++)
    {
        if (data->monitors[i].full_bounds.x == mi.rcMonitor.left
            && data->monitors[i].full_bounds.y == mi.rcMonitor.top)
        {
            return data->monitors[i].id;
        }
    }

    return 0; /* fallback to primary */
}

gf_err_t
gf_screen_get_bounds_for_monitor (gf_display_t display, gf_monitor_id_t monitor_id,
                                  gf_rect_t *bounds)
{
    (void)display;

    if (!bounds)
        return GF_ERROR_INVALID_PARAMETER;

    /* Re-enumerate to get fresh bounds (handles resolution changes) */
    gf_monitor_t monitors[GF_MAX_MONITORS];
    uint32_t count = GF_MAX_MONITORS;
    _enum_mon_ctx_t ctx = { .monitors = monitors, .count = 0, .max = count };

    EnumDisplayMonitors (NULL, NULL, _enum_monitor_proc, (LPARAM)&ctx);

    for (uint32_t i = 0; i < ctx.count; i++)
    {
        if (monitors[i].id == monitor_id)
        {
            *bounds = monitors[i].bounds;
            return GF_SUCCESS;
        }
    }

    /* Fallback: return primary or virtual screen */
    return gf_screen_get_bounds (display, bounds);
}
