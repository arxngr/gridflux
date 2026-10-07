#include "ipc_command.h"
#include "../config/excludes.h"
#include "../config/rules.h"
#include "../core/internal.h"
#include "../core/wm.h"
#include "../utils/logger.h"
#include "../utils/memory.h"
#include "ipc.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// How many fixed-layout records fit in the reply payload after a header of
// `header_bytes`. Used to cap every list we marshal into response->message so a
// large window/workspace count can never overrun the fixed buffer.
static uint32_t
ipc_max_records (size_t header_bytes, size_t record_size)
{
    if (record_size == 0 || GF_IPC_MSG_SIZE <= header_bytes)
        return 0;
    return (uint32_t)((GF_IPC_MSG_SIZE - header_bytes) / record_size);
}

static bool
parse_command (const char *input, char *command, char *args, size_t args_size)
{
    while (isspace ((unsigned char)*input))
        input++;
    size_t n = 0;
    while (*input && !isspace ((unsigned char)*input))
    {
        if (n == 63)
            return false;
        command[n++] = *input++;
    }
    command[n] = '\0';
    while (isspace ((unsigned char)*input))
        input++;
    size_t len = strlen (input);
    if (!n || len >= args_size)
        return false;
    memcpy (args, input, len + 1);
    return true;
}

static bool
parse_number (const char *token, int maximum, int *value)
{
    if (!token || !*token || *token == '-' || *token == '+')
        return false;
    char *end;
    errno = 0;
    unsigned long parsed = strtoul (token, &end, 10);
    while (isspace ((unsigned char)*end))
        end++;
    if (errno || end == token || *end || parsed > (unsigned long)maximum)
        return false;
    *value = (int)parsed;
    return true;
}

static size_t
append_text (char *buffer, size_t size, size_t pos, const char *format, ...)
{
    if (pos >= size - 1)
        return size - 1;
    va_list args;
    va_start (args, format);
    int n = vsnprintf (buffer + pos, size - pos, format, args);
    va_end (args);
    if (n < 0)
    {
        buffer[pos] = '\0';
        return pos;
    }
    return (size_t)n >= size - pos ? size - 1 : pos + (size_t)n;
}

static void
write_window (char *buffer, const gf_win_info_t *source)
{
    memset (buffer, 0, sizeof (*source));
    memcpy (buffer + offsetof (gf_win_info_t, id), &source->id, sizeof (source->id));
    memcpy (buffer + offsetof (gf_win_info_t, workspace_id), &source->workspace_id,
            sizeof (source->workspace_id));
    memcpy (buffer + offsetof (gf_win_info_t, restore_workspace_id),
            &source->restore_workspace_id, sizeof (source->restore_workspace_id));
    memcpy (buffer + offsetof (gf_win_info_t, monitor_id), &source->monitor_id,
            sizeof (source->monitor_id));
    memcpy (buffer + offsetof (gf_win_info_t, geometry), &source->geometry,
            sizeof (source->geometry));
    memcpy (buffer + offsetof (gf_win_info_t, is_maximized), &source->is_maximized,
            sizeof (source->is_maximized));
    memcpy (buffer + offsetof (gf_win_info_t, is_minimized), &source->is_minimized,
            sizeof (source->is_minimized));
    memcpy (buffer + offsetof (gf_win_info_t, monitor_suspended),
            &source->monitor_suspended, sizeof (source->monitor_suspended));
    memcpy (buffer + offsetof (gf_win_info_t, visibility_request),
            &source->visibility_request, sizeof (source->visibility_request));
    memcpy (buffer + offsetof (gf_win_info_t, visibility_wait), &source->visibility_wait,
            sizeof (source->visibility_wait));
    memcpy (buffer + offsetof (gf_win_info_t, visibility_attempts),
            &source->visibility_attempts, sizeof (source->visibility_attempts));
    memcpy (buffer + offsetof (gf_win_info_t, visibility_settle),
            &source->visibility_settle, sizeof (source->visibility_settle));
    memcpy (buffer + offsetof (gf_win_info_t, mode_wait), &source->mode_wait,
            sizeof (source->mode_wait));
    memcpy (buffer + offsetof (gf_win_info_t, needs_update), &source->needs_update,
            sizeof (source->needs_update));
    memcpy (buffer + offsetof (gf_win_info_t, arrange_failures),
            &source->arrange_failures, sizeof (source->arrange_failures));
    memcpy (buffer + offsetof (gf_win_info_t, maximize_fill_failures),
            &source->maximize_fill_failures, sizeof (source->maximize_fill_failures));
    memcpy (buffer + offsetof (gf_win_info_t, monitor_restore_failures),
            &source->monitor_restore_failures, sizeof (source->monitor_restore_failures));
    memcpy (buffer + offsetof (gf_win_info_t, rule_move_failures),
            &source->rule_move_failures, sizeof (source->rule_move_failures));
    memcpy (buffer + offsetof (gf_win_info_t, monitor_return)
                + offsetof (gf_monitor_return_t, pending),
            &source->monitor_return.pending, sizeof (source->monitor_return.pending));
    memcpy (buffer + offsetof (gf_win_info_t, monitor_return)
                + offsetof (gf_monitor_return_t, cancelled),
            &source->monitor_return.cancelled, sizeof (source->monitor_return.cancelled));
    memcpy (buffer + offsetof (gf_win_info_t, monitor_return)
                + offsetof (gf_monitor_return_t, failures),
            &source->monitor_return.failures, sizeof (source->monitor_return.failures));
    memcpy (buffer + offsetof (gf_win_info_t, monitor_return)
                + offsetof (gf_monitor_return_t, monitor_id),
            &source->monitor_return.monitor_id,
            sizeof (source->monitor_return.monitor_id));
    memcpy (buffer + offsetof (gf_win_info_t, monitor_return)
                + offsetof (gf_monitor_return_t, workspace_id),
            &source->monitor_return.workspace_id,
            sizeof (source->monitor_return.workspace_id));
    memcpy (buffer + offsetof (gf_win_info_t, monitor_return)
                + offsetof (gf_monitor_return_t, restore_workspace_id),
            &source->monitor_return.restore_workspace_id,
            sizeof (source->monitor_return.restore_workspace_id));
    memcpy (buffer + offsetof (gf_win_info_t, monitor_return)
                + offsetof (gf_monitor_return_t, geometry),
            &source->monitor_return.geometry, sizeof (source->monitor_return.geometry));
    memcpy (buffer + offsetof (gf_win_info_t, monitor_return)
                + offsetof (gf_monitor_return_t, bounds),
            &source->monitor_return.bounds, sizeof (source->monitor_return.bounds));
    memcpy (buffer + offsetof (gf_win_info_t, is_valid), &source->is_valid,
            sizeof (source->is_valid));
    memcpy (buffer + offsetof (gf_win_info_t, last_modified), &source->last_modified,
            sizeof (source->last_modified));
    size_t length = 0;
    while (length < sizeof (source->name) - 1 && source->name[length])
        length++;
    memcpy (buffer + offsetof (gf_win_info_t, name), source->name, length);
}

static void
write_workspace (char *buffer, const gf_ws_info_t *source)
{
    memset (buffer, 0, sizeof (*source));
    memcpy (buffer + offsetof (gf_ws_info_t, id), &source->id, sizeof (source->id));
    memcpy (buffer + offsetof (gf_ws_info_t, local_id), &source->local_id,
            sizeof (source->local_id));
    memcpy (buffer + offsetof (gf_ws_info_t, rule_target_id), &source->rule_target_id,
            sizeof (source->rule_target_id));
    memcpy (buffer + offsetof (gf_ws_info_t, monitor_id), &source->monitor_id,
            sizeof (source->monitor_id));
    memcpy (buffer + offsetof (gf_ws_info_t, window_count), &source->window_count,
            sizeof (source->window_count));
    memcpy (buffer + offsetof (gf_ws_info_t, max_windows), &source->max_windows,
            sizeof (source->max_windows));
    memcpy (buffer + offsetof (gf_ws_info_t, available_space), &source->available_space,
            sizeof (source->available_space));
    memcpy (buffer + offsetof (gf_ws_info_t, is_locked), &source->is_locked,
            sizeof (source->is_locked));
    memcpy (buffer + offsetof (gf_ws_info_t, has_maximized_state),
            &source->has_maximized_state, sizeof (source->has_maximized_state));
    memcpy (buffer + offsetof (gf_ws_info_t, is_custom_layout), &source->is_custom_layout,
            sizeof (source->is_custom_layout));
    memcpy (buffer + offsetof (gf_ws_info_t, has_rule), &source->has_rule,
            sizeof (source->has_rule));
    memcpy (buffer + offsetof (gf_ws_info_t, is_excluded_ws), &source->is_excluded_ws,
            sizeof (source->is_excluded_ws));
}

static void
write_monitor (char *buffer, const gf_monitor_t *source)
{
    memset (buffer, 0, sizeof (*source));
    memcpy (buffer + offsetof (gf_monitor_t, id), &source->id, sizeof (source->id));
    memcpy (buffer + offsetof (gf_monitor_t, bounds), &source->bounds,
            sizeof (source->bounds));
    memcpy (buffer + offsetof (gf_monitor_t, full_bounds), &source->full_bounds,
            sizeof (source->full_bounds));
    memcpy (buffer + offsetof (gf_monitor_t, is_primary), &source->is_primary,
            sizeof (source->is_primary));
}

static void
cmd_query_windows (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_wm_t *m = (gf_wm_t *)user_data;
    gf_win_list_t *windows = wm_windows (m);
    gf_platform_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    // Ensure names are populated
    for (uint32_t i = 0; i < windows->count; i++)
    {
        gf_win_info_t *w = &windows->items[i];
        if (!w->name[0])
        {
            gf_wm_window_class (m, w->id, w->name, sizeof (w->name) - 1);
        }
    }

    uint32_t max_items = ipc_max_records (2 * sizeof (uint32_t), sizeof (gf_win_info_t));
    uint32_t send_count = (windows->count < max_items) ? windows->count : max_items;
    if (send_count < windows->count)
        GF_LOG_WARN ("query windows: truncating %u -> %u to fit IPC buffer",
                     windows->count, send_count);

    size_t offset = 0;
    memcpy (response->message + offset, &send_count, sizeof (uint32_t));
    offset += sizeof (uint32_t);
    memcpy (response->message + offset, &windows->capacity, sizeof (uint32_t));
    offset += sizeof (uint32_t);
    for (uint32_t i = 0; i < send_count; i++)
        write_window (response->message + offset + i * sizeof (gf_win_info_t),
                      &windows->items[i]);
}

static void
cmd_query_workspaces (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_wm_t *m = (gf_wm_t *)user_data;
    gf_ws_list_t *workspaces = wm_workspaces (m);
    gf_platform_t *platform = wm_platform (m);

    if (!platform)
    {
        snprintf (response->message, sizeof (response->message),
                  "{\"type\":\"error\",\"message\":\"Platform not initialized\"}");
        return;
    }

    size_t header = 2 * sizeof (uint32_t) + sizeof (workspaces->active_workspace);
    uint32_t max_items = ipc_max_records (header, sizeof (gf_ws_info_t));
    uint32_t send_count = (workspaces->count < max_items) ? workspaces->count : max_items;

    size_t offset = 0;
    memcpy (response->message + offset, &send_count, sizeof (uint32_t));
    offset += sizeof (uint32_t);
    memcpy (response->message + offset, &workspaces->capacity, sizeof (uint32_t));
    offset += sizeof (uint32_t);
    memcpy (response->message + offset, &workspaces->active_workspace,
            sizeof (workspaces->active_workspace));
    offset += sizeof (workspaces->active_workspace);
    for (uint32_t i = 0; i < send_count; i++)
        write_workspace (response->message + offset + i * sizeof (gf_ws_info_t),
                         &workspaces->items[i]);
}

static void
cmd_query_monitors (const char *args, gf_ipc_response_t *response, void *user_data)
{
    (void)args;
    gf_wm_t *m = user_data;
    gf_monitor_t connected[GF_MAX_MONITORS] = { 0 };
    uint32_t count = 0;
    for (uint32_t i = 0; i < m->state.monitor_count && i < GF_MAX_MONITORS; i++)
    {
        const gf_monitor_t *monitor = &m->state.monitors[i];
        if (monitor->full_bounds.width && monitor->full_bounds.height)
            connected[count++] = *monitor;
    }
    memcpy (response->message, &count, sizeof (count));
    for (uint32_t i = 0; i < count; i++)
        write_monitor (response->message + sizeof (count) + i * sizeof (*connected),
                       &connected[i]);
}

static void
cmd_query_count (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_wm_t *m = (gf_wm_t *)user_data;
    gf_win_list_t *windows = wm_windows (m);

    gf_command_response_t resp = { 0 };
    resp.type = 0;

    if (args && *args)
    {
        int workspace_id;
        if (!parse_number (args, INT_MAX, &workspace_id))
        {
            response->status = GF_IPC_ERROR_INVALID_COMMAND;
            resp.type = 1;
            snprintf (resp.message, sizeof (resp.message), "Invalid workspace ID");
            memcpy (response->message, &resp, sizeof (resp));
            return;
        }
        uint32_t count = gf_window_list_count_by_workspace (windows, workspace_id);
        snprintf (resp.message, sizeof (resp.message), "Workspace %d has %u windows",
                  workspace_id, count);
    }
    else
    {
        snprintf (resp.message, sizeof (resp.message), "Total windows: %u",
                  windows->count);
    }

    memcpy (response->message, &resp, sizeof (resp));
}

static void
cmd_move_window (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_wm_t *m = (gf_wm_t *)user_data;

    gf_handle_t window_id = 0;
    int target_workspace = -1;

    gf_command_response_t resp = { 0 };

    char handle_token[32], workspace_token[32], extra[2];
    int fields = sscanf (args, "%31s %31s %1s", handle_token, workspace_token, extra);
    char *end = NULL;
    errno = 0;
    unsigned long long raw_handle = fields == 2 ? strtoull (handle_token, &end, 16) : 0;
    if (fields != 2 || !raw_handle || errno || !end || *end || handle_token[0] == '-'
        || handle_token[0] == '+' || raw_handle > UINTPTR_MAX
        || !parse_number (workspace_token, INT_MAX, &target_workspace))
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        resp.type = 1;
        snprintf (resp.message, sizeof (resp.message),
                  "Usage: move <window_id> <workspace_id>");
        memcpy (response->message, &resp, sizeof (resp));
        return;
    }

    window_id = (gf_handle_t)(uintptr_t)raw_handle;
    // Resolve the handle through the tracked window list in the core.
    gf_err_t result = gf_wm_window_move (m, window_id, target_workspace);

    resp.type = (result == GF_SUCCESS) ? 0 : 1;

    switch (result)
    {
    case GF_SUCCESS:
        snprintf (resp.message, sizeof (resp.message), "Window moved to workspace %d",
                  target_workspace);
        break;
    case GF_ERROR_INVALID_PARAMETER:
        snprintf (resp.message, sizeof (resp.message), "Window not found");
        break;
    case GF_ERROR_WORKSPACE_LOCKED:
        snprintf (resp.message, sizeof (resp.message), "Workspace %d is locked",
                  target_workspace);
        break;
    case GF_ERROR_WORKSPACE_FULL:
        snprintf (resp.message, sizeof (resp.message), "Workspace %d is full",
                  target_workspace);
        break;
    case GF_ERROR_WORKSPACE_MAXIMIZED:
        snprintf (resp.message, sizeof (resp.message),
                  "Workspace %d is maximized, only maximized windows allowed",
                  target_workspace);
        break;
    default:
        snprintf (resp.message, sizeof (resp.message), "Unknown error");
        break;
    }

    memcpy (response->message, &resp, sizeof (resp));
}

static void
cmd_lock_workspace (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_wm_t *m = (gf_wm_t *)user_data;

    int workspace_id = -1;
    gf_command_response_t resp = { 0 };

    if (!parse_number (args, INT_MAX, &workspace_id))
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        resp.type = 1;
        snprintf (resp.message, sizeof (resp.message), "Usage: lock <workspace_id>");
        memcpy (response->message, &resp, sizeof (resp));
        return;
    }

    // Just call the window manager API
    gf_err_t result = gf_wm_workspace_lock (m, workspace_id);

    resp.type = (result == GF_SUCCESS) ? 0 : 1;

    switch (result)
    {
    case GF_SUCCESS:
    {
        gf_ws_list_t *workspaces = wm_workspaces (m);
        gf_ws_info_t *ws = gf_workspace_list_find_by_id (workspaces, workspace_id);
        snprintf (resp.message, sizeof (resp.message),
                  "Locked workspace %d (%u windows will remain)", workspace_id,
                  ws ? ws->window_count : 0);
        break;
    }
    case GF_ERROR_INVALID_PARAMETER:
        snprintf (resp.message, sizeof (resp.message), "Invalid workspace ID: %d",
                  workspace_id);
        break;
    case GF_ERROR_ALREADY_LOCKED:
        snprintf (resp.message, sizeof (resp.message), "Workspace %d is already locked",
                  workspace_id);
        break;
    default:
        snprintf (resp.message, sizeof (resp.message), "Unknown error");
        break;
    }

    memcpy (response->message, &resp, sizeof (resp));
}

static void
cmd_unlock_workspace (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_wm_t *m = (gf_wm_t *)user_data;

    int workspace_id = -1;
    gf_command_response_t resp = { 0 };

    if (!parse_number (args, INT_MAX, &workspace_id))
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        resp.type = 1;
        snprintf (resp.message, sizeof (resp.message), "Usage: unlock <workspace_id>");
        memcpy (response->message, &resp, sizeof (resp));
        return;
    }

    // Just call the window manager API
    gf_err_t result = gf_wm_workspace_unlock (m, workspace_id);

    resp.type = (result == GF_SUCCESS) ? 0 : 1;

    switch (result)
    {
    case GF_SUCCESS:
    {
        gf_ws_list_t *workspaces = wm_workspaces (m);
        gf_ws_info_t *ws = gf_workspace_list_find_by_id (workspaces, workspace_id);
        snprintf (resp.message, sizeof (resp.message),
                  "Unlocked workspace %d (%d slots available)", workspace_id,
                  ws ? ws->available_space : 0);
        break;
    }
    case GF_ERROR_INVALID_PARAMETER:
        snprintf (resp.message, sizeof (resp.message), "Invalid workspace ID: %d",
                  workspace_id);
        break;
    case GF_ERROR_ALREADY_UNLOCKED:
        snprintf (resp.message, sizeof (resp.message), "Workspace %d is already unlocked",
                  workspace_id);
        break;
    default:
        snprintf (resp.message, sizeof (resp.message), "Unknown error");
        break;
    }

    memcpy (response->message, &resp, sizeof (resp));
}

static void
cmd_rule_add (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_wm_t *m = (gf_wm_t *)user_data;
    gf_command_response_t resp = { 0 };

    char wm_class[128] = { 0 };
    int workspace_id = -1;

    int monitor_id = -1;
    char workspace_token[32], monitor_token[32];
    char extra[2];
    int fields = args ? sscanf (args, "%127s %31s %31s %1s", wm_class, workspace_token,
                                monitor_token, extra)
                      : 0;
    if (strcspn (args, " \t") >= sizeof (wm_class) || fields < 2 || fields > 3
        || !parse_number (workspace_token, GF_MAX_WORKSPACES, &workspace_id)
        || workspace_id < GF_FIRST_WORKSPACE_ID
        || (fields == 3
            && !parse_number (monitor_token, GF_MAX_MONITORS - 1, &monitor_id)))
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        resp.type = 1;
        snprintf (resp.message, sizeof (resp.message),
                  "Usage: rule add <wm_class> <workspace_id> [monitor_id]");
        memcpy (response->message, &resp, sizeof (resp));
        return;
    }

    // Unset rules overlap every monitor; explicit rules only reserve their
    // selected monitor. Updating a rule never counts its old target twice.
    const gf_window_rule_t *existing = gf_rules_find (m->config, wm_class);
    uint32_t first = monitor_id >= 0 ? (uint32_t)monitor_id : 0;
    uint32_t end = monitor_id >= 0 ? first + 1 : GF_MAX_MONITORS;
    for (uint32_t monitor = first; monitor < end; monitor++)
    {
        uint32_t count = 0;
        for (uint32_t i = 0; i < m->config->window_rules_count; i++)
        {
            const gf_window_rule_t *rule = &m->config->window_rules[i];
            if (rule != existing && rule->workspace_id == workspace_id
                && (!rule->has_monitor_id || rule->monitor_id == monitor))
                count++;
        }
        if (count >= m->config->max_windows_per_workspace)
        {
            response->status = GF_IPC_ERROR_INVALID_COMMAND;
            resp.type = 1;
            snprintf (resp.message, sizeof (resp.message),
                      "Cannot add rule: M%u workspace %d would exceed %u windows",
                      monitor, workspace_id, m->config->max_windows_per_workspace);
            memcpy (response->message, &resp, sizeof (resp));
            return;
        }
    }
    gf_err_t result = gf_rules_add (m->config, wm_class, workspace_id, monitor_id);
    if (result == GF_SUCCESS)
    {
        for (uint32_t i = 0; i < wm_windows (m)->count; i++)
            wm_windows (m)->items[i].rule_move_failures = 0;
        wm_sync_workspaces (m);
    }
    resp.type = (result == GF_SUCCESS) ? 0 : 1;

    if (result == GF_SUCCESS)
    {
        if (monitor_id >= 0)
            snprintf (resp.message, sizeof (resp.message),
                      "Rule added: %s -> M%d workspace %d", wm_class, monitor_id,
                      workspace_id);
        else
            snprintf (resp.message, sizeof (resp.message),
                      "Rule added: %s -> current monitor workspace %d", wm_class,
                      workspace_id);
    }
    else
        snprintf (resp.message, sizeof (resp.message), "Failed to add rule (error %d)",
                  result);

    memcpy (response->message, &resp, sizeof (resp));
}

static void
cmd_rule_remove (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_wm_t *m = (gf_wm_t *)user_data;
    gf_command_response_t resp = { 0 };

    char wm_class[128] = { 0 };

    char extra[2];
    if (!args || sscanf (args, "%127s %1s", wm_class, extra) != 1)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        resp.type = 1;
        snprintf (resp.message, sizeof (resp.message), "Usage: rule remove <wm_class>");
        memcpy (response->message, &resp, sizeof (resp));
        return;
    }

    gf_err_t result = gf_rules_remove (m->config, wm_class);
    resp.type = (result == GF_SUCCESS) ? 0 : 1;

    if (result == GF_SUCCESS)
        snprintf (resp.message, sizeof (resp.message), "Rule removed: %s", wm_class);
    else
        snprintf (resp.message, sizeof (resp.message), "No rule found for: %s", wm_class);

    memcpy (response->message, &resp, sizeof (resp));
}

static void
cmd_rule_list (const char *args, gf_ipc_response_t *response, void *user_data)
{
    (void)args;
    gf_wm_t *m = (gf_wm_t *)user_data;
    gf_command_response_t resp = { 0 };
    resp.type = 0;

    uint32_t count = gf_rules_count (m->config);
    if (count == 0)
    {
        snprintf (resp.message, sizeof (resp.message), "No window rules configured");
        memcpy (response->message, &resp, sizeof (resp));
        return;
    }

    size_t pos = 0;
    pos = append_text (resp.message, sizeof (resp.message), pos, "Window Rules (%u):\n",
                       count);
    pos = append_text (resp.message, sizeof (resp.message), pos, "%-30s %-10s %s\n",
                       "WM Class", "Workspace", "Monitor ID");
    pos = append_text (resp.message, sizeof (resp.message), pos, "%-30s %-10s %s\n",
                       "------------------------------", "---------", "----------");

    for (uint32_t i = 0; i < count && pos < sizeof (resp.message) - 50; i++)
    {
        const gf_window_rule_t *rule = &m->config->window_rules[i];
        char monitor[24] = "Current";
        if (rule->has_monitor_id)
            snprintf (monitor, sizeof (monitor), "M%u", rule->monitor_id);
        pos = append_text (resp.message, sizeof (resp.message), pos, "%-30s %-10d %s\n",
                           rule->wm_class, rule->workspace_id, monitor);
    }

    memcpy (response->message, &resp, sizeof (resp));
}

// Copy the whole argument as the class name — exclusion/rule identities can
// contain spaces (window title fragments, "class|exe"), so first-token parsing
// would truncate them. Trims surrounding whitespace; false if empty.
static bool
copy_class_arg (const char *args, char *out, size_t out_size)
{
    if (!args)
        return false;
    while (*args == ' ' || *args == '\t')
        args++;
    size_t len = strlen (args);
    while (len > 0 && isspace ((unsigned char)args[len - 1]))
        len--;
    if (!len || len >= out_size)
        return false;
    memcpy (out, args, len);
    out[len] = '\0';
    while (len > 0
           && (out[len - 1] == '\n' || out[len - 1] == '\r' || out[len - 1] == ' '
               || out[len - 1] == '\t'))
        out[--len] = '\0';
    return out[0] != '\0';
}

static void
cmd_exclude_add (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_wm_t *m = (gf_wm_t *)user_data;
    gf_command_response_t resp = { 0 };

    char wm_class[GF_RULE_CLASS_MAX] = { 0 };
    if (!copy_class_arg (args, wm_class, sizeof (wm_class)))
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        resp.type = 1;
        snprintf (resp.message, sizeof (resp.message), "Usage: exclude add <wm_class>");
        memcpy (response->message, &resp, sizeof (resp));
        return;
    }

    gf_err_t result = gf_excludes_add (m->config, wm_class);
    resp.type = (result == GF_SUCCESS) ? 0 : 1;

    if (result == GF_SUCCESS)
        snprintf (resp.message, sizeof (resp.message), "Excluded: %s", wm_class);
    else
        snprintf (resp.message, sizeof (resp.message), "Failed to exclude %s (error %d)",
                  wm_class, result);

    memcpy (response->message, &resp, sizeof (resp));
}

static void
cmd_exclude_remove (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_wm_t *m = (gf_wm_t *)user_data;
    gf_command_response_t resp = { 0 };

    char wm_class[GF_RULE_CLASS_MAX] = { 0 };
    if (!copy_class_arg (args, wm_class, sizeof (wm_class)))
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        resp.type = 1;
        snprintf (resp.message, sizeof (resp.message),
                  "Usage: exclude remove <wm_class>");
        memcpy (response->message, &resp, sizeof (resp));
        return;
    }

    gf_err_t result = gf_excludes_remove (m->config, wm_class);
    resp.type = (result == GF_SUCCESS) ? 0 : 1;

    if (result == GF_SUCCESS)
        snprintf (resp.message, sizeof (resp.message), "Exclusion removed: %s", wm_class);
    else
        snprintf (resp.message, sizeof (resp.message), "No exclusion for: %s", wm_class);

    memcpy (response->message, &resp, sizeof (resp));
}

static void
cmd_exclude_list (const char *args, gf_ipc_response_t *response, void *user_data)
{
    (void)args;
    gf_wm_t *m = (gf_wm_t *)user_data;
    gf_command_response_t resp = { 0 };
    resp.type = 0;

    const gf_exclude_list_t *list = &m->config->excluded_apps;
    if (list->count == 0)
    {
        snprintf (resp.message, sizeof (resp.message), "No apps excluded");
        memcpy (response->message, &resp, sizeof (resp));
        return;
    }

    size_t pos = 0;
    pos = append_text (resp.message, sizeof (resp.message), pos, "Excluded apps (%u):\n",
                       list->count);
    for (uint32_t i = 0; i < list->count && pos < sizeof (resp.message) - 130; i++)
        pos = append_text (resp.message, sizeof (resp.message), pos, "%s\n",
                           list->items[i].wm_class);

    memcpy (response->message, &resp, sizeof (resp));
}

static void
cmd_query_apps (const char *args, gf_ipc_response_t *response, void *user_data)
{
    (void)args;
    gf_wm_t *m = (gf_wm_t *)user_data;
    gf_win_list_t *windows = wm_windows (m);
    gf_command_response_t resp = { 0 };
    resp.type = 0;

    // Collect unique class names from tracked windows
    char classes[128][128];
    uint32_t class_count = 0;

    for (uint32_t i = 0; i < windows->count && class_count < 128; i++)
    {
        char name[128] = { 0 };
        gf_wm_window_class (m, windows->items[i].id, name, sizeof (name));

        if (name[0] == '\0')
            continue;

        // Check for duplicate
        bool found = false;
        for (uint32_t j = 0; j < class_count; j++)
        {
            if (strcmp (classes[j], name) == 0)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            strncpy (classes[class_count], name, 127);
            classes[class_count][127] = '\0';
            class_count++;
        }
    }

    if (class_count == 0)
    {
        snprintf (resp.message, sizeof (resp.message), "No applications found");
        memcpy (response->message, &resp, sizeof (resp));
        return;
    }

    size_t pos = 0;
    for (uint32_t i = 0; i < class_count && pos < sizeof (resp.message) - 130; i++)
    {
        pos = append_text (resp.message, sizeof (resp.message), pos, "%s\n", classes[i]);
    }

    memcpy (response->message, &resp, sizeof (resp));
}

void
gf_handle_client_message (const char *message, size_t length, gf_ipc_response_t *response,
                          void *user_data)
{
    char command[64] = { 0 };
    char args[256] = { 0 };

    if (!response)
        return;
    memset (response, 0, sizeof (*response));
    response->status = GF_IPC_ERROR_INVALID_COMMAND;
    if (!message || !length || length >= GF_IPC_MSG_SIZE || !user_data
        || !((gf_wm_t *)user_data)->config || memchr (message, '\0', length))
        return;
    for (size_t i = 0; i < length; i++)
        if (((unsigned char)message[i] < 32 && message[i] != '\t')
            || (unsigned char)message[i] == 127)
            return;
    char request[GF_IPC_MSG_SIZE];
    memcpy (request, message, length);
    request[length] = '\0';
    if (!parse_command (request, command, args, sizeof (args)))
        return;
    response->status = GF_IPC_SUCCESS;

    if (strcmp (command, "query") == 0)
    {
        char subcommand[64] = { 0 };
        char subargs[256] = { 0 };
        if (!parse_command (args, subcommand, subargs, sizeof (subargs)))
        {
            response->status = GF_IPC_ERROR_INVALID_COMMAND;
            return;
        }

        if (strcmp (subcommand, "windows") == 0 || strcmp (subcommand, "W") == 0)
        {
            cmd_query_windows (subargs, response, user_data);
        }
        else if (strcmp (subcommand, "workspaces") == 0 || strcmp (subcommand, "D") == 0)
        {
            cmd_query_workspaces (subargs, response, user_data);
        }
        else if (strcmp (subcommand, "count") == 0 || strcmp (subcommand, "T") == 0)
        {
            cmd_query_count (subargs, response, user_data);
        }
        else if (strcmp (subcommand, "apps") == 0)
        {
            cmd_query_apps (subargs, response, user_data);
        }
        else if (strcmp (subcommand, "monitors") == 0)
        {
            cmd_query_monitors (subargs, response, user_data);
        }
        else
        {
            response->status = GF_IPC_ERROR_INVALID_COMMAND;
            gf_command_response_t resp = { 0 };
            resp.type = 1;
            snprintf (resp.message, sizeof (resp.message), "Unknown query: %s",
                      subcommand);
            memcpy (response->message, &resp, sizeof (resp));
        }
    }
    else if (strcmp (command, "move") == 0)
    {
        cmd_move_window (args, response, user_data);
    }
    else if (strcmp (command, "lock") == 0)
    {
        cmd_lock_workspace (args, response, user_data);
    }
    else if (strcmp (command, "unlock") == 0)
    {
        cmd_unlock_workspace (args, response, user_data);
    }
    else if (strcmp (command, "toggle-borders") == 0)
    {
        gf_wm_t *m = (gf_wm_t *)user_data;
        if (!m || !m->config)
        {
            response->status = GF_IPC_ERROR_INVALID_COMMAND;
            gf_command_response_t resp = { 0 };
            resp.type = 1;
            snprintf (resp.message, sizeof (resp.message), "WM not initialized");
            memcpy (response->message, &resp, sizeof (resp));
        }
        else
        {
            m->config->enable_borders = !m->config->enable_borders;
            const char *path = gf_config_get_path ();
            if (path)
                gf_config_save (path, m->config);

            response->status = GF_IPC_SUCCESS;
            gf_command_response_t resp = { 0 };
            resp.type = 0;
            snprintf (resp.message, sizeof (resp.message), "Borders %s",
                      m->config->enable_borders ? "enabled" : "disabled");
            memcpy (response->message, &resp, sizeof (resp));
        }
    }
    else if (strcmp (command, "rule") == 0)
    {
        char subcommand[64] = { 0 };
        char subargs[256] = { 0 };
        if (!parse_command (args, subcommand, subargs, sizeof (subargs)))
        {
            response->status = GF_IPC_ERROR_INVALID_COMMAND;
            return;
        }

        if (strcmp (subcommand, "add") == 0)
        {
            cmd_rule_add (subargs, response, user_data);
        }
        else if (strcmp (subcommand, "remove") == 0)
        {
            cmd_rule_remove (subargs, response, user_data);
        }
        else if (strcmp (subcommand, "list") == 0)
        {
            cmd_rule_list (subargs, response, user_data);
        }
        else
        {
            response->status = GF_IPC_ERROR_INVALID_COMMAND;
            gf_command_response_t resp = { 0 };
            resp.type = 1;
            snprintf (resp.message, sizeof (resp.message), "Unknown rule command: %s",
                      subcommand);
            memcpy (response->message, &resp, sizeof (resp));
        }
    }
    else if (strcmp (command, "exclude") == 0)
    {
        char subcommand[64] = { 0 };
        char subargs[256] = { 0 };
        if (!parse_command (args, subcommand, subargs, sizeof (subargs)))
        {
            response->status = GF_IPC_ERROR_INVALID_COMMAND;
            return;
        }

        if (strcmp (subcommand, "add") == 0)
        {
            cmd_exclude_add (subargs, response, user_data);
        }
        else if (strcmp (subcommand, "remove") == 0)
        {
            cmd_exclude_remove (subargs, response, user_data);
        }
        else if (strcmp (subcommand, "list") == 0)
        {
            cmd_exclude_list (subargs, response, user_data);
        }
        else
        {
            response->status = GF_IPC_ERROR_INVALID_COMMAND;
            gf_command_response_t resp = { 0 };
            resp.type = 1;
            snprintf (resp.message, sizeof (resp.message), "Unknown exclude command: %s",
                      subcommand);
            memcpy (response->message, &resp, sizeof (resp));
        }
    }
    else
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        gf_command_response_t resp = { 0 };
        resp.type = 1;
        snprintf (resp.message, sizeof (resp.message), "Unknown command: %s", command);
        memcpy (response->message, &resp, sizeof (resp));
    }
}
