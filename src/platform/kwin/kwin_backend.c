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
        GF_LOG_ERROR ("[kwin] failed to create D-Bus message: %s", method);
        return -1;
    }

    dbus_message_iter_init_append (msg, &args);

    if (arg1)
        dbus_message_iter_append_basic (&args, DBUS_TYPE_STRING, &arg1);
    if (arg2)
        dbus_message_iter_append_basic (&args, DBUS_TYPE_STRING, &arg2);

    reply = dbus_connection_send_with_reply_and_block (conn, msg, KWIN_DBUS_TIMEOUT_MS,
                                                       &error);

    if (dbus_error_is_set (&error))
    {
        GF_LOG_ERROR ("[kwin] D-Bus error (%s): %s", method, error.message);
        dbus_error_free (&error);
        goto out;
    }

    if (reply && dbus_message_iter_init (reply, &args)
        && dbus_message_iter_get_arg_type (&args) == DBUS_TYPE_INT32)
    {
        dbus_message_iter_get_basic (&args, &result);
    }

out:
    if (reply)
        dbus_message_unref (reply);
    dbus_message_unref (msg);

    return result;
}

gf_error_code_t
gf_kwin_platform_init (gf_platform_interface_t *platform)
{
    gf_x11_platform_data_t *data;
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

    data = (gf_x11_platform_data_t *)platform->platform_data;
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
        goto fail;

    for (size_t i = 0; KWIN_SCRIPT_SEARCH_PATHS[i]; i++)
    {
        if (access (KWIN_SCRIPT_SEARCH_PATHS[i], R_OK) == 0)
        {
            script_path = strdup (KWIN_SCRIPT_SEARCH_PATHS[i]);
            break;
        }
    }

    if (!script_path)
        goto fail;

    ret = gf_kwin_dbus_call (data->kwin_dbus_conn, "isScriptLoaded",
                             data->kwin_script_name, NULL);

    if (ret == 1)
    {
        gf_kwin_dbus_call (data->kwin_dbus_conn, "unloadScript", data->kwin_script_name,
                           NULL);
        sleep (KWIN_UNLOAD_SLEEP_SEC);
    }

    fd = mkstemp (tmp_template);
    if (fd < 0)
        goto fail;
    close (fd);
    fd = -1;

    if (gf_copy_file (script_path, tmp_template) != 0)
        goto fail;

    ret = gf_kwin_dbus_call (data->kwin_dbus_conn, "loadDeclarativeScript", tmp_template,
                             data->kwin_script_name);

    if (ret < 0)
        goto fail;

    gf_kwin_dbus_call (data->kwin_dbus_conn, "start", NULL, NULL);

    unlink (tmp_template);
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
    gf_x11_platform_data_t *data;

    if (!platform || !platform->platform_data)
        return;

    data = (gf_x11_platform_data_t *)platform->platform_data;

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
