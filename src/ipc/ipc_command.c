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

static void
parse_command (const char *input, char *command, char *args, size_t args_size)
{
    while (isspace (*input))
        input++;

    size_t i = 0;
    while (*input && !isspace (*input) && i < 63)
    {
        command[i++] = *input++;
    }
    command[i] = '\0';

    while (isspace (*input))
        input++;

    strncpy (args, input, args_size - 1);
    args[args_size - 1] = '\0';
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
    memcpy (response->message + offset, windows->items,
            send_count * sizeof (gf_win_info_t));
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
    memcpy (response->message + offset, workspaces->items,
            send_count * sizeof (gf_ws_info_t));
}

static void
cmd_query_count (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_wm_t *m = (gf_wm_t *)user_data;
    gf_win_list_t *windows = wm_windows (m);

    gf_command_response_t resp;
    resp.type = 0;

    if (args && *args)
    {
        int workspace_id = atoi (args);
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

    gf_command_response_t resp;

    if (!args || sscanf (args, "%p %d", (void **)&window_id, &target_workspace) != 2)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        resp.type = 1;
        snprintf (resp.message, sizeof (resp.message),
                  "Usage: move <window_id> <workspace_id>");
        memcpy (response->message, &resp, sizeof (resp));
        return;
    }

    // Just call the window manager API
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
    gf_command_response_t resp;

    if (!args || sscanf (args, "%d", &workspace_id) != 1)
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
    gf_command_response_t resp;

    if (!args || sscanf (args, "%d", &workspace_id) != 1)
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

static bool
parse_rule_number (const char *token, int maximum, int *value)
{
    char *end;
    errno = 0;
    long parsed = strtol (token, &end, 10);
    if (errno || end == token || *end || parsed < 0 || parsed > maximum)
        return false;
    *value = (int)parsed;
    return true;
}

static void
cmd_rule_add (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_wm_t *m = (gf_wm_t *)user_data;
    gf_command_response_t resp;

    char wm_class[128] = { 0 };
    int workspace_id = -1;

    int monitor_id = -1;
    char workspace_token[32], monitor_token[32];
    char extra[2];
    int fields = args ? sscanf (args, "%127s %31s %31s %1s", wm_class, workspace_token,
                                monitor_token, extra)
                      : 0;
    if (fields < 2 || fields > 3
        || !parse_rule_number (workspace_token, GF_MAX_WORKSPACES, &workspace_id)
        || workspace_id < GF_FIRST_WORKSPACE_ID
        || (fields == 3
            && !parse_rule_number (monitor_token, GF_MAX_MONITORS - 1, &monitor_id)))
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
    gf_command_response_t resp;

    char wm_class[128] = { 0 };

    if (!args || sscanf (args, "%127s", wm_class) != 1)
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
    gf_command_response_t resp;
    resp.type = 0;

    uint32_t count = gf_rules_count (m->config);
    if (count == 0)
    {
        snprintf (resp.message, sizeof (resp.message), "No window rules configured");
        memcpy (response->message, &resp, sizeof (resp));
        return;
    }

    size_t pos = 0;
    pos += snprintf (resp.message + pos, sizeof (resp.message) - pos,
                     "Window Rules (%u):\n", count);
    pos += snprintf (resp.message + pos, sizeof (resp.message) - pos, "%-30s %-10s %s\n",
                     "WM Class", "Workspace", "Monitor ID");
    pos += snprintf (resp.message + pos, sizeof (resp.message) - pos, "%-30s %-10s %s\n",
                     "------------------------------", "---------", "----------");

    for (uint32_t i = 0; i < count && pos < sizeof (resp.message) - 50; i++)
    {
        const gf_window_rule_t *rule = &m->config->window_rules[i];
        char monitor[24] = "Current";
        if (rule->has_monitor_id)
            snprintf (monitor, sizeof (monitor), "M%u", rule->monitor_id);
        pos += snprintf (resp.message + pos, sizeof (resp.message) - pos,
                         "%-30s %-10d %s\n", rule->wm_class, rule->workspace_id, monitor);
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
    gf_safe_strcpy (out, out_size, args);
    size_t len = strlen (out);
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
    gf_command_response_t resp;

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
    gf_command_response_t resp;

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
    gf_command_response_t resp;
    resp.type = 0;

    const gf_exclude_list_t *list = &m->config->excluded_apps;
    if (list->count == 0)
    {
        snprintf (resp.message, sizeof (resp.message), "No apps excluded");
        memcpy (response->message, &resp, sizeof (resp));
        return;
    }

    size_t pos = 0;
    pos += snprintf (resp.message + pos, sizeof (resp.message) - pos,
                     "Excluded apps (%u):\n", list->count);
    for (uint32_t i = 0; i < list->count && pos < sizeof (resp.message) - 130; i++)
        pos += snprintf (resp.message + pos, sizeof (resp.message) - pos, "%s\n",
                         list->items[i].wm_class);

    memcpy (response->message, &resp, sizeof (resp));
}

static void
cmd_query_apps (const char *args, gf_ipc_response_t *response, void *user_data)
{
    (void)args;
    gf_wm_t *m = (gf_wm_t *)user_data;
    gf_win_list_t *windows = wm_windows (m);
    gf_command_response_t resp;
    resp.type = 0;

    // Collect unique class names from tracked windows
    char classes[128][128];
    uint32_t class_count = 0;

    for (uint32_t i = 0; i < windows->count && class_count < 128; i++)
    {
        char name[128];
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
        pos += snprintf (resp.message + pos, sizeof (resp.message) - pos, "%s\n",
                         classes[i]);
    }

    memcpy (response->message, &resp, sizeof (resp));
}

void
gf_handle_client_message (const char *message, gf_ipc_response_t *response,
                          void *user_data)
{
    char command[64] = { 0 };
    char args[256] = { 0 };

    parse_command (message, command, args, sizeof (args));

    if (strcmp (command, "query") == 0)
    {
        char subcommand[64] = { 0 };
        char subargs[256] = { 0 };
        parse_command (args, subcommand, subargs, sizeof (subargs));

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
        else
        {
            response->status = GF_IPC_ERROR_INVALID_COMMAND;
            gf_command_response_t resp;
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
            gf_command_response_t resp;
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
            gf_command_response_t resp;
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
        parse_command (args, subcommand, subargs, sizeof (subargs));

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
            gf_command_response_t resp;
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
        parse_command (args, subcommand, subargs, sizeof (subargs));

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
            gf_command_response_t resp;
            resp.type = 1;
            snprintf (resp.message, sizeof (resp.message), "Unknown exclude command: %s",
                      subcommand);
            memcpy (response->message, &resp, sizeof (resp));
        }
    }
    else
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        gf_command_response_t resp;
        resp.type = 1;
        snprintf (resp.message, sizeof (resp.message), "Unknown command: %s", command);
        memcpy (response->message, &resp, sizeof (resp));
    }
}

gf_ws_list_t *
gf_parse_workspace_list (const char *buffer)
{
    if (!buffer)
        return NULL;

    gf_ws_list_t *list = gf_malloc (sizeof (gf_ws_list_t));
    if (!list)
        return NULL;

    size_t offset = 0;
    memcpy (&list->count, buffer + offset, sizeof (uint32_t));
    offset += sizeof (uint32_t);
    memcpy (&list->capacity, buffer + offset, sizeof (uint32_t));
    offset += sizeof (uint32_t);
    memcpy (&list->active_workspace, buffer + offset, sizeof (list->active_workspace));
    offset += sizeof (list->active_workspace);

    // Never trust the peer-supplied count: clamp it to what the fixed reply
    // buffer can actually hold before allocating/copying.
    uint32_t max_items = ipc_max_records (offset, sizeof (gf_ws_info_t));
    if (list->count > max_items)
        list->count = max_items;
    if (list->count == 0)
    {
        list->items = NULL;
        return list;
    }

    list->items = gf_malloc (list->count * sizeof (gf_ws_info_t));
    if (!list->items)
    {
        gf_free (list);
        return NULL;
    }

    memcpy (list->items, buffer + offset, list->count * sizeof (gf_ws_info_t));

    return list;
}

gf_win_list_t *
gf_parse_window_list (const char *buffer)
{
    if (!buffer)
        return NULL;

    gf_win_list_t *list = gf_malloc (sizeof (gf_win_list_t));
    if (!list)
        return NULL;

    size_t offset = 0;
    memcpy (&list->count, buffer + offset, sizeof (uint32_t));
    offset += sizeof (uint32_t);
    memcpy (&list->capacity, buffer + offset, sizeof (uint32_t));
    offset += sizeof (uint32_t);

    // Never trust the peer-supplied count: clamp it to what the fixed reply
    // buffer can actually hold before allocating/copying.
    uint32_t max_items = ipc_max_records (offset, sizeof (gf_win_info_t));
    if (list->count > max_items)
        list->count = max_items;
    if (list->count == 0)
    {
        list->items = NULL;
        return list;
    }

    list->items = gf_malloc (list->count * sizeof (gf_win_info_t));
    if (!list->items)
    {
        gf_free (list);
        return NULL;
    }

    memcpy (list->items, buffer + offset, list->count * sizeof (gf_win_info_t));

    return list;
}
