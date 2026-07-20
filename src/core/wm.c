#include "wm.h"
#include "../config/config.h"
#include "../platform/platform_compat.h"
#include "../utils/list.h"
#include "../utils/logger.h"
#include "../utils/memory.h"
#include "border.h"
#include "internal.h"
#include "layout.h"
#include "types.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Poll cadence for changes that emit no root property event (maximize, user
// resize): the loop wakes this often to run a light focus/maximize check.
// Property events (focus, window open/close) still wake it immediately.
#define GF_EVENT_TIMEOUT_MS 250

static void
handle_max_windows_change (gf_wm_t *m, const gf_config_t *old, const gf_config_t *new)
{
    if (old->max_windows_per_workspace == new->max_windows_per_workspace)
        return;

    GF_LOG_INFO ("max_windows_per_workspace changed from %u to %u",
                 old->max_windows_per_workspace, new->max_windows_per_workspace);

    recount_workspace_windows (m, wm_workspaces (m), wm_windows (m),
                               new->max_windows_per_workspace);
    gf_window_list_mark_all_needs_update (wm_windows (m), NULL);

    gf_ws_list_t *ws_list = wm_workspaces (m);
    for (uint32_t i = 0; i < ws_list->count; i++)
        ws_list->items[i].is_custom_layout = false;
}

static void
wm_reset_monitor_state (gf_wm_t *m)
{
    for (int i = 0; i < GF_MAX_MONITORS; i++)
    {
        m->state.last_active_window[i] = 0;
        m->state.last_active_workspace[i] = 0;
        wm_workspaces (m)->active_workspace[i] = 0;
    }
}

static void
wm_tick (gf_wm_t *m, gf_wake_t wake)
{
    if (gf_wm_load_cfg (m))
        wake |= GF_WAKE_LAYOUT; // new config may change gaps, rules or workspaces

    // The full re-scan is the costliest step: run it only on real window changes.
    if (wake & GF_WAKE_LAYOUT)
    {
        gf_wm_watch (m);
        gf_wm_layout_rebalance (m);
    }

    gf_wm_resize_event (m);
    gf_wm_layout_apply (m);

    // Focus/maximize tracking hits the X server; skip it on a bare timeout.
    if (wake != GF_WAKE_IDLE)
        gf_wm_event (m);

    /*
     * Keymap must run AFTER gf_wm_event so that a workspace switch is not
     * immediately undone by gf_wm_event reading the old focused window and
     * switching back.
     */
    gf_wm_keymap_event (m);

    if (m->config->enable_borders && m->platform->border_update)
        m->platform->border_update (m->platform, m->config);

    if (m->ipc_handle >= 0)
        gf_ipc_server_process (m->ipc_handle, m);
}

gf_err_t
gf_wm_create (gf_wm_t **manager, gf_platform_t *platform, gf_layout_engine_t *layout)
{
    if (!manager || !platform || !layout)
        return GF_ERROR_INVALID_PARAMETER;

    *manager = gf_calloc (1, sizeof (**manager));
    if (!*manager)
        return GF_ERROR_MEMORY_ALLOCATION;

    (*manager)->platform = platform;
    (*manager)->layout = layout;
    (*manager)->ipc_handle = -1;

    if (gf_window_list_init (wm_windows (*manager), 16) != GF_SUCCESS)
        goto fail;

    if (gf_workspace_list_init (wm_workspaces (*manager), 16) != GF_SUCCESS)
        goto fail;

    return GF_SUCCESS;

fail:
    gf_window_list_cleanup (wm_windows (*manager));
    gf_free (*manager);
    *manager = NULL;
    return GF_ERROR_MEMORY_ALLOCATION;
}

void
gf_wm_destroy (gf_wm_t *m)
{
    if (!m)
        return;

    gf_window_list_cleanup (wm_windows (m));
    gf_workspace_list_cleanup (wm_workspaces (m));
    gf_free (m);
}

gf_err_t
gf_wm_init (gf_wm_t *m)
{
    if (!m || !m->platform || !m->platform->init || !m->platform->cleanup)
        return GF_ERROR_INVALID_PARAMETER;

    gf_platform_t *platform = wm_platform (m);

    gf_err_t err = platform->init (platform, wm_display (m));
    if (err != GF_SUCCESS)
        return err;

    m->state.last_scan_time = time (NULL);
    m->state.last_cleanup_time = time (NULL);
    m->state.initialized = true;

    if (platform->keymap_init)
    {
        if (platform->keymap_init (platform, *wm_display (m)) == GF_SUCCESS)
        {
            m->state.keymap_initialized = true;
            GF_LOG_INFO ("Keymap support enabled");
        }
        else
        {
            GF_LOG_WARN ("Keymap support not available");
        }
    }

    m->ipc_handle = gf_ipc_server_create ();
    if (m->ipc_handle < 0)
        GF_LOG_WARN ("Failed to create IPC server - client commands will not work");

    if (platform->dock_restore)
        platform->dock_restore (platform);

    GF_LOG_INFO ("Window manager initialized successfully");
    gf_wm_debug_stats (m);
    return GF_SUCCESS;
}

void
gf_wm_cleanup (gf_wm_t *m)
{
    if (!m || !m->state.initialized)
        return;

    gf_platform_t *platform = wm_platform (m);
    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);

    GF_LOG_INFO ("Clearing %u windows and %u workspaces from memory", windows->count,
                 workspaces->count);

    windows->count = 0;
    workspaces->count = 0;

    wm_reset_monitor_state (m);

    if (m->state.keymap_initialized && platform->keymap_cleanup)
    {
        platform->keymap_cleanup (platform);
        m->state.keymap_initialized = false;
    }

    if (m->state.dock_hidden && platform->dock_restore)
    {
        platform->dock_restore (platform);
        m->state.dock_hidden = false;
        GF_LOG_INFO ("Dock restored during cleanup");
    }

    platform->cleanup (*wm_display (m), platform);
    m->state.initialized = false;

    if (m->ipc_handle >= 0)
    {
        gf_ipc_server_destroy (m->ipc_handle);
        m->ipc_handle = -1;
    }

    GF_LOG_INFO ("Window manager cleaned up");
}

bool
gf_wm_load_cfg (gf_wm_t *m)
{
    if (!m->config)
    {
        GF_LOG_ERROR ("Config not initialized in main()");
        return false;
    }

    const char *path = gf_config_get_path ();
    if (!path)
    {
        GF_LOG_ERROR ("Failed to determine config file path");
        return false;
    }

    struct stat st;
    if (stat (path, &st) != 0)
    {
        GF_LOG_ERROR ("Failed to stat config file: %s", path);
        return false;
    }

    if (st.st_mtime <= m->config->last_modified)
        return false;

    gf_config_t old_cfg = *m->config;
    gf_config_t new_cfg = load_or_create_config (path);

    if (!gf_config_changed (&old_cfg, &new_cfg))
    {
        m->config->last_modified = st.st_mtime;
        return false;
    }

    GF_LOG_INFO ("Configuration changed, reloading from: %s", path);
    *m->config = new_cfg;
    m->config->last_modified = st.st_mtime;

    gf_border_handle_toggle (m, &old_cfg, &new_cfg);
    sync_workspaces (m);
    handle_max_windows_change (m, &old_cfg, &new_cfg);
    gf_wm_debug_stats (m);
    return true;
}

gf_err_t
gf_wm_run (gf_wm_t *m)
{
    if (!m || !m->state.initialized)
        return GF_ERROR_INVALID_PARAMETER;

    gf_platform_t *platform = wm_platform (m);
    bool evented = platform->event_subscribe && platform->event_wait;
    if (evented)
        platform->event_subscribe (platform);
    else
        GF_LOG_INFO ("Event-driven loop unavailable; using fixed-rate polling");

    // First pass does a full sync; thereafter each wait tells us what changed.
    gf_wake_t wake = GF_WAKE_FOCUS | GF_WAKE_LAYOUT;

    while (true)
    {
        m->state.loop_counter++;

        wm_tick (m, wake);

        if (time (NULL) - m->state.last_cleanup_time >= 1)
        {
            gf_wm_prune (m);
            m->state.last_cleanup_time = time (NULL);
        }

        // Wait on events rather than spin; without platform support fall back
        // to a fixed sleep and a full tick, matching the old behaviour.
        if (evented)
        {
            wake = platform->event_wait (platform, (int)m->ipc_handle,
                                         GF_EVENT_TIMEOUT_MS);
            // A quiet wake (timeout, or churn we don't classify) still gets a
            // light focus/maximize check — the cheap way to notice a maximize,
            // which emits no root property event. The costly re-scan stays
            // gated on real window-list changes.
            if (wake == GF_WAKE_IDLE)
                wake = GF_WAKE_FOCUS;
        }
        else
        {
            gf_usleep (33000);
            wake = GF_WAKE_FOCUS | GF_WAKE_LAYOUT;
        }
    }

    return GF_SUCCESS;
}
