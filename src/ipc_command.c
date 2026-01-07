#include "internal.h"
#include "ipc.h"
#include "memory.h"
#include "window_manager.h"
#include <ctype.h>
#include <json-c/json.h>
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
    gf_window_manager_t *m = (gf_window_manager_t *)user_data;
    gf_window_list_t *windows = wm_windows (m);
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    int workspace_id = -1;
    if (args && *args)
        workspace_id = atoi (args);

    json_object *root = json_object_new_object ();
    json_object_object_add (root, "type", json_object_new_string ("window_list"));
    json_object_object_add (root, "workspace_id", json_object_new_int (workspace_id));

    json_object *windows_array = json_object_new_array ();

    if (workspace_id >= 0)
    {
        gf_window_info_t **ws_windows = NULL;
        uint32_t count = 0;

        if (gf_window_list_get_by_workspace (windows, workspace_id, ws_windows, &count)
            == GF_SUCCESS)
        {
            for (uint32_t i = 0; i < count; i++)
            {
                gf_window_info_t *w = ws_windows[i];

                if (!w->name[0])
                {
                    platform->window_name_info (display, w->native_handle, w->name,
                                                sizeof (w->name) - 1);
                }

                json_object *win_obj = json_object_new_object ();
                json_object_object_add (win_obj, "id", json_object_new_int64 (w->id));
                json_object_object_add (
                    win_obj, "name",
                    json_object_new_string (w->name[0] ? w->name : "Unknown"));
                json_object_object_add (win_obj, "workspace_id",
                                        json_object_new_int (w->workspace_id));
                json_object_object_add (win_obj, "is_maximized",
                                        json_object_new_boolean (w->is_maximized));
                json_object_object_add (win_obj, "is_minimized",
                                        json_object_new_boolean (w->is_minimized));

                json_object_array_add (windows_array, win_obj);
            }

            gf_free (ws_windows);
        }
    }
    else
    {
        for (uint32_t i = 0; i < windows->count; i++)
        {
            gf_window_info_t *w = &windows->items[i];

            if (!w->name[0])
            {
                platform->window_name_info (display, w->native_handle, w->name,
                                            sizeof (w->name) - 1);
            }

            json_object *win_obj = json_object_new_object ();
            json_object_object_add (win_obj, "id", json_object_new_int64 (w->id));
            json_object_object_add (
                win_obj, "name",
                json_object_new_string (w->name[0] ? w->name : "Unknown"));
            json_object_object_add (win_obj, "workspace_id",
                                    json_object_new_int (w->workspace_id));
            json_object_object_add (win_obj, "is_maximized",
                                    json_object_new_boolean (w->is_maximized));
            json_object_object_add (win_obj, "is_minimized",
                                    json_object_new_boolean (w->is_minimized));

            json_object_array_add (windows_array, win_obj);
        }
    }

    json_object_object_add (root, "windows", windows_array);

    const char *json_str = json_object_to_json_string_ext (root, JSON_C_TO_STRING_PLAIN);

    snprintf (response->message, sizeof (response->message), "%s", json_str);

    json_object_put (root);
}

static void
gf_cmd_query_workspaces (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_window_manager_t *m = (gf_window_manager_t *)user_data;
    gf_workspace_list_t *workspaces = wm_workspaces (m);
    gf_window_list_t *windows = wm_windows (m);
    gf_platform_interface_t *platform = wm_platform (m);
    gf_display_t display = *wm_display (m);

    if (!platform)
    {
        snprintf (response->message, sizeof (response->message),
                  "{\"type\":\"error\",\"message\":\"Platform not initialized\"}");
        return;
    }

    json_object *root = json_object_new_object ();
    json_object_object_add (root, "type", json_object_new_string ("workspace_list"));
    json_object_object_add (root, "total", json_object_new_int (workspaces->count));

    json_object *workspaces_array = json_object_new_array ();

    uint32_t max_ws = workspaces->count;
    if (max_ws > workspaces->capacity)
        max_ws = workspaces->capacity;

    for (uint32_t i = 0; i < max_ws; i++)
    {
        gf_workspace_info_t *ws = &workspaces->items[i];

        json_object *ws_obj = json_object_new_object ();
        json_object_object_add (ws_obj, "id", json_object_new_int (ws->id));
        json_object_object_add (ws_obj, "window_count",
                                json_object_new_int (ws->window_count));
        json_object_object_add (ws_obj, "max_windows",
                                json_object_new_int (ws->max_windows));
        json_object_object_add (ws_obj, "available_space",
                                json_object_new_int (ws->available_space));
        json_object_object_add (ws_obj, "is_locked",
                                json_object_new_boolean (ws->is_locked));

        json_object *windows_array = json_object_new_array ();

        gf_window_info_t **ws_windows = NULL;
        uint32_t ws_win_count = 0;

        if (gf_window_list_get_by_workspace (windows, ws->id, ws_windows, &ws_win_count)
            == GF_SUCCESS)
        {
            for (uint32_t j = 0; j < ws_win_count; j++)
            {
                gf_window_info_t *win = ws_windows[j];

                if (!win->name[0])
                {
                    platform->window_name_info (display, win->native_handle, win->name,
                                                sizeof (win->name) - 1);
                }

                const char *name = win->name[0] ? win->name : "Unknown";
                json_object_array_add (windows_array, json_object_new_string (name));
            }

            gf_free (ws_windows); /* pointer array only */
        }

        json_object_object_add (ws_obj, "windows", windows_array);
        json_object_array_add (workspaces_array, ws_obj);
    }

    json_object_object_add (root, "workspaces", workspaces_array);

    const char *json_str = json_object_to_json_string_ext (root, JSON_C_TO_STRING_PLAIN);

    snprintf (response->message, sizeof (response->message), "%s", json_str);

    json_object_put (root);
}

static void
gf_cmd_query_count (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_window_manager_t *m = (gf_window_manager_t *)user_data;
    gf_window_list_t *windows = wm_windows (m);

    json_object *root = json_object_new_object ();
    json_object_object_add (root, "type", json_object_new_string ("count"));

    if (args && *args)
    {
        int workspace_id = atoi (args);
        uint32_t count = gf_window_list_count_by_workspace (windows, workspace_id);
        json_object_object_add (root, "workspace_id", json_object_new_int (workspace_id));
        json_object_object_add (root, "count", json_object_new_int (count));
    }
    else
    {
        json_object_object_add (root, "count", json_object_new_int (windows->count));
    }

    const char *json_str = json_object_to_json_string_ext (root, JSON_C_TO_STRING_PLAIN);
    snprintf (response->message, sizeof (response->message), "%s", json_str);

    json_object_put (root);
}

static void
gf_cmd_move_window (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_window_manager_t *m = (gf_window_manager_t *)user_data;
    gf_window_list_t *windows = wm_windows (m);
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    uint32_t window_id = 0;
    int target_workspace = -1;

    json_object *root = json_object_new_object ();

    if (!args || sscanf (args, "%u %d", &window_id, &target_workspace) != 2)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        json_object_object_add (root, "type", json_object_new_string ("error"));
        json_object_object_add (
            root, "message",
            json_object_new_string ("Usage: move <window_id> <workspace_id>"));
        goto finish_move;
    }

    if (target_workspace < 0 || target_workspace >= (int)m->config->max_workspaces)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        json_object_object_add (root, "type", json_object_new_string ("error"));

        char error_msg[128];
        snprintf (error_msg, sizeof (error_msg),
                  "Invalid workspace ID: %d (must be 0-%u)", target_workspace,
                  m->config->max_workspaces - 1);
        json_object_object_add (root, "message", json_object_new_string (error_msg));
        goto finish_move;
    }

    gf_window_info_t *win = NULL;
    for (uint32_t i = 0; i < windows->count; i++)
    {
        if (windows->items[i].id == window_id)
        {
            win = &windows->items[i];
            break;
        }
    }

    if (!win)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        json_object_object_add (root, "type", json_object_new_string ("error"));

        char error_msg[128];
        snprintf (error_msg, sizeof (error_msg), "Window %u not found", window_id);
        json_object_object_add (root, "message", json_object_new_string (error_msg));
        goto finish_move;
    }

    gf_workspace_id_t old_workspace = win->workspace_id;

    if (old_workspace == target_workspace)
    {
        json_object_object_add (root, "type", json_object_new_string ("success"));

        char msg[128];
        snprintf (msg, sizeof (msg), "Window %u is already in workspace %d", window_id,
                  target_workspace);
        json_object_object_add (root, "message", json_object_new_string (msg));
        goto finish_move;
    }

    gf_workspace_list_ensure (workspaces, target_workspace,
                              m->config->max_windows_per_workspace);

    gf_workspace_info_t *target_ws = &workspaces->items[target_workspace];

    if (target_ws->is_locked)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        json_object_object_add (root, "type", json_object_new_string ("error"));

        char error_msg[128];
        snprintf (error_msg, sizeof (error_msg), "Workspace %d is locked",
                  target_workspace);
        json_object_object_add (root, "message", json_object_new_string (error_msg));
        goto finish_move;
    }

    if (target_ws->window_count >= m->config->max_windows_per_workspace)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        json_object_object_add (root, "type", json_object_new_string ("error"));

        char error_msg[128];
        snprintf (error_msg, sizeof (error_msg), "Workspace %d is full (%u/%u windows)",
                  target_workspace, target_ws->window_count,
                  m->config->max_windows_per_workspace);
        json_object_object_add (root, "message", json_object_new_string (error_msg));
        goto finish_move;
    }

    win->workspace_id = target_workspace;

    if (old_workspace >= 0 && old_workspace < (int)workspaces->count)
    {
        workspaces->items[old_workspace].window_count--;
        workspaces->items[old_workspace].available_space++;
    }

    target_ws->window_count++;
    target_ws->available_space--;

    json_object_object_add (root, "type", json_object_new_string ("success"));

    char msg[256];
    snprintf (msg, sizeof (msg), "Moved window %u from workspace %d to workspace %d",
              window_id, old_workspace, target_workspace);
    json_object_object_add (root, "message", json_object_new_string (msg));

finish_move:
{
    const char *json_str = json_object_to_json_string_ext (root, JSON_C_TO_STRING_PLAIN);
    snprintf (response->message, sizeof (response->message), "%s", json_str);
    json_object_put (root);
}
}

static void
gf_cmd_lock_workspace (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_window_manager_t *m = (gf_window_manager_t *)user_data;
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    int workspace_id = -1;

    json_object *root = json_object_new_object ();

    if (!args || sscanf (args, "%d", &workspace_id) != 1)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        json_object_object_add (root, "type", json_object_new_string ("error"));
        json_object_object_add (root, "message",
                                json_object_new_string ("Usage: lock <workspace_id>"));
        goto finish_lock;
    }

    if (workspace_id < 0 || workspace_id >= (int)m->config->max_workspaces)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        json_object_object_add (root, "type", json_object_new_string ("error"));

        char error_msg[128];
        snprintf (error_msg, sizeof (error_msg),
                  "Invalid workspace ID: %d (must be 0-%u)", workspace_id,
                  m->config->max_workspaces - 1);
        json_object_object_add (root, "message", json_object_new_string (error_msg));
        goto finish_lock;
    }

    gf_workspace_list_ensure (workspaces, workspace_id,
                              m->config->max_windows_per_workspace);

    gf_workspace_info_t *ws = &workspaces->items[workspace_id];

    if (ws->is_locked)
    {
        json_object_object_add (root, "type", json_object_new_string ("success"));

        char msg[128];
        snprintf (msg, sizeof (msg), "Workspace %d is already locked", workspace_id);
        json_object_object_add (root, "message", json_object_new_string (msg));
        goto finish_lock;
    }

    ws->is_locked = true;
    ws->available_space = 0;

    if (gf_config_add_locked_workspace (m->config, workspace_id) == GF_SUCCESS)
    {
        json_object_object_add (root, "type", json_object_new_string ("success"));

        char msg[128];
        snprintf (msg, sizeof (msg), "Locked workspace %d (%u windows will remain)",
                  workspace_id, ws->window_count);
        json_object_object_add (root, "message", json_object_new_string (msg));
    }
    else
    {
        json_object_object_add (root, "type", json_object_new_string ("success"));
        json_object_object_add (
            root, "message",
            json_object_new_string ("Locked workspace but failed to save to config"));
    }

finish_lock:
{
    const char *json_str = json_object_to_json_string_ext (root, JSON_C_TO_STRING_PLAIN);
    snprintf (response->message, sizeof (response->message), "%s", json_str);
    json_object_put (root);
}
}

static void
gf_cmd_unlock_workspace (const char *args, gf_ipc_response_t *response, void *user_data)
{
    gf_window_manager_t *m = (gf_window_manager_t *)user_data;
    gf_workspace_list_t *workspaces = wm_workspaces (m);

    int workspace_id = -1;

    json_object *root = json_object_new_object ();

    if (!args || sscanf (args, "%d", &workspace_id) != 1)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        json_object_object_add (root, "type", json_object_new_string ("error"));
        json_object_object_add (root, "message",
                                json_object_new_string ("Usage: unlock <workspace_id>"));
        goto finish_unlock;
    }

    if (workspace_id < 0 || workspace_id >= (int)m->config->max_workspaces)
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        json_object_object_add (root, "type", json_object_new_string ("error"));

        char error_msg[128];
        snprintf (error_msg, sizeof (error_msg),
                  "Invalid workspace ID: %d (must be 0-%u)", workspace_id,
                  m->config->max_workspaces - 1);
        json_object_object_add (root, "message", json_object_new_string (error_msg));
        goto finish_unlock;
    }

    gf_workspace_list_ensure (workspaces, workspace_id,
                              m->config->max_windows_per_workspace);

    gf_workspace_info_t *ws = &workspaces->items[workspace_id];

    if (!ws->is_locked)
    {
        json_object_object_add (root, "type", json_object_new_string ("success"));

        char msg[128];
        snprintf (msg, sizeof (msg), "Workspace %d is already unlocked", workspace_id);
        json_object_object_add (root, "message", json_object_new_string (msg));
        goto finish_unlock;
    }

    ws->is_locked = false;
    ws->available_space = m->config->max_windows_per_workspace - ws->window_count;

    if (gf_config_remove_locked_workspace (m->config, workspace_id) == GF_SUCCESS)
    {
        json_object_object_add (root, "type", json_object_new_string ("success"));

        char msg[128];
        snprintf (msg, sizeof (msg), "Unlocked workspace %d (%d slots available)",
                  workspace_id, ws->available_space);
        json_object_object_add (root, "message", json_object_new_string (msg));
    }
    else
    {
        json_object_object_add (root, "type", json_object_new_string ("success"));
        json_object_object_add (
            root, "message",
            json_object_new_string ("Unlocked workspace but failed to save to config"));
    }

finish_unlock:
{
    const char *json_str = json_object_to_json_string_ext (root, JSON_C_TO_STRING_PLAIN);
    snprintf (response->message, sizeof (response->message), "%s", json_str);
    json_object_put (root);
}
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
        else
        {
            response->status = GF_IPC_ERROR_INVALID_COMMAND;
            json_object *root = json_object_new_object ();
            json_object_object_add (root, "type", json_object_new_string ("error"));

            char error_msg[128];
            snprintf (error_msg, sizeof (error_msg), "Unknown query: %s", subcommand);
            json_object_object_add (root, "message", json_object_new_string (error_msg));

            const char *json_str
                = json_object_to_json_string_ext (root, JSON_C_TO_STRING_PLAIN);
            snprintf (response->message, sizeof (response->message), "%s", json_str);
            json_object_put (root);
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
    else
    {
        response->status = GF_IPC_ERROR_INVALID_COMMAND;
        json_object *root = json_object_new_object ();
        json_object_object_add (root, "type", json_object_new_string ("error"));

        char error_msg[128];
        snprintf (error_msg, sizeof (error_msg), "Unknown command: %s", command);
        json_object_object_add (root, "message", json_object_new_string (error_msg));

        const char *json_str
            = json_object_to_json_string_ext (root, JSON_C_TO_STRING_PLAIN);
        snprintf (response->message, sizeof (response->message), "%s", json_str);
        json_object_put (root);
    }
}

gf_workspace_list_t *
gf_parse_workspace_list (const char *json_str)
{
    if (!json_str)
        return NULL;

    json_object *root = json_tokener_parse (json_str);
    if (!root)
        return NULL;

    gf_workspace_list_t *list = gf_malloc (sizeof (gf_workspace_list_t));
    if (!list)
    {
        json_object_put (root);
        return NULL;
    }

    json_object *workspaces_array;
    if (!json_object_object_get_ex (root, "workspaces", &workspaces_array))
    {
        gf_free (list);
        json_object_put (root);
        return NULL;
    }

    size_t count = json_object_array_length (workspaces_array);
    list->items = gf_malloc (sizeof (gf_workspace_info_t) * count);
    if (!list->items)
    {
        gf_free (list);
        json_object_put (root);
        return NULL;
    }

    list->count = count;
    list->capacity = count;
    list->active_workspace = 0; // default

    for (size_t i = 0; i < count; i++)
    {
        json_object *ws_obj = json_object_array_get_idx (workspaces_array, i);
        gf_workspace_info_t *ws = &list->items[i];

        json_object *id_obj;
        json_object_object_get_ex (ws_obj, "id", &id_obj);
        ws->id = json_object_get_int (id_obj);

        json_object *wc_obj;
        json_object_object_get_ex (ws_obj, "window_count", &wc_obj);
        ws->window_count = json_object_get_int (wc_obj);

        json_object *mw_obj;
        json_object_object_get_ex (ws_obj, "max_windows", &mw_obj);
        ws->max_windows = json_object_get_int (mw_obj);

        json_object *as_obj;
        json_object_object_get_ex (ws_obj, "available_space", &as_obj);
        ws->available_space = json_object_get_int (as_obj);

        json_object *locked_obj;
        json_object_object_get_ex (ws_obj, "is_locked", &locked_obj);
        ws->is_locked = json_object_get_boolean (locked_obj);
    }

    json_object_put (root);
    return list;
}

gf_window_list_t *
gf_parse_window_list (const char *json_str)
{
    if (!json_str)
        return NULL;

    json_object *root = json_tokener_parse (json_str);
    if (!root)
        return NULL;

    gf_window_list_t *list = gf_malloc (sizeof (gf_window_list_t));
    if (!list)
    {
        json_object_put (root);
        return NULL;
    }

    json_object *windows_array;
    if (!json_object_object_get_ex (root, "windows", &windows_array))
    {
        gf_free (list);
        json_object_put (root);
        return NULL;
    }

    size_t count = json_object_array_length (windows_array);
    list->items = gf_malloc (sizeof (gf_window_info_t) * count);
    if (!list->items)
    {
        gf_free (list);
        json_object_put (root);
        return NULL;
    }

    list->count = count;
    list->capacity = count;

    for (size_t i = 0; i < count; i++)
    {
        json_object *win_obj = json_object_array_get_idx (windows_array, i);
        gf_window_info_t *win = &list->items[i];

        json_object *id_obj;
        json_object_object_get_ex (win_obj, "id", &id_obj);
        win->id = json_object_get_int64 (id_obj);

        const char *name = NULL;
        json_object *name_obj;

        if (json_object_object_get_ex (win_obj, "name", &name_obj))
            name = json_object_get_string (name_obj);

        snprintf (win->name, sizeof (win->name), "%s",
                  (name && *name) ? name : "Unknown");
        win->name[sizeof (win->name) - 1] = '\0';

        json_object *ws_id_obj;
        json_object_object_get_ex (win_obj, "workspace_id", &ws_id_obj);
        win->workspace_id = json_object_get_int (ws_id_obj);

        // Set defaults for other fields
        win->native_handle = (gf_native_window_t)win->id;
        win->is_maximized = false;
        win->is_minimized = false;
        win->needs_update = false;
        win->is_valid = true;
        memset (&win->geometry, 0, sizeof (win->geometry));
        win->last_modified = time (NULL);
    }

    json_object_put (root);
    return list;
}
