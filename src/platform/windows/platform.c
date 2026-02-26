#include "../../utils/logger.h"
#include "../../utils/memory.h"
#include "internal.h"
#include <string.h>

gf_platform_t *
gf_platform_create (void)
{
    gf_platform_t *platform = gf_malloc (sizeof (gf_platform_t));
    if (!platform)
        return NULL;

    gf_windows_platform_data_t *data = gf_malloc (sizeof (gf_windows_platform_data_t));
    if (!data)
    {
        gf_free (platform);
        return NULL;
    }

    memset (platform, 0, sizeof (gf_platform_t));
    memset (data, 0, sizeof (gf_windows_platform_data_t));

    // --- Lifecycle & Core ---
    platform->init = gf_platform_init;
    platform->cleanup = gf_platform_cleanup;

    // --- Window Enumeration & Info ---
    platform->window_enumerate = gf_platform_get_windows;
    platform->window_get_focused = gf_window_get_focused;
    platform->window_get_class = gf_window_get_class;

    // --- Window Geometry & State ---
    platform->window_get_geometry = gf_window_get_geometry;
    platform->window_is_excluded = gf_window_is_excluded;
    platform->window_is_fullscreen = gf_window_is_fullscreen;
    platform->window_is_hidden = gf_platform_window_hidden;
    platform->window_is_maximized = gf_window_is_maximized;
    platform->window_is_minimized = gf_platform_window_minimized;
    platform->window_is_valid = gf_window_is_valid;
    platform->window_minimize = gf_window_minimize;
    platform->window_set_geometry = gf_window_set_geometry;
    platform->window_unminimize = gf_window_unminimize;

    // --- Workspace & Screen ---
    platform->screen_get_bounds = gf_screen_get_bounds;
    platform->workspace_get_count = gf_workspace_get_count;

    // --- Border Management ---
    platform->border_add = gf_border_add;
    platform->border_cleanup = gf_border_cleanup;
    platform->border_remove = gf_border_remove;
    platform->border_update = gf_border_update;

    // --- Dock Management ---
    platform->dock_hide = gf_dock_hide;
    platform->dock_restore = gf_dock_restore;

    platform->platform_data = data;

    return platform;
}

gf_err_t
gf_platform_init (gf_platform_t *platform, gf_display_t *display)
{
    GF_LOG_INFO ("Initialize Windows platform...");

    if (!display)
        return GF_ERROR_INVALID_PARAMETER;

    *display = NULL;

    gf_windows_platform_data_t *data
        = (gf_windows_platform_data_t *)platform->platform_data;
    data->monitor_count = GetSystemMetrics (SM_CMONITORS);

    GF_LOG_INFO ("Platform initialized successfully (monitors: %d)", data->monitor_count);
    return GF_SUCCESS;
}

void
gf_platform_cleanup (gf_display_t display, gf_platform_t *platform)
{
    (void)display;

    if (!platform || !platform->platform_data)
        return;

    gf_border_cleanup (platform);

    GF_LOG_INFO ("Platform cleaned up");
}

void
gf_platform_destroy (gf_platform_t *platform)
{
    if (!platform)
        return;

    gf_free (platform->platform_data);
    gf_free (platform);
}
