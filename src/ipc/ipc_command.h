#ifndef GF_IPC_COMMAND_H
#define GF_IPC_COMMAND_H

#include "../utils/list.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    int type; // 0 = success, 1 = error
    char message[256];
} gf_command_response_t;

gf_ws_list_t *gf_parse_workspace_list (const char *buffer, size_t length);
gf_win_list_t *gf_parse_window_list (const char *buffer, size_t length);
bool gf_parse_monitor_list (const char *buffer, size_t length, gf_monitor_t *monitors,
                            uint32_t capacity, uint32_t *count);
bool gf_parse_command_response (const char *buffer, size_t length,
                                gf_command_response_t *response);
void gf_free_workspace_list (gf_ws_list_t *list);
void gf_free_window_list (gf_win_list_t *list);

#endif
