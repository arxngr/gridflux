#include "ipc_command.h"
#include "../config/rules.h"
#include "../core/internal.h"
#include "../core/wm.h"
#include "../utils/memory.h"
#include "ipc.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
gf_parse_command (const char *input, char *command, char *args, size_t args_size)
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
gf_cmd_query_windows (const char *args, gf_ipc_response_t *response, void *user_data)
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

    size_t offset = 0;
    memcpy (response->message + offset, &windows->count, sizeof (uint32_t));
    offset += sizeof (uint32_t);
    memcpy (response->message + offset, &windows->capacity, sizeof (uint32_t));
    offset += sizeof (uint32_t);
    memcpy (response->message + offset, windows->items,
            windows->count * sizeof (gf_win_info_t));
}

static void
gf_cmd_query_workspaces (const char *args, gf_ipc_response_t *response, void *user_data)
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

    size_t offset = 0;
    memcpy (response->message + offset, &workspaces->count, sizeof (uint32_t));
    offset += sizeof (uint32_t);
    memcpy (response->message + offset, &workspaces->capacity, sizeof (uint32_t));
    offset += sizeof (uint32_t);
    memcpy (response->message + offset, &workspaces->active_workspace,
            sizeof (gf_ws_id_t));
    offset += sizeof (gf_ws_id_t);
    memcpy (response->message + offset, workspaces->items,
            workspaces->count * sizeof (gf_ws_info_t));
}

static void
gf_cmd_query_count (const char *args, gf_ipc_response_t *response, void *user_data)
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
gf_cmd_move_window (const char *args, gf_ipc_response_t *response, void *user_data)
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
        snprintf (resp.message, sizeof (resp.message), "Moved window %p to workspace %d",
                  (void *)window_id, target_workspace);
        break;
    case GF_ERROR_INVALID_PARAMETER:
        snprintf (resp.message, sizeof (resp.message), "Window %p not found",
                  (void *)window_id);
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
gf_cmd_lock_workspace (const char *args, gf_ipc_response_t *response, void *user_data)
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
        gf_ws_info_t *ws = &workspaces->items[workspace_id];
        snprintf (resp.message, sizeof (resp.message),
                  "Locked workspace %d (%u windows will remain)", workspace_id,
                  ws->window_count);
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
gf_cmd_unlock_workspace (const char *args, gf_ipc_response_t *response, void *user_data)
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
        gf_ws_info_t *ws = &workspaces->items[workspace_id];
        snprintf (resp.message, sizeof (resp.message),
                  "Unlocked workspace %d (%d slots available)", workspace_id,
                  ws->available_space);
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
gf_cmd_rule_add (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_wm_t *m = (gf_wm_t *)user_data;
    gf_command_response_t resp;

    char wm_class[128] = { 0 };
    int workspace_id = -1;

    if (!args || sscanf (args, "%127s %d", wm_class, &workspace_id) != 2)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        resp.type = 1;
        snprintf (resp.message, sizeof (resp.message),
                  "Usage: rule add <wm_class> <workspace_id>");
        memcpy (response->message, &resp, sizeof (resp));
        return;
    }

    gf_ws_info_t *target_ws
        = gf_workspace_list_find_by_id (wm_workspaces (m), workspace_id);
    if (target_ws && target_ws->has_maximized_state)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        resp.type = 1;
        snprintf (resp.message, sizeof (resp.message),
                  "Cannot add rule to a maximized workspace");
        memcpy (response->message, &resp, sizeof (resp));
        return;
    }

    // Check if adding this rule would exceed max_windows_per_workspace
    uint32_t rule_count_for_ws = 0;
    for (uint32_t i = 0; i < m->config->window_rules_count; i++)
    {
        if (m->config->window_rules[i].workspace_id == workspace_id)
            rule_count_for_ws++;
    }

    if (rule_count_for_ws >= m->config->max_windows_per_workspace)
    {
        const gf_window_rule_t *existing_rule = gf_rules_find (m->config, wm_class);
        if (!existing_rule || existing_rule->workspace_id != workspace_id)
        {
            response->status = GF_IPC_ERROR_INVALID_COMMAND;
            resp.type = 1;
            snprintf (resp.message, sizeof (resp.message),
                      "Cannot add rule: Workspace %d would exceed max windows limit (%d)",
                      workspace_id, m->config->max_windows_per_workspace);
            memcpy (response->message, &resp, sizeof (resp));
            return;
        }
    }

    gf_err_t result = gf_rules_add (m->config, wm_class, workspace_id);
    resp.type = (result == GF_SUCCESS) ? 0 : 1;

    if (result == GF_SUCCESS)
        snprintf (resp.message, sizeof (resp.message), "Rule added: %s -> workspace %d",
                  wm_class, workspace_id);
    else
        snprintf (resp.message, sizeof (resp.message), "Failed to add rule (error %d)",
                  result);

    memcpy (response->message, &resp, sizeof (resp));
}

static void
gf_cmd_rule_remove (const char *args, gf_ipc_response_t *response, void *user_data)
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
gf_cmd_rule_list (const char *args, gf_ipc_response_t *response, void *user_data)
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
    pos += snprintf (resp.message + pos, sizeof (resp.message) - pos, "%-30s %s\n",
                     "WM Class", "Workspace");
    pos += snprintf (resp.message + pos, sizeof (resp.message) - pos, "%-30s %s\n",
                     "------------------------------", "---------");

    for (uint32_t i = 0; i < count && pos < sizeof (resp.message) - 50; i++)
    {
        pos += snprintf (resp.message + pos, sizeof (resp.message) - pos, "%-30s %d\n",
                         m->config->window_rules[i].wm_class,
                         m->config->window_rules[i].workspace_id);
    }

    memcpy (response->message, &resp, sizeof (resp));
}

static void
gf_cmd_query_apps (const char *args, gf_ipc_response_t *response, void *user_data)
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

    gf_parse_command (message, command, args, sizeof (args));

    if (strcmp (command, "query") == 0)
    {
        char subcommand[64] = { 0 };
        char subargs[256] = { 0 };
        gf_parse_command (args, subcommand, subargs, sizeof (subargs));

        if (strcmp (subcommand, "windows") == 0 || strcmp (subcommand, "W") == 0)
        {
            gf_cmd_query_windows (subargs, response, user_data);
        }
        else if (strcmp (subcommand, "workspaces") == 0 || strcmp (subcommand, "D") == 0)
        {
            gf_cmd_query_workspaces (subargs, response, user_data);
        }
        else if (strcmp (subcommand, "count") == 0 || strcmp (subcommand, "T") == 0)
        {
            gf_cmd_query_count (subargs, response, user_data);
        }
        else if (strcmp (subcommand, "apps") == 0)
        {
            gf_cmd_query_apps (subargs, response, user_data);
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
        gf_cmd_move_window (args, response, user_data);
    }
    else if (strcmp (command, "lock") == 0)
    {
        gf_cmd_lock_workspace (args, response, user_data);
    }
    else if (strcmp (command, "unlock") == 0)
    {
        gf_cmd_unlock_workspace (args, response, user_data);
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
        gf_parse_command (args, subcommand, subargs, sizeof (subargs));

        if (strcmp (subcommand, "add") == 0)
        {
            gf_cmd_rule_add (subargs, response, user_data);
        }
        else if (strcmp (subcommand, "remove") == 0)
        {
            gf_cmd_rule_remove (subargs, response, user_data);
        }
        else if (strcmp (subcommand, "list") == 0)
        {
            gf_cmd_rule_list (subargs, response, user_data);
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
    memcpy (&list->active_workspace, buffer + offset, sizeof (gf_ws_id_t));
    offset += sizeof (gf_ws_id_t);

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

    list->items = gf_malloc (list->count * sizeof (gf_win_info_t));
    if (!list->items)
    {
        gf_free (list);
        return NULL;
    }

    memcpy (list->items, buffer + offset, list->count * sizeof (gf_win_info_t));

    return list;
}
