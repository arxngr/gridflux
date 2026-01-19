#include "../../../file.h"
#include "../../../logger.h"
#include "../../../types.h"
#include "../backend.h"
#include "../platform.h"

#include "platform_compat.h" // Centralized platform-specific includes
#include <dbus/dbus.h>
#include <stdlib.h>
#include <string.h>

static int
gf_kwin_dbus_call (DBusConnection *conn, const char *method, const char *arg1,
                   const char *arg2)
{
    DBusError error;
    DBusMessage *msg = NULL;
    DBusMessage *reply = NULL;
    DBusMessageIter args;
    int result = -1;

    if (!conn || !method)
    {
        GF_LOG_ERROR ("[kwin] Invalid arguments to dbus call");
        return -1;
    }

    dbus_error_init (&error);

    msg = dbus_message_new_method_call (KWIN_DBUS_SERVICE, KWIN_DBUS_OBJECT_PATH,
                                        KWIN_DBUS_INTERFACE, method);
    if (!msg)
    {
        GF_LOG_ERROR ("[kwin] Failed to create D-Bus message for method: %s", method);
        return -1;
    }

    dbus_message_iter_init_append (msg, &args);
    if (arg1)
    {
        if (!dbus_message_iter_append_basic (&args, DBUS_TYPE_STRING, &arg1))
        {
            GF_LOG_ERROR ("[kwin] Failed to append arg1 to message");
            goto out;
        }
    }
    if (arg2)
    {
        if (!dbus_message_iter_append_basic (&args, DBUS_TYPE_STRING, &arg2))
        {
            GF_LOG_ERROR ("[kwin] Failed to append arg2 to message");
            goto out;
        }
    }

    reply = dbus_connection_send_with_reply_and_block (conn, msg, KWIN_DBUS_TIMEOUT_MS,
                                                       &error);

    if (dbus_error_is_set (&error))
    {
        GF_LOG_ERROR ("[kwin] D-Bus error calling '%s': %s", method, error.message);
        dbus_error_free (&error);
        goto out;
    }

    if (!reply)
    {
        GF_LOG_WARN ("[kwin] Method '%s' returned no reply (void method)", method);
        result = 0;
        goto out;
    }

    if (dbus_message_get_type (reply) == DBUS_MESSAGE_TYPE_ERROR)
    {
        const char *error_msg = dbus_message_get_error_name (reply);
        GF_LOG_ERROR ("[kwin] Method '%s' returned error: %s", method, error_msg);
        goto out;
    }

    if (!dbus_message_iter_init (reply, &args))
    {
        GF_LOG_DEBUG ("[kwin] Method '%s' returned void (no arguments)", method);
        result = 0;
        goto out;
    }

    int arg_type = dbus_message_iter_get_arg_type (&args);

    if (arg_type == DBUS_TYPE_INT32)
    {
        dbus_message_iter_get_basic (&args, &result);
        GF_LOG_INFO ("[kwin] Method '%s' returned int32: %d", method, result);
    }
    else if (arg_type == DBUS_TYPE_BOOLEAN)
    {
        dbus_bool_t bool_result;
        dbus_message_iter_get_basic (&args, &bool_result);
        result = bool_result ? 1 : 0;
        GF_LOG_INFO ("[kwin] Method '%s' returned boolean: %s", method,
                     bool_result ? "true" : "false");
    }
    else if (arg_type == DBUS_TYPE_STRING)
    {
        char *str_result;
        dbus_message_iter_get_basic (&args, &str_result);
        GF_LOG_INFO ("[kwin] Method '%s' returned string: %s", method, str_result);
        result = 0;
    }
    else if (arg_type == DBUS_TYPE_INVALID)
    {
        GF_LOG_WARN ("[kwin] Method '%s' returned empty message", method);
        result = 0;
    }
    else
    {
        GF_LOG_WARN ("[kwin] Method '%s' returned unexpected type: %c (0x%02x)", method,
                     arg_type, arg_type);
        result = 0;
    }

out:
    if (reply)
        dbus_message_unref (reply);
    if (msg)
        dbus_message_unref (msg);

    return result;
}

gf_error_code_t
gf_kwin_platform_init (gf_platform_interface_t *platform)
{
    gf_linux_platform_data_t *data;
    DBusError error;
    char *script_path = NULL;
    int ret = -1;
    int fd = -1;
    char tmp_template[] = KWIN_TEMP_TEMPLATE;

    GF_LOG_INFO ("[kwin] init: starting");

    if (!platform || !platform->platform_data)
    {
        GF_LOG_ERROR ("[kwin] init: invalid platform");
        return GF_ERROR_PLATFORM_ERROR;
    }

    data = (gf_linux_platform_data_t *)platform->platform_data;
    data->kwin_dbus_conn = NULL;
    data->kwin_script_name = NULL;

    dbus_error_init (&error);

    data->kwin_dbus_conn = dbus_bus_get (DBUS_BUS_SESSION, &error);
    if (dbus_error_is_set (&error) || !data->kwin_dbus_conn)
    {
        GF_LOG_ERROR ("[kwin] init: D-Bus connection failed: %s",
                      error.message ? error.message : "unknown");
        dbus_error_free (&error);
        goto fail;
    }

    data->kwin_script_name = strdup (KWIN_SCRIPT_NAME);
    if (!data->kwin_script_name)
    {
        GF_LOG_ERROR ("[kwin] init: failed to allocate script name");
        goto fail;
    }
    GF_LOG_INFO ("[kwin] init: script name set to '%s'", data->kwin_script_name);

    for (size_t i = 0; KWIN_SCRIPT_SEARCH_PATHS[i]; i++)
    {
        GF_LOG_DEBUG ("[kwin] searching for script at: %s", KWIN_SCRIPT_SEARCH_PATHS[i]);
        if (access (KWIN_SCRIPT_SEARCH_PATHS[i], R_OK) == 0)
        {
            script_path = strdup (KWIN_SCRIPT_SEARCH_PATHS[i]);
            GF_LOG_INFO ("[kwin] found script at: %s", script_path);
            break;
        }
    }

    if (!script_path)
    {
        GF_LOG_ERROR ("[kwin] init: script not found in any search path");
        goto fail;
    }

    ret = gf_kwin_dbus_call (data->kwin_dbus_conn, "isScriptLoaded",
                             data->kwin_script_name, NULL);

    if (ret == 1)
    {
        GF_LOG_INFO ("[kwin] init: script already loaded, unloading...");
        gf_kwin_dbus_call (data->kwin_dbus_conn, "unloadScript", data->kwin_script_name,
                           NULL);
        sleep (KWIN_UNLOAD_SLEEP_SEC);
        GF_LOG_INFO ("[kwin] init: script unloaded");
    }

    GF_LOG_DEBUG ("[kwin] attempting to load script from installed location: %s",
                  script_path);
    ret = gf_kwin_dbus_call (data->kwin_dbus_conn, "loadDeclarativeScript", script_path,
                             data->kwin_script_name);

    if (ret >= 0)
    {
        GF_LOG_INFO ("[kwin] init: declarative script loaded successfully from %s",
                     script_path);
        gf_kwin_dbus_call (data->kwin_dbus_conn, "start", NULL, NULL);
        free (script_path);
        return GF_SUCCESS;
    }

    fd = mkstemp (tmp_template);
    if (fd < 0)
    {
        GF_LOG_ERROR ("[kwin] init: failed to create temp file");
        goto fail;
    }

    close (fd);
    fd = -1;

    if (gf_copy_file (script_path, tmp_template) != 0)
    {
        GF_LOG_ERROR ("[kwin] init: failed to copy script from '%s' to '%s'", script_path,
                      tmp_template);
        goto fail;
    }

    ret = gf_kwin_dbus_call (data->kwin_dbus_conn, "loadDeclarativeScript", tmp_template,
                             data->kwin_script_name);

    if (ret < 0)
    {
        GF_LOG_ERROR ("[kwin] init: failed to load declarative script (error code: %d)",
                      ret);
        goto fail;
    }

    gf_kwin_dbus_call (data->kwin_dbus_conn, "start", NULL, NULL);

    // Don't delete temp file - KWin may need it for async loading
    // unlink (tmp_template);
    free (script_path);

    GF_LOG_INFO ("[kwin] init: success");
    return GF_SUCCESS;

fail:
    if (fd >= 0)
        close (fd);
    unlink (tmp_template);

    if (script_path)
        free (script_path);

    if (data->kwin_script_name)
    {
        free (data->kwin_script_name);
        data->kwin_script_name = NULL;
    }

    if (data->kwin_dbus_conn)
    {
        dbus_connection_unref (data->kwin_dbus_conn);
        data->kwin_dbus_conn = NULL;
    }

    return GF_ERROR_PLATFORM_ERROR;
}

void
gf_kwin_platform_cleanup (gf_platform_interface_t *platform)
{
    gf_linux_platform_data_t *data;

    if (!platform || !platform->platform_data)
        return;

    data = (gf_linux_platform_data_t *)platform->platform_data;

    if (data->kwin_dbus_conn && data->kwin_script_name)
    {
        gf_kwin_dbus_call (data->kwin_dbus_conn, "unloadScript", data->kwin_script_name,
                           NULL);
    }

    if (data->kwin_script_name)
    {
        free (data->kwin_script_name);
        data->kwin_script_name = NULL;
    }

    if (data->kwin_dbus_conn)
    {
        dbus_connection_unref (data->kwin_dbus_conn);
        data->kwin_dbus_conn = NULL;
    }
}
