#ifndef GF_APP_STATE_H
#define GF_APP_STATE_H

#include "../config/config.h"
#include "../core/types.h"
#include "../ipc/ipc.h"
#include "../ipc/ipc_command.h"
#include "../utils/list.h"
#include <gtk/gtk.h>

typedef struct
{
    GtkWidget *window;
    GtkWidget *workspace_table;
    GtkWidget *ws_dropdown;
    GtkStringList *ws_model;
    GtkWidget *window_dropdown;
    GtkStringList *window_model;
    GtkWidget *target_ws_dropdown;
    GtkStringList *target_ws_model;
#ifdef _WIN32
    gboolean operation_in_progress;
#endif
} gf_app_state_t;

#ifdef _WIN32
typedef struct
{
    gf_app_state_t *app;
    gchar *command;
    gboolean should_refresh;
    gboolean show_dialog;
} gf_cmd_task_t;

typedef struct
{
    gf_app_state_t *app;
    gf_ipc_response_t response;
    gboolean should_refresh;
    gboolean show_dialog;
} gf_cmd_result_t;

typedef struct
{
    gf_app_state_t *app;
} gf_refresh_task_t;
#endif

#endif // GF_APP_STATE_H
