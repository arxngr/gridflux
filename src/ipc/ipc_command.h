#ifndef GF_IPC_COMMAND_H
#define GF_IPC_COMMAND_H

#include "../utils/list.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    int type; // 0 = success, 1 = error
    char message[256];
} gf_command_response_t;

gf_ws_list_t *gf_parse_workspace_list (const char *json_str);
gf_win_list_t *gf_parse_window_list (const char *json_str);
void gf_free_workspace_list (gf_ws_list_t *list);
void gf_free_window_list (gf_win_list_t *list);

#endif
