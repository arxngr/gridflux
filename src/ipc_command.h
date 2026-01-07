#ifndef GF_IPC_COMMAND_H
#define GF_IPC_COMMAND_H

#include "list.h"
#include <stdbool.h>
#include <stdint.h>

gf_workspace_list_t *gf_parse_workspace_list (const char *json_str);
gf_window_list_t *gf_parse_window_list (const char *json_str);
void gf_free_workspace_list (gf_workspace_list_t *list);
void gf_free_window_list (gf_window_list_t *list);

#endif
