#include "statusbar.h"
#include "../bridge/ipc_client.h"

#define GF_HEALTHCHECK_INTERVAL_MS 5000

static void
update_health (GtkWidget *statusbar, gboolean healthy)
{
    if (!gtk_widget_get_root (statusbar))
        return;
    GtkWidget *indicator = g_object_get_data (G_OBJECT (statusbar), "indicator");
    GtkWidget *label = g_object_get_data (G_OBJECT (statusbar), "label");
    if (!indicator || !label)
        return;
    gtk_label_set_text (GTK_LABEL (label), healthy ? "Ready" : "Not Ready");
    gtk_widget_remove_css_class (indicator,
                                 healthy ? "status-not-ready" : "status-ready");
    gtk_widget_add_css_class (indicator, healthy ? "status-ready" : "status-not-ready");
    gtk_widget_remove_css_class (label,
                                 healthy ? "status-text-not-ready" : "status-text-ready");
    gtk_widget_add_css_class (label,
                              healthy ? "status-text-ready" : "status-text-not-ready");
}

#ifdef _WIN32
static void
healthcheck_worker (GTask *task, gpointer source, gpointer task_data,
                    GCancellable *cancellable)
{
    (void)source;
    (void)task_data;
    (void)cancellable;
    gf_ipc_response_t response = gf_run_client_command ("query workspaces");
    g_task_return_boolean (task, response.status == GF_IPC_SUCCESS);
}

static void
healthcheck_ready (GObject *source, GAsyncResult *result, gpointer user_data)
{
    (void)user_data;
    g_object_set_data (source, "healthcheck-pending", NULL);
    update_health (GTK_WIDGET (source), g_task_propagate_boolean (G_TASK (result), NULL));
}
#endif

static gboolean
healthcheck_tick (gpointer user_data)
{
    GtkWidget *statusbar = user_data;
    if (!gtk_widget_get_root (statusbar))
        return G_SOURCE_REMOVE;
#ifdef _WIN32
    if (g_object_get_data (G_OBJECT (statusbar), "healthcheck-pending"))
        return G_SOURCE_CONTINUE;
    g_object_set_data (G_OBJECT (statusbar), "healthcheck-pending", GINT_TO_POINTER (1));
    GTask *task = g_task_new (statusbar, NULL, healthcheck_ready, NULL);
    g_task_run_in_thread (task, healthcheck_worker);
    g_object_unref (task);
#else
    gf_ipc_response_t response = gf_run_client_command ("query workspaces");
    update_health (statusbar, response.status == GF_IPC_SUCCESS);
#endif
    return G_SOURCE_CONTINUE;
}

GtkWidget *
gf_gui_statusbar_new (void)
{
    GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class (bar, "statusbar");
    gtk_widget_set_halign (bar, GTK_ALIGN_FILL);
    gtk_widget_set_margin_start (bar, 8);
    gtk_widget_set_margin_end (bar, 8);
    gtk_widget_set_margin_top (bar, 4);
    gtk_widget_set_margin_bottom (bar, 4);

    // Status indicator dot
    GtkWidget *indicator = gtk_label_new ("\u2B24");
    gtk_widget_add_css_class (indicator, "status-indicator");
    gtk_widget_add_css_class (indicator, "status-not-ready");
    gtk_widget_set_valign (indicator, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (bar), indicator);

    // Status text label
    GtkWidget *label = gtk_label_new ("Checking...");
    gtk_widget_add_css_class (label, "status-text-not-ready");
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (bar), label);

    g_object_set_data (G_OBJECT (bar), "indicator", indicator);
    g_object_set_data (G_OBJECT (bar), "label", label);

    return bar;
}

void
gf_gui_statusbar_set_text (GtkWidget *statusbar, const char *text)
{
    GtkWidget *label = g_object_get_data (G_OBJECT (statusbar), "label");
    if (label)
        gtk_label_set_text (GTK_LABEL (label), text);
}

void
gf_gui_statusbar_start_healthcheck (GtkWidget *statusbar)
{
    // Run first check immediately, then schedule periodic checks
    healthcheck_tick (statusbar);
    g_timeout_add_full (G_PRIORITY_DEFAULT, GF_HEALTHCHECK_INTERVAL_MS, healthcheck_tick,
                        g_object_ref (statusbar), g_object_unref);
}
