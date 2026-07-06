#include "border.h"
#include "../utils/logger.h"
#include "../utils/memory.h"
#include "internal.h"
#include "wm.h"
#include <inttypes.h>
#include <stdint.h>

static bool
_win_needs_border (gf_wm_t *m, const gf_win_info_t *win)
{
    return win->is_valid && !win->is_minimized && !wm_is_excluded (m, win->id);
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
_borders_apply_to_workspace (gf_wm_t *m, gf_ws_id_t workspace)
{
    gf_win_info_t *wins = NULL;
    uint32_t count = 0;
    gf_err_t err;

    err = m->platform->window_enumerate (m->display, &workspace, &wins, &count);
    if (err != GF_SUCCESS)
    {
        GF_LOG_DEBUG ("Failed to get windows for workspace %d", workspace);
        return;
    }

    GF_LOG_DEBUG ("Processing workspace %d with %u windows", workspace, count);

    for (uint32_t i = 0; i < count; i++)
    {
        gf_win_info_t *win = &wins[i];

        if (_win_needs_border (m, win))
        {
            GF_LOG_DEBUG ("Adding border to window %" PRIuPTR " in workspace %d",
                          (uintptr_t)win->id, workspace);
            _border_add_to_win (m, win);
        }
        else
        {
            GF_LOG_DEBUG ("Skipping window %" PRIuPTR
                          " (valid=%d, minimized=%d, excluded=%d)",
                          (uintptr_t)win->id, win->is_valid, win->is_minimized,
                          wm_is_excluded (m, win->id));
        }
    }

    gf_free (wins);
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

    for (gf_ws_id_t ws = 0; ws < GF_MAX_WORKSPACES; ws++)
        _borders_apply_to_workspace (m, ws);

    /* Fallback: catch any windows tracked in the current list */
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
