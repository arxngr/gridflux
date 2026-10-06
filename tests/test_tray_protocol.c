#include "../src/gui/window/tray.c"
#include <assert.h>

bool
gf_server_start (void)
{
    return true;
}
bool
gf_server_stop (void)
{
    return true;
}

int
main (void)
{
    GError *error = NULL;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml (tray_xml, &error);
    assert (info && !error);
    assert (g_dbus_node_info_lookup_interface (info, "org.kde.StatusNotifierItem"));
    assert (g_dbus_node_info_lookup_interface (info, "com.canonical.dbusmenu"));
    g_dbus_node_info_unref (info);

    GVariant *layout = g_variant_ref_sink (gf_gui_menu_layout (0, -1));
    assert (g_variant_is_of_type (layout, G_VARIANT_TYPE ("(ia{sv}av)")));
    GVariant *children = g_variant_get_child_value (layout, 2);
    assert (g_variant_n_children (children) == 4);
    for (int i = 0; i < 4; i++)
    {
        GVariant *boxed = g_variant_get_child_value (children, i);
        GVariant *child = g_variant_get_variant (boxed);
        GVariant *props = g_variant_get_child_value (child, 1);
        const char *label = NULL;
        gboolean visible = FALSE;
        assert (g_variant_lookup (props, "label", "&s", &label) && label[0]);
        assert (g_variant_lookup (props, "visible", "b", &visible) && visible);
        g_variant_unref (props);
        g_variant_unref (child);
        g_variant_unref (boxed);
    }
    g_variant_unref (children);
    g_variant_unref (layout);

    GVariant *tooltip = g_variant_ref_sink (
        gf_gui_tray_property (NULL, NULL, NULL, NULL, "ToolTip", NULL, NULL));
    assert (g_variant_is_of_type (tooltip, G_VARIANT_TYPE ("(sa(iiay)ss)")));
    g_variant_unref (tooltip);
    GVariant *path = g_variant_ref_sink (
        gf_gui_tray_property (NULL, NULL, NULL, NULL, "Menu", NULL, NULL));
    assert (g_variant_is_object_path (g_variant_get_string (path, NULL)));
    g_variant_unref (path);
    g_print ("Tray protocol regressions passed\n");
    return 0;
}
