#include "config/config.h"
#include "core/internal.h"
#include "ipc/ipc.h"
#include "ipc/ipc_command.h"
#include <assert.h>
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <sys/utime.h>
#else
#include <utime.h>
#endif

// Configuration reload uses the real core; no IPC server is opened by tests.
gf_ipc_handle_t
gf_ipc_server_create (void)
{
    return -1;
}
void
gf_ipc_server_destroy (gf_ipc_handle_t handle)
{
    (void)handle;
}
bool
gf_ipc_server_process (gf_ipc_handle_t handle, void *user_data)
{
    (void)handle;
    (void)user_data;
    return false;
}

void
gf_log (gf_log_level_t level, const char *format, ...)
{
    (void)level;
    (void)format;
}

static uint32_t
workspace_count (gf_display_t display)
{
    (void)display;
    return 1;
}

static int
command (gf_wm_t *m, const char *text)
{
    gf_ipc_response_t response = { 0 };
    gf_command_response_t result;
    gf_handle_client_message (text, &response, m);
    memcpy (&result, response.message, sizeof (result));
    return result.type;
}

static void
test_persistence (void)
{
    const char *path = gf_config_get_path ();
    FILE *file = fopen (path, "w");
    assert (file);
    fputs ("{\"window_rules\":["
           "{\"wm_class\":\"legacy\",\"workspace_id\":3},"
           "{\"wm_class\":\"unset\",\"workspace_id\":3,\"monitor_id\":null},"
           "{\"wm_class\":\"primary\",\"workspace_id\":2,\"monitor_id\":0},"
           "{\"wm_class\":\"external\",\"workspace_id\":3,\"monitor_id\":2},"
           "{\"wm_class\":\"negative\",\"workspace_id\":3,\"monitor_id\":-1},"
           "{\"wm_class\":\"overflow\",\"workspace_id\":3,\"monitor_id\":16},"
           "{\"wm_class\":\"string\",\"workspace_id\":3,\"monitor_id\":\"2\"}]}\n",
           file);
    assert (fclose (file) == 0);
    gf_config_t cfg = gf_config_load_or_create (path);
    assert (cfg.window_rules_count == 4);
    assert (!cfg.window_rules[0].has_monitor_id && !cfg.window_rules[1].has_monitor_id);
    assert (cfg.window_rules[2].has_monitor_id && cfg.window_rules[2].monitor_id == 0);
    assert (cfg.window_rules[3].has_monitor_id && cfg.window_rules[3].monitor_id == 2);
    gf_config_save (path, &cfg);
    struct json_object *json = json_object_from_file (path), *rules, *monitor;
    assert (json && json_object_object_get_ex (json, "window_rules", &rules));
    assert (!json_object_object_get_ex (json_object_array_get_idx (rules, 0),
                                        "monitor_id", &monitor));
    assert (json_object_object_get_ex (json_object_array_get_idx (rules, 2), "monitor_id",
                                       &monitor));
    assert (json_object_get_int (monitor) == 0);
    json_object_put (json);
    gf_config_t loaded = gf_config_load_or_create (path);
    assert (!gf_config_changed (&cfg, &loaded));
    loaded.window_rules[3].monitor_id = 1;
    assert (gf_config_changed (&cfg, &loaded));
    loaded.window_rules[3] = cfg.window_rules[3];
    loaded.window_rules[3].has_monitor_id = false;
    assert (gf_config_changed (&cfg, &loaded));
    loaded.window_rules[3] = cfg.window_rules[3];
    loaded.window_rules[3].workspace_id = 2;
    assert (gf_config_changed (&cfg, &loaded));
    assert (gf_rules_add (&cfg, "primary", 3, -1) == GF_SUCCESS);
    assert (!gf_rules_find (&cfg, "primary")->has_monitor_id);
    assert (gf_rules_add (&cfg, "invalid", 33, 0) == GF_ERROR_INVALID_PARAMETER);
    assert (gf_rules_add (&cfg, "invalid", 1, 16) == GF_ERROR_INVALID_PARAMETER);
    gf_config_release (&cfg);
    gf_config_release (&loaded);
}

static void
test_commands (void)
{
    gf_config_t cfg = { .max_windows_per_workspace = 1, .max_workspaces = 32 };
    gf_platform_t platform = { .workspace_get_count = workspace_count };
    gf_wm_t m = { .platform = &platform, .config = &cfg };
    m.state.monitor_count = 2;
    assert (gf_window_list_init (&m.state.windows, 8) == GF_SUCCESS);
    assert (gf_workspace_list_init (&m.state.workspaces, 8) == GF_SUCCESS);
    assert (command (&m, "rule add app-a 3 0") == 0);
    assert (command (&m, "rule add app-b 3 1") == 0);
    assert (cfg.window_rules_count == 2);
    assert (command (&m, "rule add app-c 3") == 1);
    assert (command (&m, "rule add app-c 3 1") == 1);
    assert (command (&m, "rule add app-a 3 0") == 0);
    assert (cfg.window_rules_count == 2);
    assert (command (&m, "rule add app-d 4") == 0);
    assert (!gf_rules_find (&cfg, "app-d")->has_monitor_id);
    assert (command (&m, "rule add app-e 5 nope") == 1);
    assert (command (&m, "rule add app-e 5 -1") == 1);
    assert (command (&m, "rule add app-e 5 16") == 1);
    assert (command (&m, "rule add app-e 5 1junk") == 1);
    assert (command (&m, "rule add app-e 5 0 extra") == 1);
    assert (command (&m, "rule add app-e 99999999999999999999 0") == 1);
    assert (command (&m, "rule add app-e 0 0") == 1);
    assert (cfg.window_rules_count == 3);
    gf_ws_info_t *primary = gf_workspace_list_find_by_id (&m.state.workspaces, 3);
    gf_ws_info_t *external = gf_workspace_list_find_by_id (&m.state.workspaces, 35);
    assert (primary && external && primary->has_rule && external->has_rule);
    assert (primary->monitor_id == 0 && external->monitor_id == 1);
    // Connected IDs need not be contiguous. Never expose reserved offline slots.
    m.state.monitor_count = 4;
    for (unsigned i = 0; i < 4; i++)
        m.state.monitors[i]
            = (gf_monitor_t){ .id = i, .full_bounds = { 0, 0, 1920, 1080 } };
    m.state.monitors[1].full_bounds.width = 0;
    gf_ipc_response_t response = { 0 };
    gf_handle_client_message ("query monitors", &response, &m);
    uint32_t connected;
    gf_monitor_t monitors[GF_MAX_MONITORS];
    memcpy (&connected, response.message, sizeof (connected));
    assert (response.status == GF_IPC_SUCCESS && connected == 3);
    memcpy (monitors, response.message + sizeof (connected),
            connected * sizeof (*monitors));
    assert (monitors[0].id == 0 && monitors[1].id == 2 && monitors[2].id == 3);
    m.state.monitors[3].full_bounds.height = 0;
    gf_handle_client_message ("query monitors", &response, &m);
    memcpy (&connected, response.message, sizeof (connected));
    assert (connected == 2);
    m.state.monitors[1].full_bounds.width = 1920;
    gf_handle_client_message ("query monitors", &response, &m);
    memcpy (&connected, response.message, sizeof (connected));
    memcpy (monitors, response.message + sizeof (connected),
            connected * sizeof (*monitors));
    assert (connected == 3 && monitors[1].id == 1 && monitors[2].id == 2);
    gf_config_t loaded = gf_config_load_or_create (gf_config_get_path ());
    assert (!gf_config_changed (&cfg, &loaded));
    gf_config_release (&loaded);
    gf_config_release (&cfg);
    gf_window_list_cleanup (&m.state.windows);
    gf_workspace_list_cleanup (&m.state.workspaces);
}

static void
test_color_reload (void)
{
    const char *path = gf_config_get_path ();
    gf_config_t cfg = gf_config_load_or_create (path);
    gf_platform_t platform = { .workspace_get_count = workspace_count };
    gf_wm_t m = { .config = &cfg, .platform = &platform };
    assert (gf_window_list_init (&m.state.windows, 8) == GF_SUCCESS);
    assert (gf_workspace_list_init (&m.state.workspaces, 8) == GF_SUCCESS);
    struct stat metadata;
    assert (stat (path, &metadata) == 0);
    cfg.last_modified = metadata.st_mtime;
    gf_config_t updated;
    assert (gf_config_dup (&updated, &cfg) == GF_SUCCESS);
    const uint32_t colors[] = { 0x00FF0000, 0x000000FF, 0x00000000 };
    for (unsigned i = 0; i < 3; i++)
    {
        updated.border_color = colors[i];
        gf_config_save (path, &updated);
        // Force every edit to share the original second-resolution timestamp.
#ifdef _WIN32
        struct _utimbuf timestamp = { metadata.st_mtime, metadata.st_mtime };
        assert (_utime (path, &timestamp) == 0);
#else
        struct utimbuf timestamp = { metadata.st_mtime, metadata.st_mtime };
        assert (utime (path, &timestamp) == 0);
#endif
        m.state.loop_counter = 30 * (i + 1);
        gf_wm_load_cfg (&m);
        assert (cfg.border_color == colors[i]);
        assert (cfg.last_modified == metadata.st_mtime);
    }
    gf_config_release (&updated);
    gf_config_release (&cfg);
    gf_window_list_cleanup (&m.state.windows);
    gf_workspace_list_cleanup (&m.state.workspaces);
}

static void
test_selected_config (void)
{
    gf_config_t cfg = gf_config_load_or_create (gf_config_get_path ());
    cfg.border_color = 0x00F49D2A;
    gf_config_save ("config.json", &cfg);
    gf_config_t selected;
    assert (gf_config_dup (&selected, &cfg) == GF_SUCCESS);
    selected.border_color = 0x003584E4;
    gf_config_save ("caller config.json", &selected);
    assert (gf_config_set_path ("caller config.json") == GF_SUCCESS);
    gf_platform_t platform = { .workspace_get_count = workspace_count };
    gf_wm_t m = { .config = &cfg, .platform = &platform };
    assert (gf_window_list_init (&m.state.windows, 8) == GF_SUCCESS);
    assert (gf_workspace_list_init (&m.state.workspaces, 8) == GF_SUCCESS);
    gf_wm_load_cfg (&m);
    assert (cfg.border_color == selected.border_color);
    // IPC mutations must save into the selected user's file too.
    assert (gf_config_workspace_lock (&cfg, 6) == GF_SUCCESS);
    gf_config_t saved = gf_config_load_or_create (gf_config_get_path ());
    assert (saved.border_color == selected.border_color
            && gf_config_workspace_is_locked (&saved, 6));
    gf_config_t original = gf_config_load_or_create ("config.json");
    assert (original.border_color == 0x00F49D2A
            && !gf_config_workspace_is_locked (&original, 6));
    gf_config_release (&saved);
    gf_config_release (&original);
    gf_config_release (&selected);
    gf_config_release (&cfg);
    gf_window_list_cleanup (&m.state.windows);
    gf_workspace_list_cleanup (&m.state.workspaces);
    assert (remove ("config.json") == 0);
}

int
main (void)
{
    // CTest supplies a dedicated build-directory working directory. Dev mode
    // prevents these persistence tests from touching the installed user config.
    assert (strcmp (gf_config_get_path (), "config.json") == 0);
    test_persistence ();
    test_commands ();
    test_color_reload ();
    test_selected_config ();
    assert (remove (gf_config_get_path ()) == 0);
    puts ("Monitor rule persistence and command regressions passed");
    return 0;
}
