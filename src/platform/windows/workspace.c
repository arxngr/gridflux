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
