#include "ipc.h"
#include "ipc_command.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

bool
gf_ipc_command_length (const char *command, size_t *length)
{
    if (!command || !length)
        return false;
    size_t n = 0;
    while (n < GF_IPC_MSG_SIZE && command[n])
        n++;
    if (!n || n == GF_IPC_MSG_SIZE)
        return false;
    *length = n;
    return true;
}

bool
gf_ipc_response_valid (const gf_ipc_response_t *response)
{
    if (!response)
        return false;
    // Inspect bytes before interpreting an untrusted enum representation.
    for (gf_ipc_status_t status = GF_IPC_SUCCESS; status <= GF_IPC_ERROR_PERMISSION;
         status++)
        if (memcmp (&response->status, &status, sizeof (status)) == 0)
            return true;
    return false;
}

static bool
bool_valid (const char *record, size_t offset)
{
    const bool no = false, yes = true;
    return memcmp (record + offset, &no, sizeof (no)) == 0
           || memcmp (record + offset, &yes, sizeof (yes)) == 0;
}

static bool
list_header (const char *buffer, size_t length, size_t header, size_t item_size,
             uint32_t *count)
{
    if (!buffer || length < header || length > GF_IPC_MSG_SIZE)
        return false;
    uint32_t capacity;
    memcpy (count, buffer, sizeof (*count));
    memcpy (&capacity, buffer + sizeof (*count), sizeof (capacity));
    return *count <= capacity && *count <= (length - header) / item_size;
}

gf_ws_list_t *
gf_parse_workspace_list (const char *buffer, size_t length)
{
    const size_t header
        = 2 * sizeof (uint32_t) + sizeof (((gf_ws_list_t *)0)->active_workspace);
    uint32_t count;
    if (!list_header (buffer, length, header, sizeof (gf_ws_info_t), &count))
        return NULL;
    for (uint32_t i = 0; i < count; i++)
    {
        const char *record = buffer + header + i * sizeof (gf_ws_info_t);
        const size_t flags[] = { offsetof (gf_ws_info_t, is_locked),
                                 offsetof (gf_ws_info_t, has_maximized_state),
                                 offsetof (gf_ws_info_t, is_custom_layout),
                                 offsetof (gf_ws_info_t, has_rule),
                                 offsetof (gf_ws_info_t, is_excluded_ws) };
        for (size_t f = 0; f < sizeof (flags) / sizeof (*flags); f++)
            if (!bool_valid (record, flags[f]))
                return NULL;
        gf_monitor_id_t monitor;
        memcpy (&monitor, record + offsetof (gf_ws_info_t, monitor_id), sizeof (monitor));
        bool excluded;
        memcpy (&excluded, record + offsetof (gf_ws_info_t, is_excluded_ws),
                sizeof (excluded));
        if (monitor >= GF_MAX_MONITORS && !(excluded && monitor == GF_MONITOR_SHARED))
            return NULL;
    }
    gf_ws_list_t *list = calloc (1, sizeof (*list));
    if (!list)
        return NULL;
    if (count)
    {
        list->items = malloc (count * sizeof (*list->items));
        if (!list->items)
        {
            free (list);
            return NULL;
        }
        memcpy (list->items, buffer + header, count * sizeof (*list->items));
    }
    list->count = list->capacity = count;
    memcpy (list->active_workspace, buffer + 2 * sizeof (uint32_t),
            sizeof (list->active_workspace));
    return list;
}

gf_win_list_t *
gf_parse_window_list (const char *buffer, size_t length)
{
    const size_t header = 2 * sizeof (uint32_t);
    uint32_t count;
    if (!list_header (buffer, length, header, sizeof (gf_win_info_t), &count))
        return NULL;
    for (uint32_t i = 0; i < count; i++)
    {
        const char *record = buffer + header + i * sizeof (gf_win_info_t);
        const size_t flags[] = { offsetof (gf_win_info_t, is_maximized),
                                 offsetof (gf_win_info_t, is_minimized),
                                 offsetof (gf_win_info_t, monitor_suspended),
                                 offsetof (gf_win_info_t, needs_update),
                                 offsetof (gf_win_info_t, is_valid),
                                 offsetof (gf_win_info_t, monitor_return)
                                     + offsetof (gf_monitor_return_t, pending),
                                 offsetof (gf_win_info_t, monitor_return)
                                     + offsetof (gf_monitor_return_t, cancelled) };
        for (size_t f = 0; f < sizeof (flags) / sizeof (*flags); f++)
            if (!bool_valid (record, flags[f]))
                return NULL;
        gf_monitor_id_t monitor;
        memcpy (&monitor, record + offsetof (gf_win_info_t, monitor_id),
                sizeof (monitor));
        if (monitor >= GF_MAX_MONITORS
            || !memchr (record + offsetof (gf_win_info_t, name), '\0',
                        sizeof (((gf_win_info_t *)0)->name)))
            return NULL;
    }
    gf_win_list_t *list = calloc (1, sizeof (*list));
    if (!list)
        return NULL;
    if (count)
    {
        list->items = malloc (count * sizeof (*list->items));
        if (!list->items)
        {
            free (list);
            return NULL;
        }
        memcpy (list->items, buffer + header, count * sizeof (*list->items));
    }
    list->count = list->capacity = count;
    return list;
}

bool
gf_parse_monitor_list (const char *buffer, size_t length, gf_monitor_t *monitors,
                       uint32_t capacity, uint32_t *count)
{
    if (!buffer || !monitors || !count || length < sizeof (uint32_t)
        || length > GF_IPC_MSG_SIZE)
        return false;
    uint32_t n;
    memcpy (&n, buffer, sizeof (n));
    if (n > capacity || n > GF_MAX_MONITORS
        || n > (length - sizeof (n)) / sizeof (*monitors))
        return false;
    bool seen[GF_MAX_MONITORS] = { false };
    for (uint32_t i = 0; i < n; i++)
    {
        const char *record = buffer + sizeof (n) + i * sizeof (*monitors);
        if (!bool_valid (record, offsetof (gf_monitor_t, is_primary)))
            return false;
        gf_monitor_id_t id;
        memcpy (&id, record + offsetof (gf_monitor_t, id), sizeof (id));
        if (id >= GF_MAX_MONITORS || seen[id])
            return false;
        seen[id] = true;
    }
    memcpy (monitors, buffer + sizeof (n), n * sizeof (*monitors));
    *count = n;
    return true;
}

bool
gf_parse_command_response (const char *buffer, size_t length,
                           gf_command_response_t *response)
{
    if (!buffer || !response || length < sizeof (*response)
        || !memchr (buffer + offsetof (gf_command_response_t, message), '\0',
                    sizeof (response->message)))
        return false;
    int type;
    memcpy (&type, buffer, sizeof (type));
    if (type != 0 && type != 1)
        return false;
    memcpy (response, buffer, sizeof (*response));
    return true;
}

void
gf_free_workspace_list (gf_ws_list_t *list)
{
    if (!list)
        return;
    free (list->items);
    free (list);
}

void
gf_free_window_list (gf_win_list_t *list)
{
    if (!list)
        return;
    free (list->items);
    free (list);
}
