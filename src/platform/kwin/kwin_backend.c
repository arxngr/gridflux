#include "kwin_backend.h"
#include "../../core/logger.h"
#include "../../utils/file.h"
#include "core/types.h"
#include "platform/x11/x11_window_manager.h"
#include <dbus/dbus.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int
gf_kwin_dbus_call (DBusConnection *conn, const char *method, const char *arg1,
                   const char *arg2)
{
    DBusError error;
    DBusMessage *msg = NULL;
    DBusMessage *reply = NULL;
    DBusMessageIter args;
    int result = -1;

    dbus_error_init (&error);

    msg = dbus_message_new_method_call (KWIN_DBUS_SERVICE, KWIN_DBUS_OBJECT_PATH,
                                        KWIN_DBUS_INTERFACE, method);
    if (!msg)
    {
        GF_LOG_ERROR ("Failed to create D-Bus message for method: %s", method);
        return -1;
    }

    dbus_message_iter_init_append (msg, &args);
    if (arg1)
    {
        dbus_message_iter_append_basic (&args, DBUS_TYPE_STRING, &arg1);
    }
    if (arg2)
    {
        dbus_message_iter_append_basic (&args, DBUS_TYPE_STRING, &arg2);
    }

    reply = dbus_connection_send_with_reply_and_block (conn, msg, KWIN_DBUS_TIMEOUT_MS,
                                                       &error);
    if (dbus_error_is_set (&error))
    {
        GF_LOG_ERROR ("KWin D-Bus error for method '%s': %s", method, error.message);
        dbus_error_free (&error);
        dbus_message_unref (msg);
        return -1;
    }

    if (reply && dbus_message_iter_init (reply, &args))
    {
        if (dbus_message_iter_get_arg_type (&args) == DBUS_TYPE_INT32)
        {
            dbus_message_iter_get_basic (&args, &result);
        }
    }

    dbus_message_unref (msg);
    if (reply)
    {
        dbus_message_unref (reply);
    }

    return result;
}

gf_error_code_t
gf_kwin_platform_init (gf_platform_interface_t *platform)
{
    GF_LOG_INFO ("[kwin] init: starting");

    if (!platform || !platform->platform_data)
    {
        GF_LOG_ERROR ("[kwin] init: invalid platform or platform_data");
        return GF_ERROR_PLATFORM_ERROR;
    }

    gf_x11_platform_data_t *data = (gf_x11_platform_data_t *)platform->platform_data;

    DBusError error;
    dbus_error_init (&error);

    GF_LOG_INFO ("[kwin] init: connecting to session D-Bus...");
    data->kwin_dbus_conn = dbus_bus_get (DBUS_BUS_SESSION, &error);
    if (dbus_error_is_set (&error))
    {
        GF_LOG_ERROR ("[kwin] init: D-Bus error: %s", error.message);
        dbus_error_free (&error);
        return GF_ERROR_PLATFORM_ERROR;
    }

    if (!data->kwin_dbus_conn)
    {
        GF_LOG_ERROR ("[kwin] init: D-Bus connection is NULL");
        return GF_ERROR_PLATFORM_ERROR;
    }

    GF_LOG_INFO ("[kwin] init: D-Bus connected");

    data->kwin_script_name = strdup (KWIN_SCRIPT_NAME);
    if (!data->kwin_script_name)
    {
        GF_LOG_ERROR ("[kwin] init: strdup failed for script name");
        dbus_connection_unref (data->kwin_dbus_conn);
        data->kwin_dbus_conn = NULL;
        return GF_ERROR_PLATFORM_ERROR;
    }

    char *script_path = NULL;
    for (size_t i = 0; KWIN_SCRIPT_SEARCH_PATHS[i] != NULL; ++i)
    {
        GF_LOG_INFO ("[kwin] init: checking script path: %s",
                     KWIN_SCRIPT_SEARCH_PATHS[i]);
        if (access (KWIN_SCRIPT_SEARCH_PATHS[i], R_OK) == 0)
        {
            script_path = strdup (KWIN_SCRIPT_SEARCH_PATHS[i]);
            if (!script_path)
            {
                GF_LOG_ERROR ("[kwin] init: strdup failed for script path");
                gf_free (data->kwin_script_name);
                data->kwin_script_name = NULL;
                dbus_connection_unref (data->kwin_dbus_conn);
                data->kwin_dbus_conn = NULL;
                return GF_ERROR_PLATFORM_ERROR;
            }
            break;
        }
    }

    if (!script_path)
    {
        GF_LOG_ERROR ("[kwin] init: script not found in any search path");
        gf_free (data->kwin_script_name);
        data->kwin_script_name = NULL;
        dbus_connection_unref (data->kwin_dbus_conn);
        data->kwin_dbus_conn = NULL;
        return GF_ERROR_PLATFORM_ERROR;
    }

    GF_LOG_INFO ("[kwin] init: using script path: %s", script_path);

    GF_LOG_INFO ("[kwin] init: querying isScriptLoaded...");
    int is_loaded = gf_kwin_dbus_call (data->kwin_dbus_conn, "isScriptLoaded",
                                       data->kwin_script_name, NULL);
    GF_LOG_INFO ("[kwin] init: isScriptLoaded returned %d", is_loaded);

    if (is_loaded == 1)
    {
        GF_LOG_INFO ("[kwin] init: script already loaded, unloading...");
        gf_kwin_dbus_call (data->kwin_dbus_conn, "unloadScript", data->kwin_script_name,
                           NULL);
        GF_LOG_INFO ("[kwin] init: unloadScript returned, sleeping...");
        sleep (KWIN_UNLOAD_SLEEP_SEC);
    }

    char tmp_template[] = KWIN_TEMP_TEMPLATE;
    GF_LOG_INFO ("[kwin] init: creating temp file...");
    int fd = mkstemp (tmp_template);
    if (fd < 0)
    {
        GF_LOG_ERROR ("[kwin] init: mkstemp failed: %s", strerror (errno));
        gf_free (script_path);
        gf_free (data->kwin_script_name);
        data->kwin_script_name = NULL;
        dbus_connection_unref (data->kwin_dbus_conn);
        data->kwin_dbus_conn = NULL;
        return GF_ERROR_PLATFORM_ERROR;
    }
    close (fd);

    GF_LOG_INFO ("[kwin] init: temp script path: %s", tmp_template);

    GF_LOG_INFO ("[kwin] init: copying script...");
    if (gf_copy_file (script_path, tmp_template) != 0)
    {
        GF_LOG_ERROR ("[kwin] init: script copy failed");
        unlink (tmp_template);
        gf_free (script_path);
        gf_free (data->kwin_script_name);
        data->kwin_script_name = NULL;
        dbus_connection_unref (data->kwin_dbus_conn);
        data->kwin_dbus_conn = NULL;
        return GF_ERROR_PLATFORM_ERROR;
    }

    GF_LOG_INFO ("[kwin] init: loading declarative script...");
    int result = gf_kwin_dbus_call (data->kwin_dbus_conn, "loadDeclarativeScript",
                                    tmp_template, data->kwin_script_name);
    GF_LOG_INFO ("[kwin] init: loadDeclarativeScript returned %d", result);

    if (result < 0)
    {
        GF_LOG_ERROR ("[kwin] init: loadDeclarativeScript failed");
        unlink (tmp_template);
        gf_free (script_path);
        gf_free (data->kwin_script_name);
        data->kwin_script_name = NULL;
        dbus_connection_unref (data->kwin_dbus_conn);
        data->kwin_dbus_conn = NULL;
        return GF_ERROR_PLATFORM_ERROR;
    }

    GF_LOG_INFO ("[kwin] init: starting script...");
    gf_kwin_dbus_call (data->kwin_dbus_conn, "start", NULL, NULL);
    GF_LOG_INFO ("[kwin] init: start call returned");

    unlink (tmp_template);
    gf_free (script_path);

    GF_LOG_INFO ("[kwin] init: success");
    return GF_SUCCESS;
}

void
gf_kwin_platform_cleanup (gf_platform_interface_t *platform)
{
    if (!platform || !platform->platform_data)
    {
        return;
    }

    gf_x11_platform_data_t *data = (gf_x11_platform_data_t *)platform->platform_data;

    if (data->kwin_dbus_conn && data->kwin_script_name)
    {
        gf_kwin_dbus_call (data->kwin_dbus_conn, "unloadScript", data->kwin_script_name,
                           NULL);
    }

    if (data->kwin_script_name)
    {
        gf_free (data->kwin_script_name);
        data->kwin_script_name = NULL;
    }

    if (data->kwin_dbus_conn)
    {
        dbus_connection_unref (data->kwin_dbus_conn);
        data->kwin_dbus_conn = NULL;
    }
}
