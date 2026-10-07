#include "../utils/logger.h"
#include "internal.h"
#include "types.h"
#include <string.h>

static void
pause_monitors (gf_wm_t *m)
{
    gf_platform_t *platform = wm_platform (m);
    if (!m->state.monitors_paused)
    {
        if (platform->border_cleanup)
            platform->border_cleanup (platform);
        if (platform->dock_restore)
            platform->dock_restore (platform);
        m->state.dock_hidden = false;
        for (uint32_t i = 0; i < wm_windows (m)->count; i++)
            wm_windows (m)->items[i].monitor_restore_failures = 0;
    }
    m->state.monitors_paused = m->state.monitors_recovering = true;
}

bool
wm_poll_monitors (gf_wm_t *m)
{
    gf_platform_t *platform = wm_platform (m);
    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);
    if (platform->monitor_poll && !platform->monitor_poll (platform))
    {
        pause_monitors (m);
        return false;
    }
    if (!platform->monitor_enumerate)
        return true;

    gf_monitor_t monitors[GF_MAX_MONITORS] = { 0 };
    uint32_t count = GF_MAX_MONITORS;
    if (platform->monitor_enumerate (platform, monitors, &count) != GF_SUCCESS || !count
        || count > GF_MAX_MONITORS)
    {
        pause_monitors (m);
        return false;
    }

    if (m->state.monitors_recovering && platform->window_restore_monitor)
    {
        bool pending = false;
        for (uint32_t i = 0; i < windows->count; i++)
        {
            gf_win_info_t *win = &windows->items[i];
            if (!win->is_valid || win->monitor_id >= count
                || !monitors[win->monitor_id].full_bounds.width
                || !monitors[win->monitor_id].full_bounds.height
                || win->monitor_restore_failures >= 3
                || wm_is_system_excluded (m, win->id))
                continue;
            if (platform->window_restore_monitor (
                    platform, win, &m->state.monitors[win->monitor_id].bounds)
                != GF_SUCCESS)
            {
                pending |= ++win->monitor_restore_failures < 3;
                GF_LOG_WARN ("Window %p monitor recovery failed (attempt %u/3)",
                             (void *)win->id, win->monitor_restore_failures);
            }
        }
        if (pending)
        {
            m->state.monitors_paused = true;
            return false;
        }
    }
    bool changed = m->state.monitors_recovering || count != m->state.monitor_count;
    for (uint32_t i = 0; !changed && i < count; i++)
        changed = monitors[i].id != m->state.monitors[i].id
                  || monitors[i].is_primary != m->state.monitors[i].is_primary
                  || memcmp (&monitors[i].bounds, &m->state.monitors[i].bounds,
                             sizeof (gf_rect_t))
                         != 0
                  || memcmp (&monitors[i].full_bounds, &m->state.monitors[i].full_bounds,
                             sizeof (gf_rect_t))
                         != 0;
    m->state.monitors_paused = m->state.monitors_recovering = false;
    if (changed)
    {
        gf_window_list_mark_all_needs_update (windows, NULL);
        for (uint32_t i = 0; i < workspaces->count; i++)
            workspaces->items[i].is_custom_layout = false;
        memcpy (m->state.monitors, monitors, count * sizeof (gf_monitor_t));
        m->state.monitor_count = count;
    }
    return true;
}

static bool
window_is_minimized (gf_wm_t *m, gf_win_info_t *win)
{
    gf_platform_t *platform = wm_platform (m);
    if (platform->window_is_minimized)
        return platform->window_is_minimized (*wm_display (m), win->id);
    return win->is_minimized;
}

gf_err_t
wm_request_visibility (gf_wm_t *m, gf_win_info_t *win, bool minimized)
{
    if (wm_is_system_excluded (m, win->id))
        return GF_SUCCESS;
    gf_platform_t *platform = wm_platform (m);
    uint8_t request = minimized ? 1 : 2;
    bool actual = window_is_minimized (m, win);
    bool reversing = win->visibility_request && win->visibility_request != request;
    if (actual == minimized && !reversing && !win->visibility_settle)
    {
        win->visibility_request = win->visibility_wait = win->visibility_attempts = 0;
        win->is_minimized = actual;
        if (!actual)
            win->monitor_suspended = false;
        return GF_SUCCESS;
    }
    if (win->visibility_request == request && win->visibility_wait)
    {
        win->is_minimized = minimized;
        return GF_SUCCESS;
    }
    if (win->visibility_request != request)
        win->visibility_attempts = 0;
    if (win->visibility_attempts >= 3)
        return GF_ERROR_PLATFORM_ERROR;
    win->visibility_request = request;
    win->visibility_wait = 6;
    win->visibility_attempts++;
    win->visibility_settle = reversing ? 6 : 0;
    gf_err_t result = minimized
                          ? (platform->window_minimize
                                 ? platform->window_minimize (*wm_display (m), win->id)
                                 : GF_ERROR_PLATFORM_ERROR)
                          : (platform->window_unminimize
                                 ? platform->window_unminimize (*wm_display (m), win->id)
                                 : GF_ERROR_PLATFORM_ERROR);
    if (result != GF_SUCCESS)
    {
        GF_LOG_WARN ("Window %p visibility request failed (attempt %u/3)",
                     (void *)win->id, win->visibility_attempts);
        return result;
    }
    if (win->is_minimized != minimized)
        gf_window_list_mark_all_needs_update (wm_windows (m), &win->workspace_id);
    win->is_minimized = minimized;
    win->monitor_suspended = true;
    if (window_is_minimized (m, win) == minimized && !win->visibility_settle)
    {
        win->visibility_request = win->visibility_wait = win->visibility_attempts = 0;
        win->monitor_suspended = minimized;
    }
    return GF_SUCCESS;
}

void
wm_observe_window_state (gf_wm_t *m, gf_win_info_t *win, bool *minimized, bool *maximized)
{
    if (win->visibility_request)
    {
        bool target = win->visibility_request == 1;
        if (*minimized == target)
        {
            if (win->visibility_settle)
                win->visibility_settle--;
            if (!win->visibility_settle)
            {
                win->visibility_request = win->visibility_wait = win->visibility_attempts
                    = 0;
                if (!target)
                    win->monitor_suspended = false;
            }
        }
        else if (win->visibility_settle)
        {
            // The earlier, opposite request arrived after it was cancelled.
            // Allow the selected visibility to be reissued immediately.
            win->visibility_settle = win->visibility_wait = 0;
        }
        else if (win->visibility_wait)
        {
            win->visibility_wait--;
            *minimized = target;
        }
    }
    if (win->mode_wait)
    {
        if (*maximized == win->is_maximized && !wm_platform (m)->window_maximize_async)
            win->mode_wait = 0;
        else
        {
            win->mode_wait--;
            *maximized = win->is_maximized;
        }
    }
}

void
wm_request_maximized (gf_wm_t *m, gf_win_info_t *win)
{
    if (wm_is_excluded (m, win->id))
        return;
    gf_platform_t *platform = wm_platform (m);
    if (!win->is_minimized && platform->window_set_maximized
        && platform->window_set_maximized (*wm_display (m), win->id, win->is_maximized)
               == GF_SUCCESS)
        win->mode_wait
            = !platform->window_maximize_async && platform->window_is_maximized
                      && platform->window_is_maximized (*wm_display (m), win->id)
                             == (bool)win->is_maximized
                  ? 0
                  : 6;
}

void
wm_sync_monitor_activity (gf_wm_t *m, gf_monitor_id_t active_monitor)
{
    if (!m || active_monitor >= GF_MAX_MONITORS)
        return;

    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);
    m->state.active_monitor_id = active_monitor;
    m->state.active_monitor_valid = true;

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *win = &windows->items[i];
        if (!win->is_valid)
            continue;
        if (wm_is_system_excluded (m, win->id))
        {
            if (platform->border_remove)
                platform->border_remove (platform, win->id);
            continue;
        }

        bool maximized = win->is_maximized
                         || (platform->window_is_maximized
                             && platform->window_is_maximized (display, win->id));
        gf_ws_info_t *workspace
            = gf_workspace_list_find_by_id (workspaces, win->workspace_id);
        bool in_live_workspace
            = gf_workspace_has_monitor (workspace, win->monitor_id)
              && win->workspace_id == workspaces->active_workspace[win->monitor_id];

        if (!in_live_workspace)
        {
            wm_request_visibility (m, win, true);
        }
        else if (win->monitor_suspended)
            wm_request_visibility (m, win, false);
        bool show_border = m->config && m->config->enable_borders && in_live_workspace
                           && !maximized && !win->is_minimized
                           && !window_is_minimized (m, win)
                           && !wm_user_excluded (m, win->id);
        if (show_border && platform->border_add)
            platform->border_add (platform, win->id, m->config->border_color,
                                  GF_BORDER_WIDTH);
        else if (platform->border_remove)
            platform->border_remove (platform, win->id);
    }
}
