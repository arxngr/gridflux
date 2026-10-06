#include "border.h"
#include "../utils/logger.h"
#include "internal.h"
#include "wm.h"

static bool
_win_needs_border (gf_wm_t *m, const gf_win_info_t *win)
{
    if (!win->is_valid || win->is_minimized || win->is_maximized
        || wm_is_excluded (m, win->id))
        return false;
    if (win->monitor_id >= GF_MAX_MONITORS
        || m->state.workspaces.active_workspace[win->monitor_id] != win->workspace_id)
        return false;
    return true;
}

static void
_border_add_to_win (gf_wm_t *m, const gf_win_info_t *win)
{
    if (!m->platform->border_add)
        return;

    m->platform->border_add (m->platform, win->id, m->config->border_color,
                             GF_BORDER_WIDTH);
}

static void
_borders_apply_to_current_windows (gf_wm_t *m)
{
    gf_win_list_t *list = wm_windows (m);

    GF_LOG_DEBUG ("Current workspace has %u additional windows", list->count);

    for (uint32_t i = 0; i < list->count; i++)
    {
        gf_win_info_t *win = &list->items[i];

        if (_win_needs_border (m, win))
            _border_add_to_win (m, win);
    }
}

void
gf_border_enable_all (gf_wm_t *m)
{
    if (m->platform->border_cleanup)
        m->platform->border_cleanup (m->platform);

    // Platform enumeration returns native desktop/monitor defaults, not the
    // monitor-local identities tracked by the manager.
    _borders_apply_to_current_windows (m);
}

void
gf_border_disable_all (gf_wm_t *m)
{
    GF_LOG_INFO ("Borders disabled, cleaning up...");

    if (m->platform->border_cleanup)
        m->platform->border_cleanup (m->platform);
}

void
gf_border_handle_toggle (gf_wm_t *m, const gf_config_t *old, const gf_config_t *new)
{
    if (old->enable_borders == new->enable_borders)
        return;

    if (!new->enable_borders)
    {
        gf_border_disable_all (m);
        return;
    }

    GF_LOG_INFO ("Borders enabled, adding to all valid windows...");
    gf_border_enable_all (m);
}
