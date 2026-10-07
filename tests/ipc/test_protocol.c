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
    test_fuzz ();
    puts ("IPC malformed commands, replies, exact buffers, and 10000 fuzz cases passed");
    return 0;
}
