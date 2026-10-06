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

void
wm_print_workspace_header (gf_ws_id_t id, bool is_locked, uint32_t count,
                           uint32_t max_windows, int32_t available)
{
    const char *lock_str = is_locked ? "LOCKED" : "unlocked";
    GF_LOG_INFO ("Workspace %u (%s): %u/%u windows, %d available", id, lock_str, count,
                 max_windows, available);
}

void
wm_print_window_info (uint32_t window_id, const char *name)
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

    GF_LOG_INFO ("=== Stats ===");
    GF_LOG_INFO ("Total Windows: %u, Total Workspaces: %u", windows->count,
                 workspaces->count);

    char win_name[256];

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        const gf_ws_info_t *ws = &workspaces->items[i];
        const char *kind = ws->has_maximized_state ? "MAX"
                           : ws->is_excluded_ws    ? "EXCLUDED"
                                                   : "NORMAL";
        GF_LOG_INFO ("WS %d monitor=%u local=%d %s %s %u/%u free=%d", ws->id,
                     ws->monitor_id, ws->local_id, kind, ws->is_locked ? "[LOCKED]" : "",
                     ws->window_count, ws->max_windows, ws->available_space);

        for (uint32_t w = 0; w < windows->count; w++)
        {
            const gf_win_info_t *win = &windows->items[w];

            if (win->workspace_id != ws->id || win->monitor_id != ws->monitor_id)
                continue;

            gf_wm_window_class (m, win->id, win_name, sizeof (win_name));

            GF_LOG_INFO ("   %p  %s", (void *)win->id, win_name);
        }
    }
}
