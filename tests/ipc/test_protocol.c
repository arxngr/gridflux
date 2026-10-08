#include "core/internal.h"
#include "ipc/ipc.h"
#include "ipc/ipc_command.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Parser tests use the real core and never open the live IPC endpoint.
gf_ipc_handle_t
gf_ipc_server_create (void)
{
    return -1;
}
void
gf_ipc_server_destroy (gf_ipc_handle_t handle)
{
    (void)handle;
}
bool
gf_ipc_server_process (gf_ipc_handle_t handle, void *data)
{
    (void)handle;
    (void)data;
    return false;
}
void
gf_log (gf_log_level_t level, const char *format, ...)
{
    (void)level;
    (void)format;
}

static void
test_commands (void)
{
    gf_config_t config = { .max_windows_per_workspace = 4, .max_workspaces = 32 };
    gf_platform_t platform = { 0 };
    gf_wm_t manager = { .config = &config, .platform = &platform };
    const char *invalid[] = { "",
                              "lock 9999999999999999999999",
                              "unlock -1",
                              "lock 1 trailing",
                              "query count 2147483648",
                              "query count 3garbage",
                              "move -1 3",
                              "move 0x10000000000000000 3",
                              "move 0x1234 3 trailing",
                              "rule add abc 99999999999999999999999",
                              "rule add abc 2 16",
                              "rule remove abc trailing",
                              "toggle-borders\nlock 1",
                              "query\r count",
                              "lock 1\0unlock 2",
                              "exclude add \x01malicious" };
    for (size_t i = 0; i < sizeof (invalid) / sizeof (*invalid); i++)
    {
        gf_ipc_response_t response;
        memset (&response, 0xa5, sizeof (response));
        size_t length = i == 14 ? sizeof ("lock 1\0unlock 2") - 1 : strlen (invalid[i]);
        gf_handle_client_message (invalid[i], length, &response, &manager);
        assert (response.status == GF_IPC_ERROR_INVALID_COMMAND);
        assert (!config.window_rules_count && !config.excluded_apps.count);
    }
    char oversized[GF_IPC_MSG_SIZE];
    memset (oversized, 'a', sizeof (oversized));
    gf_ipc_response_t response;
    gf_handle_client_message (oversized, sizeof (oversized), &response, &manager);
    assert (response.status == GF_IPC_ERROR_INVALID_COMMAND);
    gf_handle_client_message (oversized, 64, &response, &manager);
    assert (response.status == GF_IPC_ERROR_INVALID_COMMAND);
    char exclusion[300] = "exclude add ";
    memset (exclusion + strlen (exclusion), 'x', 200);
    gf_handle_client_message (exclusion, strlen (exclusion), &response, &manager);
    assert (response.status == GF_IPC_ERROR_INVALID_COMMAND
            && !config.excluded_apps.count);
    gf_handle_client_message ("query count", 11, &response, &manager);
    assert (response.status == GF_IPC_SUCCESS);
    gf_command_response_t command;
    assert (gf_parse_command_response (response.message, sizeof (response.message),
                                       &command));
    assert (strcmp (command.message, "Total windows: 0") == 0);
    size_t end = offsetof (gf_command_response_t, message) + strlen (command.message);
    for (size_t i = end; i < sizeof (response.message); i++)
        assert (response.message[i] == 0); // No stack bytes after the text.
    // Exactly length readable bytes, without a terminator, is a valid request.
    char *exact = malloc (11);
    assert (exact);
    memcpy (exact, "query count", 11);
    gf_handle_client_message (exact, 11, &response, &manager);
    assert (response.status == GF_IPC_SUCCESS);
    free (exact);
    gf_handle_client_message (NULL, 1, &response, &manager);
    assert (response.status == GF_IPC_ERROR_INVALID_COMMAND);
    gf_handle_client_message ("query count", 11, &response, NULL);
    assert (response.status == GF_IPC_ERROR_INVALID_COMMAND);
    gf_handle_client_message ("query count", 11, NULL, &manager);
}

static void
test_lists (void)
{
    char buffer[GF_IPC_MSG_SIZE] = { 0 };
    const size_t wh = 2 * sizeof (uint32_t);
    const size_t sh = wh + sizeof (((gf_ws_list_t *)0)->active_workspace);
    uint32_t one = 1, huge = UINT32_MAX;
    memcpy (buffer, &huge, sizeof (huge));
    memcpy (buffer + sizeof (huge), &huge, sizeof (huge));
    assert (!gf_parse_window_list (buffer, sizeof (buffer)));
    assert (!gf_parse_workspace_list (buffer, sizeof (buffer)));
    memcpy (buffer, &one, sizeof (one));
    gf_win_info_t window = { .monitor_id = 2, .name = "terminal" };
    memcpy (buffer + wh, &window, sizeof (window));
    for (size_t length = 0; length < wh + sizeof (window); length++)
        assert (!gf_parse_window_list (buffer, length));
    gf_win_list_t *windows = gf_parse_window_list (buffer, wh + sizeof (window));
    assert (windows && windows->count == 1 && windows->capacity == 1);
    gf_free_window_list (windows);
    buffer[wh + offsetof (gf_win_info_t, is_maximized)] = 2;
    assert (!gf_parse_window_list (buffer, sizeof (buffer)));
    buffer[wh + offsetof (gf_win_info_t, is_maximized)] = 0;
    memset (buffer + wh + offsetof (gf_win_info_t, name), 'x', sizeof (window.name));
    assert (!gf_parse_window_list (buffer, sizeof (buffer)));
    memset (buffer, 0, sizeof (buffer));
    memcpy (buffer, &one, sizeof (one));
    memcpy (buffer + sizeof (one), &one, sizeof (one));
    gf_ws_info_t workspace
        = { .id = 3, .monitor_id = GF_MONITOR_SHARED, .is_excluded_ws = true };
    memcpy (buffer + sh, &workspace, sizeof (workspace));
    assert (!gf_parse_workspace_list (buffer, sh + sizeof (workspace) - 1));
    gf_ws_list_t *workspaces = gf_parse_workspace_list (buffer, sh + sizeof (workspace));
    assert (workspaces && workspaces->items[0].is_excluded_ws);
    gf_free_workspace_list (workspaces);
    buffer[sh + offsetof (gf_ws_info_t, is_excluded_ws)] = 0;
    assert (!gf_parse_workspace_list (buffer, sizeof (buffer)));
    assert (!gf_parse_window_list (NULL, sizeof (buffer)));
    assert (!gf_parse_workspace_list (buffer, sizeof (buffer) + 1));
    memset (buffer, 0, sizeof (buffer));
    memcpy (buffer, &one, sizeof (one));
    gf_monitor_t source = { .id = 2, .is_primary = true };
    memcpy (buffer + sizeof (one), &source, sizeof (source));
    gf_monitor_t monitors[GF_MAX_MONITORS];
    uint32_t count = GF_MAX_MONITORS;
    assert (gf_parse_monitor_list (buffer, sizeof (buffer), monitors, count, &count));
    assert (count == 1 && monitors[0].id == 2);
    assert (
        !gf_parse_monitor_list (buffer, sizeof (one), monitors, GF_MAX_MONITORS, &count));
    source.id = GF_MAX_MONITORS;
    memcpy (buffer + sizeof (one), &source, sizeof (source));
    assert (!gf_parse_monitor_list (buffer, sizeof (buffer), monitors, GF_MAX_MONITORS,
                                    &count));
    source.id = 2;
    memcpy (buffer + sizeof (one), &source, sizeof (source));
    buffer[sizeof (one) + offsetof (gf_monitor_t, is_primary)] = 2;
    assert (!gf_parse_monitor_list (buffer, sizeof (buffer), monitors, GF_MAX_MONITORS,
                                    &count));
    memcpy (buffer + sizeof (one), &source, sizeof (source));
    memcpy (buffer + sizeof (one) + sizeof (source), &source, sizeof (source));
    uint32_t two = 2;
    memcpy (buffer, &two, sizeof (two));
    assert (!gf_parse_monitor_list (buffer, sizeof (buffer), monitors, GF_MAX_MONITORS,
                                    &count));
    memset (buffer, 'x', sizeof (buffer));
    gf_command_response_t text;
    assert (!gf_parse_command_response (buffer, sizeof (buffer), &text));
}

static void
test_query_roundtrip (void)
{
    // Preserve the actual GUI/CLI reply layout and ownership/state fields for
    // normal, maximized and shared excluded workspaces on sparse monitors.
    gf_config_t config = { 0 };
    gf_platform_t platform = { 0 };
    gf_wm_t manager = { .config = &config, .platform = &platform };
    gf_win_info_t source_windows[3];
    gf_ws_info_t source_workspaces[3];
    memset (source_windows, 0, sizeof (source_windows));
    memset (source_workspaces, 0, sizeof (source_workspaces));
    gf_ws_id_t maximized_id = GF_MAX_WORKSPACES_TOTAL + 1;
    gf_ws_id_t excluded_id = GF_MAX_WORKSPACES_TOTAL + 2;
    source_workspaces[0].id = source_workspaces[0].local_id = 1;
    source_workspaces[0].window_count = 1;
    source_workspaces[0].max_windows = 4;
    source_workspaces[0].available_space = 3;
    source_workspaces[0].is_custom_layout = true;
    source_workspaces[1].id = maximized_id;
    source_workspaces[1].monitor_id = 2;
    source_workspaces[1].window_count = source_workspaces[1].max_windows = 1;
    source_workspaces[1].has_maximized_state = true;
    source_workspaces[2].id = excluded_id;
    source_workspaces[2].monitor_id = GF_MONITOR_SHARED;
    source_workspaces[2].window_count = 1;
    source_workspaces[2].max_windows = INT32_MAX;
    source_workspaces[2].is_excluded_ws = true;
    for (uint32_t i = 0; i < 3; i++)
    {
        gf_win_info_t *win = &source_windows[i];
        win->id = (gf_handle_t)(uintptr_t)(0x1000 + i);
        win->monitor_id = i ? 2 : 0;
        win->workspace_id = source_workspaces[i].id;
        win->restore_workspace_id
            = gf_workspace_id_for_monitor_local (win->monitor_id, 3);
        win->is_valid = true;
        win->geometry.x = i ? 1920 : 0;
        win->geometry.width = i ? 2560 : 1920;
        win->geometry.height = 1080;
        snprintf (win->name, sizeof (win->name), "app-%u|app-%u.exe", i, i);
    }
    source_windows[1].is_maximized = true;
    source_windows[1].monitor_return.pending = true;
    source_windows[1].monitor_return.monitor_id = 2;
    source_windows[1].monitor_return.geometry = source_windows[1].geometry;
    source_windows[2].is_minimized = source_windows[2].monitor_suspended = true;
    manager.state.windows.items = source_windows;
    manager.state.windows.count = 3;
    manager.state.windows.capacity = 16;
    manager.state.workspaces.items = source_workspaces;
    manager.state.workspaces.count = 3;
    manager.state.workspaces.capacity = 16;
    manager.state.workspaces.active_workspace[0] = 1;
    manager.state.workspaces.active_workspace[2] = maximized_id;
    manager.state.monitor_count = 3;
    manager.state.monitors[0].id = 0;
    manager.state.monitors[0].is_primary = true;
    manager.state.monitors[0].full_bounds = source_windows[0].geometry;
    manager.state.monitors[0].bounds = source_windows[0].geometry;
    manager.state.monitors[2].id = 2;
    manager.state.monitors[2].full_bounds = source_windows[1].geometry;
    manager.state.monitors[2].bounds = source_windows[1].geometry;
    unsigned char snapshot[sizeof (manager)];
    memcpy (snapshot, &manager, sizeof (manager));
    const char *window_queries[] = { "query windows", "query W", "query windows 3" };
    gf_ipc_response_t response;
    for (size_t q = 0; q < sizeof (window_queries) / sizeof (*window_queries); q++)
    {
        gf_handle_client_message (window_queries[q], strlen (window_queries[q]),
                                  &response, &manager);
        assert (response.status == GF_IPC_SUCCESS);
        gf_win_list_t *windows
            = gf_parse_window_list (response.message, sizeof (response.message));
        assert (windows && windows->count == 3 && windows->capacity == 3);
        assert (memcmp (windows->items, source_windows, sizeof (source_windows)) == 0);
        gf_free_window_list (windows);
    }
    const char *workspace_queries[] = { "query workspaces", "query D" };
    for (size_t q = 0; q < sizeof (workspace_queries) / sizeof (*workspace_queries); q++)
    {
        gf_handle_client_message (workspace_queries[q], strlen (workspace_queries[q]),
                                  &response, &manager);
        assert (response.status == GF_IPC_SUCCESS);
        gf_ws_list_t *workspaces
            = gf_parse_workspace_list (response.message, sizeof (response.message));
        assert (workspaces && workspaces->count == 3 && workspaces->capacity == 3);
        assert (memcmp (workspaces->items, source_workspaces, sizeof (source_workspaces))
                == 0);
        assert (memcmp (workspaces->active_workspace,
                        manager.state.workspaces.active_workspace,
                        sizeof (workspaces->active_workspace))
                == 0);
        gf_free_workspace_list (workspaces);
    }
    gf_handle_client_message ("query monitors", 14, &response, &manager);
    gf_monitor_t monitors[GF_MAX_MONITORS];
    uint32_t count;
    assert (response.status == GF_IPC_SUCCESS
            && gf_parse_monitor_list (response.message, sizeof (response.message),
                                      monitors, GF_MAX_MONITORS, &count));
    assert (count == 2
            && memcmp (&monitors[0], &manager.state.monitors[0], sizeof (*monitors)) == 0
            && memcmp (&monitors[1], &manager.state.monitors[2], sizeof (*monitors))
                   == 0);
    gf_handle_client_message ("query T 1", 9, &response, &manager);
    gf_command_response_t result;
    assert (
        gf_parse_command_response (response.message, sizeof (response.message), &result));
    assert (strcmp (result.message, "Workspace 1 has 1 windows") == 0);
    assert (memcmp (snapshot, &manager, sizeof (manager)) == 0);
}

static void
test_fuzz (void)
{
    gf_config_t config = { 0 };
    gf_platform_t platform = { 0 };
    gf_wm_t manager = { .config = &config, .platform = &platform };
    uint32_t random = 0x12345678;
    for (unsigned round = 0; round < 10000; round++)
    {
        size_t length = round % GF_IPC_MSG_SIZE;
        char *buffer = malloc (length ? length : 1);
        assert (buffer);
        for (size_t i = 0; i < length; i++)
        {
            random = random * 1664525u + 1013904223u;
            buffer[i] = (char)(random >> 24);
        }
        struct
        {
            uint32_t before;
            gf_ipc_response_t response;
            uint32_t after;
        } result;
        result.before = result.after = 0xfeedabcd;
        gf_handle_client_message (buffer, length, &result.response, &manager);
        assert (result.before == 0xfeedabcd && result.after == 0xfeedabcd);
        assert (gf_ipc_response_valid (&result.response));
        gf_ws_list_t *ws = gf_parse_workspace_list (buffer, length);
        if (ws)
        {
            gf_free_workspace_list (ws);
        }
        gf_win_list_t *win = gf_parse_window_list (buffer, length);
        if (win)
        {
            gf_free_window_list (win);
        }
        gf_monitor_t monitors[GF_MAX_MONITORS];
        uint32_t count;
        gf_parse_monitor_list (buffer, length, monitors, GF_MAX_MONITORS, &count);
        gf_command_response_t response;
        gf_parse_command_response (buffer, length, &response);
        free (buffer);
    }
}

int
main (void)
{
    test_commands ();
    test_lists ();
    test_query_roundtrip ();
    test_fuzz ();
    puts ("IPC malformed commands, replies, exact buffers, and 10000 fuzz cases passed");
    return 0;
}
