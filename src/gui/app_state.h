#ifndef GF_APP_STATE_H
#define GF_APP_STATE_H

#include "../config/config.h"
#include "../core/types.h"
#include "../ipc/ipc.h"
#include "../ipc/ipc_command.h"
#include "../utils/list.h"
#include "platform/gui_platform.h"
#include <gtk/gtk.h>

typedef struct
{
    GtkWidget *window;
    GtkWidget *workspace_table; // scrolled window hosting the workspace cards
    GtkWidget *server_btn;      // persistent start/stop toggle in the header
    gf_gui_platform_t *platform;
#ifdef _WIN32
    gboolean operation_in_progress;
    void *tray_data;
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
