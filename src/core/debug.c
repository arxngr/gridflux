#include "../config/config.h"
#include "../utils/list.h"
#include "../utils/logger.h"
#include "../utils/memory.h"
#include "internal.h"
#include "layout.h"
#include "types.h"
#include "wm.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void
_print_workspace_header (gf_ws_id_t id, bool is_locked, uint32_t count,
                         uint32_t max_windows, int32_t available)
{
    const char *lock_str = is_locked ? "LOCKED" : "unlocked";
    GF_LOG_INFO ("Workspace %u (%s): %u/%u windows, %d available", id, lock_str, count,
                 max_windows, available);
}

void
_print_window_info (uint32_t window_id, const char *name)
{
    GF_LOG_INFO ("  - [%u] %s", window_id, name);
}

void
gf_wm_debug_stats (const gf_wm_t *m)
{
    if (!m)
        return;

    const gf_win_list_t *windows = &m->state.windows;
    const gf_ws_list_t *workspaces = &m->state.workspaces;

    uint32_t *workspace_counts = gf_calloc (m->config->max_workspaces, sizeof (uint32_t));
    if (!workspace_counts)
    {
        GF_LOG_ERROR ("Failed to allocate workspace_counts");
        return;
    }

    gf_ws_id_t max_workspace = -1;

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_ws_id_t ws = windows->items[i].workspace_id;
        if (ws >= GF_FIRST_WORKSPACE_ID
            && ws < m->config->max_workspaces + GF_FIRST_WORKSPACE_ID)
        {
            workspace_counts[ws]++;
            if (ws > max_workspace)
                max_workspace = ws;
        }
    }

    GF_LOG_INFO ("=== Stats ===");
    GF_LOG_INFO ("Total Windows: %u, Total Workspaces: %u", windows->count,
                 workspaces->count);

    char win_name[256];

    for (gf_ws_id_t i = GF_FIRST_WORKSPACE_ID; i <= max_workspace; i++)
    {
        uint32_t count = workspace_counts[i];
        uint32_t max_windows = m->config->max_windows_per_workspace;
        int32_t available = -1;
        bool is_locked = gf_config_workspace_is_locked (m->config, i);
        bool has_maximized = false;

        gf_ws_info_t *ws = _get_workspace ((gf_ws_list_t *)workspaces, i);
        if (ws)
        {
            max_windows = ws->max_windows;
            available = ws->available_space;
            is_locked = ws->is_locked;
            has_maximized = ws->has_maximized_state;
        }
        else
        {
            // Workspace does not exist yet
            available = is_locked ? 0 : max_windows;
        }

        /* ---- Workspace header ---- */
        GF_LOG_INFO ("WS %d %s %s  %u/%u  free=%d", i, is_locked ? "[LOCKED]" : "",
                     has_maximized ? "[MAX]" : "", count, max_windows, available);

        /* ---- Windows in this workspace ---- */
        for (uint32_t w = 0; w < windows->count; w++)
        {
            const gf_win_info_t *win = &windows->items[w];

            if (win->workspace_id != i)
                continue;

            gf_wm_window_name (m, win->id, win_name, sizeof (win_name));

            GF_LOG_INFO ("   %p  %s", (void *)win->id, win_name);
        }
    }

    gf_free (workspace_counts);
}
