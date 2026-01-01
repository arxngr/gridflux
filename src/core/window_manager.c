#include "window_manager.h"
#include "../utils/list.h"
#include "../utils/memory.h"
#include "config.h"
#include "internal.h"
#include "logger.h"
#include "types.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

static gf_error_code_t gf_window_manager_calculate_layout (gf_window_manager_t *m,
                                                           gf_window_info_t *windows,
                                                           uint32_t window_count,
                                                           gf_rect_t **out_geometries);
static void gf_window_manager_unmaximize_all (gf_window_manager_t *m,
                                              gf_window_info_t *windows,
                                              uint32_t window_count);
static void gf_window_manager_apply_layout (gf_window_manager_t *m,
                                            gf_window_info_t *windows,
                                            gf_rect_t *geometry, uint32_t window_count);
static gf_error_code_t gf_window_manager_arrange_overflow (gf_window_manager_t *m);
static void gf_window_manager_watch (gf_window_manager_t *m);

gf_error_code_t
gf_window_manager_create (gf_window_manager_t **manager,
                          gf_platform_interface_t *platform,
                          gf_geometry_calculator_t *geometry_calc)
{
    if (!manager || !platform || !geometry_calc)
        return GF_ERROR_INVALID_PARAMETER;

    *manager = gf_calloc (1, sizeof (**manager));
    if (!*manager)
        return GF_ERROR_MEMORY_ALLOCATION;

    (*manager)->platform = platform;
    (*manager)->geometry_calc = geometry_calc;

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
gf_window_manager_destroy (gf_window_manager_t *m)
{
    if (!m)
        return;

    gf_window_list_cleanup (wm_windows (m));
    gf_workspace_list_cleanup (wm_workspaces (m));
    gf_free (m);
}

gf_error_code_t
gf_window_manager_init (gf_window_manager_t *m)
{
    if (!m || !m->platform || !m->platform->init || !m->platform->cleanup)
        return GF_ERROR_INVALID_PARAMETER;

    gf_platform_interface_t *platform = wm_platform (m);

    gf_error_code_t result = platform->init (platform, wm_display (m));
    if (result != GF_SUCCESS)
        return result;

    m->state.last_scan_time = time (NULL);
    m->state.last_cleanup_time = time (NULL);
    m->state.initialized = true;

    GF_LOG_INFO ("Window manager initialized successfully");
    return GF_SUCCESS;
}

void
gf_window_manager_cleanup (gf_window_manager_t *m)
{
    if (!m || !m->state.initialized)
        return;

    gf_platform_interface_t *platform = wm_platform (m);
    platform->cleanup (*wm_display (m), platform);
    m->state.initialized = false;

    GF_LOG_INFO ("Window manager cleaned up");
}

gf_error_code_t
gf_window_manager_update_window_info (gf_window_manager_t *m, gf_native_window_t window,
                                      gf_workspace_id_t workspace_id)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_window_list_t *windows = wm_windows (m);
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    gf_window_id_t id = (gf_window_id_t)window;

    gf_rect_t geom;
    if (platform->get_window_geometry (display, window, &geom) != GF_SUCCESS)
        return GF_ERROR_PLATFORM_ERROR;

    gf_window_info_t info = {
        .id = id,
        .native_handle = window,
        .workspace_id = workspace_id,
        .geometry = geom,
        .is_valid = true,
        .needs_update = true,
        .last_modified = time (NULL),
    };

    gf_window_info_t *existing = gf_window_list_find_by_window_id (windows, id);

    return existing ? gf_window_list_update (windows, &info)
                    : gf_window_list_add (windows, &info);
}

void
gf_window_manager_cleanup_invalid_windows (gf_window_manager_t *m)
{
    if (!m || !wm_platform (m))
        return;

    gf_window_list_t *windows = wm_windows (m);
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    if (!windows || !workspaces)
        return;

    uint32_t removed_windows = 0;

    for (uint32_t i = 0; i < windows->count;)
    {
        gf_window_info_t *win = &windows->items[i];

        bool excluded = wm_is_excluded (m, win->native_handle);
        bool invalid = !wm_is_valid (m, win->native_handle);

        if (excluded || invalid)
        {
            gf_window_list_remove (windows, win->id);
            removed_windows++;
            continue;
        }

        i++;
    }

    if (removed_windows > 0)
    {
        GF_LOG_DEBUG ("Cleaned %u invalid/excluded windows", removed_windows);
    }

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_workspace_info_t *ws = &workspaces->items[i];

        ws->window_count = gf_window_list_count_by_workspace (windows, ws->id);
    }

    for (uint32_t i = 0; i < workspaces->count;)
    {
        gf_workspace_info_t *ws = &workspaces->items[i];

        bool is_active = (ws->id == workspaces->active_workspace);
        bool is_empty = (ws->window_count == 0);
        bool can_remove = (workspaces->count > 1);

        if (is_empty && !is_active && can_remove)
        {
            GF_LOG_INFO ("Removing empty workspace %u", ws->id);

            if (platform->remove_workspace (display, ws->id) == GF_SUCCESS)
            {
                ws->id = -1;
                ws->available_space = 0;
                ws->max_windows = 0;
                ws->window_count = 0;

                continue;
            }
            else
            {
                GF_LOG_WARN ("Failed to remove workspace %u", ws->id);
            }
        }

        i++;
    }
}

void
gf_window_manager_print_stats (const gf_window_manager_t *m)
{
    if (!m)
        return;

    const gf_window_list_t *windows = &m->state.windows;
    const gf_workspace_list_t *workspaces = &m->state.workspaces;

    uint32_t *workspace_counts = gf_calloc (m->config->max_workspaces, sizeof (uint32_t));

    if (!workspace_counts)
    {
        GF_LOG_ERROR ("Failed to allocate workspace_counts");
        return;
    }

    gf_workspace_id_t max_workspace = -1;

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_workspace_id_t ws = windows->items[i].workspace_id;

        if (ws >= 0 && ws < m->config->max_workspaces)
        {
            workspace_counts[ws]++;
            if (ws > max_workspace)
                max_workspace = ws;
        }
    }

    GF_LOG_DEBUG ("Total windows: %u", windows->count);
    GF_LOG_DEBUG ("Total workspaces: %u", workspaces->count);
    char win_name[256];

    for (gf_workspace_id_t i = 0; i <= max_workspace; i++)
    {
        uint32_t count = workspace_counts[i];
        if (count == 0)
            continue;

        uint32_t max_windows = 0;
        int32_t available = -1;

        if (workspaces && i < workspaces->count)
        {
            const gf_workspace_info_t *ws = &workspaces->items[i];
            max_windows = ws->max_windows;
            available = ws->available_space;
        }

        GF_LOG_DEBUG ("WS %d | windows=%u max=%u available=%d", i, count, max_windows,
                      available);

        for (uint32_t w = 0; w < windows->count; w++)
        {
            const gf_window_info_t *win = &windows->items[w];

            if (win->workspace_id != i)
                continue;

            if (m->platform && m->platform->window_name_info)
            {
                m->platform->window_name_info (m->display, win->native_handle, win_name,
                                               sizeof (win_name));
            }
            else
            {
                snprintf (win_name, sizeof (win_name), "N/A");
            }

            GF_LOG_DEBUG ("  Window %u -> WS %d | Name: %s", win->id, win->workspace_id,
                          win_name);
        }
    }

    gf_free (workspace_counts);
}

static gf_error_code_t
gf_window_manager_calculate_layout (gf_window_manager_t *m, gf_window_info_t *windows,
                                    uint32_t window_count, gf_rect_t **out_geometries)
{
    if (!m || !windows || !out_geometries || window_count == 0)
        return GF_ERROR_INVALID_PARAMETER;

    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    gf_rect_t workspace_bounds;
    gf_error_code_t result = platform->get_screen_bounds (display, &workspace_bounds);

    if (result != GF_SUCCESS)
    {
        return GF_ERROR_DISPLAY_CONNECTION;
    }

    gf_rect_t *new_geometries = gf_malloc (window_count * sizeof (gf_rect_t));
    if (!new_geometries)
    {
        return GF_ERROR_MEMORY_ALLOCATION;
    }

    wm_geometry (m)->calculate_layout (wm_geometry (m), windows, window_count,
                                       &workspace_bounds, new_geometries);

    *out_geometries = new_geometries;
    return GF_SUCCESS;
}

static void
gf_window_manager_assign_workspaces (gf_window_manager_t *m)
{
    gf_workspace_list_t *workspaces = wm_workspaces (m);
    gf_window_list_t *windows = wm_windows (m);

    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    /* Reset workspace list */
    workspaces->count = 0;

    uint32_t ws_id = 0;
    uint32_t slot = 0;

    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_window_info_t *win = &windows->items[i];

        if (!win->is_valid)
            continue;

        if (slot >= max_per_ws)
        {
            ws_id++;
            slot = 0;
        }

        while (workspaces->count <= ws_id)
        {
            gf_workspace_info_t ws = { .id = workspaces->count,
                                       .window_count = 0,
                                       .max_windows = max_per_ws,
                                       .available_space = max_per_ws };
            gf_workspace_list_add (workspaces, &ws);
        }

        win->workspace_id = ws_id;

        gf_workspace_info_t *ws = &workspaces->items[ws_id];
        ws->window_count++;
        ws->available_space--;

        slot++;
    }

    if (workspaces->active_workspace >= workspaces->count)
        workspaces->active_workspace = 0;
}

gf_error_code_t
gf_window_manager_arrange_workspace (gf_window_manager_t *m)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_list_t *workspaces = wm_workspaces (m);
    gf_window_list_t *windows = wm_windows (m);

    if (windows->count == 0 || !windows->items)
        return GF_SUCCESS;

    gf_window_manager_assign_workspaces (m);
    gf_window_manager_unmaximize_all (m, windows->items, windows->count);

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_workspace_info_t *ws = &workspaces->items[i];

        gf_window_info_t *ws_windows = NULL;
        uint32_t ws_window_count = 0;

        if (gf_window_list_get_by_workspace (windows, ws->id, &ws_windows,
                                             &ws_window_count)
                != GF_SUCCESS
            || ws_window_count == 0)
        {
            continue;
        }

        gf_rect_t workspace_bounds;
        if (wm_platform (m)->get_screen_bounds (*wm_display (m), &workspace_bounds)
            != GF_SUCCESS)
        {
            gf_free (ws_windows);
            continue;
        }

        gf_rect_t *new_geometries = NULL;
        if (gf_window_manager_calculate_layout (m, ws_windows, ws_window_count,
                                                &new_geometries)
            == GF_SUCCESS)
        {
            gf_window_manager_apply_layout (m, ws_windows, new_geometries,
                                            ws_window_count);
            gf_free (new_geometries);
        }

        gf_free (ws_windows);
    }

    return GF_SUCCESS;
}

gf_error_code_t
gf_window_manager_run (gf_window_manager_t *m)
{
    if (!m || !m->state.initialized)
    {
        return GF_ERROR_INVALID_PARAMETER;
    }

    gf_window_list_t *windows = wm_windows (m);

    GF_LOG_INFO ("Window manager main loop started");

    time_t last_stats_time = time (NULL);

    while (true)
    {
        m->state.loop_counter++;
        time_t current_time = time (NULL);

        gf_window_manager_load_cfg (m);
        gf_window_manager_watch (m);
        gf_window_manager_arrange_overflow (m);
        gf_window_manager_arrange_workspace (m);
        gf_window_manager_event (m);

        // Periodic statistics
        if (current_time - last_stats_time >= 1)
        {
            gf_window_manager_print_stats (m);
            last_stats_time = current_time;
        }

        // Periodic cleanup of invalid windows
        if (current_time - m->state.last_cleanup_time >= 1)
        {
            gf_window_manager_cleanup_invalid_windows (m);
            m->state.last_cleanup_time = current_time;
        }

        // Adaptive sleep based on activity
        uint32_t sleep_time = windows->count > 10 ? 100000 : 80000;
        usleep (sleep_time);
    }

    return GF_SUCCESS;
}

static void
gf_window_manager_unmaximize_all (gf_window_manager_t *m, gf_window_info_t *windows,
                                  uint32_t window_count)
{
    if (!m || !wm_platform (m) || !windows)
        return;

    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    for (uint32_t i = 0; i < window_count; i++)
    {
        if (windows[i].is_maximized)
            platform->unmaximize_window (display, windows[i].native_handle);
    }
}

static void
gf_window_manager_apply_layout (gf_window_manager_t *m, gf_window_info_t *windows,
                                gf_rect_t *geometry, uint32_t window_count)
{
    if (!m || !windows || !geometry || window_count == 0)
        return;

    gf_window_list_t *window_list = wm_windows (m);

    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    gf_error_code_t result;

    for (uint32_t i = 0; i < window_count; i++)
    {
        if (!windows[i].needs_update && !windows[i].is_valid)
            continue;

        result = platform->set_window_geometry (
            display, windows[i].native_handle, &geometry[i],
            GF_GEOMETRY_CHANGE_ALL | GF_GEOMETRY_APPLY_PADDING, m->config);

        if (result != GF_SUCCESS)
        {
            GF_LOG_WARN ("Failed to set geometry for window %llu",
                         (unsigned long long)windows[i].id);
        }
        gf_window_manager_update_window_info (m, windows[i].native_handle,
                                              windows[i].workspace_id);

        gf_window_list_clear_update_flags (window_list, windows[i].workspace_id);
    }
}

gf_error_code_t
gf_window_manager_swap (gf_window_manager_t *m, const gf_window_info_t *src_copy,
                        const gf_window_info_t *dst_copy)
{
    if (!m || !src_copy || !dst_copy)
        return GF_ERROR_INVALID_PARAMETER;

    gf_window_list_t *list = wm_windows (m);
    int src_idx = -1, dst_idx = -1;

    // Match based on unique ID (not pointer)
    for (int i = 0; i < list->count; i++)
    {
        if (list->items[i].id == src_copy->id)
            src_idx = i;
        if (list->items[i].id == dst_copy->id)
            dst_idx = i;
    }

    if (src_idx < 0 || dst_idx < 0)
    {
        GF_LOG_WARN ("Swap aborted: one or both windows not found in manager list "
                     "(src_id=%d, dst_id=%d)",
                     src_copy->id, dst_copy->id);
        return GF_ERROR_INVALID_PARAMETER;
    }

    // Swap in the actual list
    gf_window_info_t temp = list->items[src_idx];
    list->items[src_idx] = list->items[dst_idx];
    list->items[dst_idx] = temp;

    // Recalculate layout
    gf_rect_t *new_geometries = NULL;
    gf_error_code_t result = gf_window_manager_calculate_layout (
        m, list->items, list->count, &new_geometries);
    if (result != GF_SUCCESS)
        return result;

    gf_window_manager_unmaximize_all (m, list->items, list->count);
    gf_window_manager_apply_layout (m, list->items, new_geometries, list->count);

    gf_free (new_geometries);
    return GF_SUCCESS;
}

static void
gf_window_manager_sync_workspaces (gf_window_manager_t *m)
{
    gf_workspace_list_t *workspaces = wm_workspaces (m);
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    uint32_t platform_count = platform->get_workspace_count (display);

    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    for (uint32_t i = 0; i < platform_count; i++)
    {
        gf_workspace_list_ensure (workspaces, i, max_per_ws);

        gf_workspace_info_t *ws = &workspaces->items[i];
        ws->max_windows = max_per_ws;
    }
}

static gf_error_code_t
gf_window_manager_arrange_overflow (gf_window_manager_t *m)
{
    if (!m)
        return GF_ERROR_INVALID_PARAMETER;

    gf_workspace_list_t *workspaces = wm_workspaces (m);
    gf_window_list_t *windows = wm_windows (m);
    uint32_t max_per_ws = m->config->max_windows_per_workspace;

    gf_workspace_id_t active_ws = workspaces->active_workspace;

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_workspace_info_t *src_ws = &workspaces->items[i];

        if (src_ws->window_count <= max_per_ws)
            continue;

        uint32_t overflow = src_ws->window_count - max_per_ws;

        for (uint32_t m = 0; m < overflow; m++)
        {
            gf_workspace_id_t dst_id = workspaces->count;
            gf_workspace_list_ensure (workspaces, dst_id, max_per_ws);

            gf_window_info_t *list = NULL;
            uint32_t count = 0;

            if (gf_window_list_get_by_workspace (windows, src_ws->id, &list, &count)
                    != GF_SUCCESS
                || count == 0)
            {
                gf_free (list);
                break;
            }

            gf_window_info_t *win = &list[0];

            for (uint32_t w = 0; w < windows->count; w++)
            {
                if (windows->items[w].id == win->id)
                {
                    windows->items[w].workspace_id = dst_id;
                    break;
                }
            }

            gf_free (list);
        }
    }

    gf_workspace_list_rebuild_stats (workspaces, windows);
    return GF_SUCCESS;
}

static void
gf_window_manager_watch (gf_window_manager_t *m)
{
    if (!m)
        return;

    gf_workspace_list_t *workspaces = wm_workspaces (m);
    gf_window_list_t *windows = wm_windows (m);

    gf_window_manager_sync_workspaces (m);

    for (uint32_t ws_id = 0; ws_id < workspaces->count; ws_id++)
    {
        gf_window_info_t *platform_windows = NULL;
        uint32_t count = 0;

        if (wm_platform (m)->get_windows (*wm_display (m), (gf_workspace_id_t *)&ws_id,
                                          &platform_windows, &count)
            != GF_SUCCESS)
            continue;

        for (uint32_t i = 0; i < count; i++)
        {
            gf_window_info_t *existing
                = gf_window_list_find_by_window_id (windows, platform_windows[i].id);

            if (!existing && platform_windows[i].is_valid)
            {
                platform_windows[i].workspace_id = workspaces->active_workspace;

                gf_window_list_add (windows, &platform_windows[i]);

                char win_name[256] = { 0 };
                wm_platform (m)->window_name_info (
                    *wm_display (m), platform_windows[i].id, win_name, sizeof (win_name));
                GF_LOG_DEBUG ("Added window: win=%lu | WS=%u | Name='%s'",
                              platform_windows[i].id, platform_windows[i].workspace_id,
                              win_name);
            }
        }

        gf_free (platform_windows);
    }
}

void
gf_window_manager_load_cfg (gf_window_manager_t *m)
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

    // Check if file has been modified
    if (st.st_mtime <= m->config->last_modified)
    {
        return;
    }

    usleep (10000); // Small delay

    // Reload and check for changes
    gf_config_t old_config = *m->config;
    gf_config_t new_config = load_or_create_config (config_file);

    if (gf_config_has_changed (&old_config, &new_config))
    {
        GF_LOG_INFO ("Configuration changed! Reloading from: %s", config_file);

        *m->config = new_config;
        m->config->last_modified = st.st_mtime;

        gf_window_manager_sync_workspaces (m);
    }
    else
    {
        m->config->last_modified = st.st_mtime;
    }
}

void
gf_window_manager_event (gf_window_manager_t *m)
{
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);
    gf_window_list_t *windows = wm_windows (m);
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    gf_window_id_t curr_win_id = platform->get_active_window (display);

    if (curr_win_id == 0)
        return;

    gf_window_info_t *focused = gf_window_list_find_by_window_id (windows, curr_win_id);

    if (!focused)
    {
        gf_window_info_t win = { 0 };
        win.id = curr_win_id;
        win.native_handle = (gf_native_window_t)curr_win_id;
        win.is_valid = true;
        win.workspace_id = gf_workspace_list_find_free (
            workspaces, m->config->max_windows_per_workspace);

        if (gf_window_list_add (windows, &win) == GF_SUCCESS)
        {
            return;
        }
    }

    gf_window_info_t *workspace_windows = NULL;
    uint32_t workspace_win_count = 0;

    if (gf_window_list_get_by_workspace (windows, focused->workspace_id,
                                         &workspace_windows, &workspace_win_count)
        != GF_SUCCESS)
        return;

    for (uint32_t i = 0; i < workspaces->count; i++)
    {
        gf_workspace_id_t ws_id = workspaces->items[i].id;

        if (ws_id == focused->workspace_id)
            continue;

        gf_window_info_t *list = NULL;
        uint32_t count = 0;

        if (gf_window_list_get_by_workspace (windows, ws_id, &list, &count) != GF_SUCCESS)
            continue;

        for (uint32_t w = 0; w < count; w++)
        {
            gf_window_info_t *win = &list[w];

            if (wm_is_excluded (m, win->native_handle))
            {
                continue;
            }

            platform->minimize_window (display, win->native_handle);
        }

        gf_free (list);
    }

    for (uint32_t i = 0; i < workspace_win_count; i++)
    {
        gf_window_info_t *win = &workspace_windows[i];

        if (wm_is_excluded (m, win->native_handle))
        {
            GF_LOG_DEBUG ("Skip unminimize (excluded): win=%lu ws=%u", win->native_handle,
                          win->workspace_id);
            continue;
        }

        platform->unminimize_window (display, win->native_handle);
    }

    gf_free (workspace_windows);
}
