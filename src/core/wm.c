#include "wm.h"
#include "../config/config.h"
#include "../platform/platform_compat.h"
#include "../utils/list.h"
#include "../utils/logger.h"
#include "../utils/memory.h"
#include "internal.h"
#include "layout.h"
#include "types.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

    gf_err_t result = platform->init (platform, wm_display (m));
    if (result != GF_SUCCESS)
        return result;

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
    {
        GF_LOG_WARN ("Failed to create IPC server - client commands will not work");
    }

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

    // Clear all windows and workspaces from memory
    gf_win_list_t *windows = wm_windows (m);
    gf_ws_list_t *workspaces = wm_workspaces (m);

    GF_LOG_INFO ("Clearing %u windows and %u workspaces from memory", windows->count,
                 workspaces->count);

    // Reset lists (items still allocated, will be freed in destroy)
    windows->count = 0;
    workspaces->count = 0;
    workspaces->active_workspace = 0;

    // Reset state
    m->state.last_active_window = 0;
    m->state.last_active_workspace = 0;

    // Cleanup keymap
    if (m->state.keymap_initialized && platform->keymap_cleanup)
    {
        platform->keymap_cleanup (platform);
        m->state.keymap_initialized = false;
    }

    // Restore dock if it was hidden (critical for program termination)
    if (m->state.dock_hidden && platform->dock_restore)
    {
        platform->dock_restore (platform);
        m->state.dock_hidden = false;
        GF_LOG_INFO ("Dock restored during cleanup");
    }

    platform->cleanup (*wm_display (m), platform);
    m->state.initialized = false;

    // Cleanup IPC server
    if (m->ipc_handle >= 0)
    {
        gf_ipc_server_destroy (m->ipc_handle);
        m->ipc_handle = -1;
    }

    GF_LOG_INFO ("Window manager cleaned up");
}

void
gf_wm_load_cfg (gf_wm_t *m)
{
    if (!m->config)
    {
        GF_LOG_ERROR ("Config not initialized in main()");
        return;
    }

    const char *config_file = gf_config_get_path ();
    if (!config_file)
    {
        GF_LOG_ERROR ("Failed to determine config file path");
        return;
    }

    struct stat st;
    if (stat (config_file, &st) != 0)
    {
        GF_LOG_ERROR ("Failed to read stat config file: %s", config_file);
        return;
    }

    if (st.st_mtime <= m->config->last_modified)
        return;

    gf_config_t old_config = *m->config;
    gf_config_t new_config = load_or_create_config (config_file);

    if (gf_config_changed (&old_config, &new_config))
    {
        GF_LOG_INFO ("Configuration changed! Reloading from: %s", config_file);

        *m->config = new_config;
        m->config->last_modified = st.st_mtime;

        if (old_config.enable_borders != new_config.enable_borders)
        {
            if (!new_config.enable_borders)
            {
                GF_LOG_INFO ("Borders disabled, cleaning up...");
                if (m->platform->border_cleanup)
                    m->platform->border_cleanup (m->platform);
            }
            else
            {
                GF_LOG_INFO ("Borders enabled, adding to all valid windows...");
                if (m->platform->border_cleanup)
                    m->platform->border_cleanup (m->platform);
                // Get all windows from all workspaces to ensure we don't miss any
                for (gf_ws_id_t workspace = 0; workspace < GF_MAX_WORKSPACES; workspace++)
                {
                    gf_win_info_t *workspace_windows = NULL;
                    uint32_t count = 0;

                    if (m->platform->window_enumerate (m->display, &workspace,
                                                       &workspace_windows, &count)
                        != GF_SUCCESS)
                    {
                        GF_LOG_DEBUG ("Failed to get windows for workspace %d",
                                      workspace);
                        continue;
                    }

                    GF_LOG_DEBUG ("Processing workspace %d with %d windows", workspace,
                                  count);

                    for (uint32_t i = 0; i < count; i++)
                    {
                        gf_win_info_t *win = &workspace_windows[i];

                        // Check if window is valid and not excluded
                        if (win->is_valid && !win->is_minimized
                            && !wm_is_excluded (m, win->id))
                        {
                            GF_LOG_DEBUG ("Adding border to window %lu in workspace %d",
                                          (void *)win->id, workspace);

                            if (m->platform->border_add)
                                m->platform->border_add (m->platform, win->id,
                                                         m->config->border_color, 3);
                        }
                        else
                        {
                            GF_LOG_DEBUG ("Skipping window %lu (valid=%d, minimized=%d, "
                                          "excluded=%d)",
                                          (void *)win->id, win->is_valid,
                                          win->is_minimized, wm_is_excluded (m, win->id));
                        }
                    }

                    // Free the window list for this workspace
                    if (workspace_windows)
                        gf_free (workspace_windows);
                }

                // Also check the current workspace windows list as a fallback
                gf_win_list_t *current_windows = wm_windows (m);
                GF_LOG_DEBUG ("Current workspace has %d additional windows",
                              current_windows->count);
                for (uint32_t i = 0; i < current_windows->count; i++)
                {
                    gf_win_info_t *win = &current_windows->items[i];

                    if (win->is_valid && !win->is_minimized
                        && !wm_is_excluded (m, win->id))
                    {
                        // Check if border already exists to avoid duplicates
                        bool has_border = false;
                        // This check will be handled by add_border function now

                        if (m->platform->border_add)
                            m->platform->border_add (m->platform, win->id,
                                                     m->config->border_color, 3);
                    }
                }
            }
        }

        _sync_workspaces (m);
        gf_wm_debug_stats (m);
    }
    else
    {
        m->config->last_modified = st.st_mtime;
    }
}

gf_err_t
gf_wm_run (gf_wm_t *m)
{
    if (!m || !m->state.initialized)
        return GF_ERROR_INVALID_PARAMETER;

    time_t last_stats_time = time (NULL);

    while (true)
    {
        m->state.loop_counter++;
        time_t current_time = time (NULL);

        gf_wm_load_cfg (m);
        gf_wm_watch (m);

        gf_wm_layout_apply (m);
        gf_wm_layout_rebalance (m);
        gf_wm_event (m);

        /* Keymap must run AFTER gf_wm_event so that our workspace switch
         * is not immediately undone by gf_wm_event reading the old focused
         * window and switching back. */
        gf_wm_keymap_event (m);

        if (m->config->enable_borders && m->platform->border_update)
            m->platform->border_update (m->platform, m->config);

        if (m->ipc_handle >= 0)
        {
            gf_ipc_server_process (m->ipc_handle, m);
        }

        if (current_time - m->state.last_cleanup_time >= 1)
        {
            gf_wm_prune (m);
            m->state.last_cleanup_time = current_time;
        }

        gf_usleep (33000);
    }

    return GF_SUCCESS;
}
