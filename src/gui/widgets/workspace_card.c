#include "workspace_card.h"
#include "../platform/async.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define GF_CHIP_VISIBLE 8 // chips shown before collapsing into "+N more"

typedef struct
{
    gf_app_state_t *app;
    gf_ws_id_t ws;
    bool locked;
    bool has_rule; // rule-pinned: stays locked, cannot be unlocked
} ws_ctx_t;

// A command whose refresh rebuilds the whole card tree; run it from an idle
// callback so we never destroy the widget currently dispatching the event.
typedef struct
{
    gf_app_state_t *app;
    char command[64];
    gboolean dialog;
} deferred_cmd_t;

static gboolean
run_deferred (gpointer user_data)
{
    deferred_cmd_t *d = user_data;
    platform_run_command (d->app, d->command, TRUE, d->dialog);
    g_free (d);
    return G_SOURCE_REMOVE;
}

static void
defer_command (gf_app_state_t *app, const char *command, gboolean dialog)
{
    deferred_cmd_t *d = g_new0 (deferred_cmd_t, 1);
    d->app = app;
    d->dialog = dialog;
    g_strlcpy (d->command, command, sizeof (d->command));
    g_idle_add (run_deferred, d);
}

static GdkPaintable *
icon_from_theme (const char *name)
{
    if (!name || !name[0])
        return NULL;
    GtkIconTheme *theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());

    char lower[256];
    size_t i;
    for (i = 0; i + 1 < sizeof (lower) && name[i]; i++)
        lower[i] = (char)tolower ((unsigned char)name[i]);
    lower[i] = '\0';

    const char *tries[] = { lower, name, "application-x-executable", NULL };
    for (int t = 0; tries[t]; t++)
    {
        if (!gtk_icon_theme_has_icon (theme, tries[t]))
            continue;
        GtkIconPaintable *icon = gtk_icon_theme_lookup_icon (
            theme, tries[t], NULL, 16, 1, GTK_TEXT_DIR_NONE, 0);
        if (icon)
            return GDK_PAINTABLE (icon);
    }
    return NULL;
}

static GdkPaintable *
chip_icon (gf_app_state_t *app, const gf_win_info_t *win)
{
    if (app->platform && app->platform->get_window_icon)
    {
        GdkPaintable *p = app->platform->get_window_icon (app->platform, win->id);
        if (p)
            return p;
    }
    return icon_from_theme (win->name);
}

static GdkContentProvider *
on_chip_prepare (GtkDragSource *source, double x, double y, gpointer user_data)
{
    (void)x;
    (void)y;
    (void)user_data;
    GtkWidget *chip = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (source));
    gsize handle = GPOINTER_TO_SIZE (g_object_get_data (G_OBJECT (chip), "gf-win"));
    return gdk_content_provider_new_typed (G_TYPE_UINT64, (guint64)handle);
}

static gboolean
on_card_drop (GtkDropTarget *target, const GValue *value, double x, double y,
              gpointer user_data)
{
    (void)target;
    (void)x;
    (void)y;
    ws_ctx_t *ctx = user_data;
    if (!G_VALUE_HOLDS_UINT64 (value))
        return FALSE;

    guint64 handle = g_value_get_uint64 (value);
    char command[64];
    snprintf (command, sizeof (command), "move %p %d", (void *)(gsize)handle, ctx->ws);
    defer_command (ctx->app, command, TRUE);
    return TRUE;
}

static void
on_lock_toggle (GtkButton *btn, gpointer user_data)
{
    (void)btn;
    ws_ctx_t *ctx = user_data;
    if (ctx->has_rule) // rule-pinned workspaces cannot be unlocked
    {
        GtkAlertDialog *dialog = gtk_alert_dialog_new (
            "Workspace %d is pinned by a window rule and can't be unlocked.\n"
            "Remove the rule from Rules first.",
            ctx->ws);
        gtk_alert_dialog_show (dialog, GTK_WINDOW (ctx->app->window));
        return;
    }
    char command[64];
    snprintf (command, sizeof (command), "%s %d", ctx->locked ? "unlock" : "lock",
              ctx->ws);
    defer_command (ctx->app, command, FALSE);
}

static void
on_show_more (GtkButton *btn, gpointer user_data)
{
    GtkWidget *flowbox = user_data;
    for (GtkWidget *c = gtk_widget_get_first_child (flowbox); c;
         c = gtk_widget_get_next_sibling (c))
        gtk_widget_set_visible (c, TRUE);
    gtk_widget_set_visible (gtk_widget_get_parent (GTK_WIDGET (btn)), FALSE);
}

// Fallback when the platform has no friendly name: take the part after the last
// '|', drop a trailing ".exe", and capitalise — e.g. "SDL_app|steam.exe" -> "Steam".
static void
prettify_name (const char *raw, char *out, size_t n)
{
    const char *pipe = strrchr (raw, '|');
    g_strlcpy (out, pipe ? pipe + 1 : raw, n);
    size_t len = strlen (out);
    if (len > 4 && g_ascii_strcasecmp (out + len - 4, ".exe") == 0)
        out[len - 4] = '\0';
    if (out[0])
        out[0] = g_ascii_toupper (out[0]);
}

static const char *
chip_label_text (gf_app_state_t *app, const gf_win_info_t *win, char *scratch, size_t n)
{
    const char *friendly = NULL;
    if (app->platform && app->platform->get_friendly_name)
        friendly = app->platform->get_friendly_name (app->platform, win->name);
    if (friendly && friendly[0])
        return friendly;
    prettify_name (win->name, scratch, n);
    return scratch;
}

static GtkWidget *
build_chip (gf_app_state_t *app, const gf_win_info_t *win, bool draggable)
{
    GtkWidget *chip = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class (chip, "gf-chip");

    GdkPaintable *icon = chip_icon (app, win);
    if (icon)
    {
        gtk_box_append (GTK_BOX (chip), gtk_image_new_from_paintable (icon));
        g_object_unref (icon);
    }

    char scratch[128];
    GtkWidget *label = gtk_label_new (chip_label_text (app, win, scratch, sizeof (scratch)));
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 24);
    gtk_box_append (GTK_BOX (chip), label);

    if (!draggable)
    {
        // Rule-pinned windows stay in their workspace; don't offer a drag.
        gtk_widget_set_tooltip_text (chip, "Pinned by a window rule");
        return chip;
    }

    GtkDragSource *source = gtk_drag_source_new ();
    gtk_drag_source_set_actions (source, GDK_ACTION_MOVE);
    g_object_set_data (G_OBJECT (chip), "gf-win", GSIZE_TO_POINTER ((gsize)win->id));
    g_signal_connect (source, "prepare", G_CALLBACK (on_chip_prepare), NULL);
    gtk_widget_add_controller (chip, GTK_EVENT_CONTROLLER (source));
    return chip;
}

static gboolean
window_in_workspace (const gf_win_info_t *win, gf_ws_id_t ws)
{
    return win->is_valid && win->workspace_id == ws && win->name[0] != '\0';
}

static GtkWidget *
build_chips (gf_app_state_t *app, const gf_ws_info_t *ws, const gf_win_list_t *windows)
{
    GtkWidget *fb = gtk_flow_box_new ();
    gtk_widget_add_css_class (fb, "gf-chips");
    gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (fb), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (fb), 24);
    gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (fb), FALSE);

    uint32_t shown = 0, hidden = 0;
    for (uint32_t i = 0; windows && i < windows->count; i++)
    {
        if (!window_in_workspace (&windows->items[i], ws->id))
            continue;
        GtkWidget *chip = build_chip (app, &windows->items[i], !ws->has_rule);
        gtk_flow_box_append (GTK_FLOW_BOX (fb), chip);
        if (shown >= GF_CHIP_VISIBLE)
        {
            gtk_widget_set_visible (gtk_widget_get_parent (chip), FALSE);
            hidden++;
        }
        shown++;
    }

    if (shown == 0)
    {
        GtkWidget *empty = gtk_label_new ("—");
        gtk_widget_add_css_class (empty, "gf-chip-empty");
        gtk_flow_box_append (GTK_FLOW_BOX (fb), empty);
    }
    else if (hidden > 0)
    {
        char buf[24];
        snprintf (buf, sizeof (buf), "+%u more", hidden);
        GtkWidget *more = gtk_button_new_with_label (buf);
        gtk_widget_add_css_class (more, "gf-chip-more");
        g_signal_connect (more, "clicked", G_CALLBACK (on_show_more), fb);
        gtk_flow_box_append (GTK_FLOW_BOX (fb), more);
    }
    return fb;
}

static void
compose_status (const gf_ws_info_t *ws, char *buf, size_t n)
{
    if (ws->has_maximized_state)
        snprintf (buf, n, "%u window%s · maximized", ws->window_count,
                  ws->window_count == 1 ? "" : "s");
    else if (ws->window_count == 0)
        snprintf (buf, n, "Empty · %d slots free", ws->available_space);
    else
        snprintf (buf, n, "%u window%s · %d slot%s free", ws->window_count,
                  ws->window_count == 1 ? "" : "s", ws->available_space,
                  ws->available_space == 1 ? "" : "s");
}

static GtkWidget *
build_number (gf_ws_id_t id)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign (box, GTK_ALIGN_CENTER);

    char idbuf[16];
    snprintf (idbuf, sizeof (idbuf), "%d", id);
    GtkWidget *num = gtk_label_new (idbuf);
    gtk_widget_add_css_class (num, "gf-wsnum");
    GtkWidget *cap = gtk_label_new ("WS");
    gtk_widget_add_css_class (cap, "gf-wsnum-cap");

    gtk_box_append (GTK_BOX (box), num);
    gtk_box_append (GTK_BOX (box), cap);
    return box;
}

static GtkWidget *
build_tile (bool on, bool maximized)
{
    GtkWidget *cell = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class (cell, "gf-tile");
    gtk_widget_add_css_class (cell, maximized ? "max" : (on ? "on" : "off"));
    return cell;
}

static GtkWidget *
build_minimap (uint32_t count, uint32_t cap, bool maximized)
{
    GtkWidget *grid = gtk_grid_new ();
    gtk_widget_add_css_class (grid, "gf-tiles");
    gtk_grid_set_row_spacing (GTK_GRID (grid), 2);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 2);

    if (maximized)
    {
        gtk_grid_attach (GTK_GRID (grid), build_tile (true, true), 0, 0, 1, 1);
        return grid;
    }

    uint32_t show = count > 0 ? count : (cap > 0 ? cap : 4);
    if (show > 16)
        show = 16;
    uint32_t cols = 1;
    while (cols * cols < show)
        cols++;

    for (uint32_t i = 0; i < show; i++)
        gtk_grid_attach (GTK_GRID (grid), build_tile (i < count, false),
                         (int)(i % cols), (int)(i / cols), 1, 1);
    return grid;
}

static GtkWidget *
build_pill (bool maximized)
{
    GtkWidget *pill = gtk_label_new (maximized ? "Maximized" : "Tiled");
    gtk_widget_add_css_class (pill, "gf-pill");
    gtk_widget_add_css_class (pill, maximized ? "max" : "tiled");
    return pill;
}

static GtkWidget *
build_lock (ws_ctx_t *ctx)
{
    bool locked = ctx->locked || ctx->has_rule;
    GtkWidget *btn = gtk_button_new_with_label (locked ? "Locked" : "Open");
    gtk_widget_add_css_class (btn, "gf-lock");
    if (locked)
        gtk_widget_add_css_class (btn, "on");

    if (ctx->has_rule)
        // Pinned by a window rule: clickable, but explains why it can't unlock.
        gtk_widget_set_tooltip_text (btn, "Locked by a window rule");
    g_signal_connect (btn, "clicked", G_CALLBACK (on_lock_toggle), ctx);
    return btn;
}

static GtkWidget *
build_right_column (const gf_ws_info_t *ws, ws_ctx_t *ctx)
{
    GtkWidget *right = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_halign (right, GTK_ALIGN_END);
    gtk_widget_set_valign (right, GTK_ALIGN_CENTER);

    uint32_t cap = ws->max_windows ? ws->max_windows : 4;
    gtk_box_append (GTK_BOX (right),
                    build_minimap (ws->window_count, cap, ws->has_maximized_state));
    gtk_box_append (GTK_BOX (right), build_pill (ws->has_maximized_state));
    gtk_box_append (GTK_BOX (right), build_lock (ctx));
    return right;
}

GtkWidget *
gf_gui_workspace_card_new (const gf_ws_info_t *ws, const gf_win_list_t *windows,
                           gf_app_state_t *app)
{
    ws_ctx_t *ctx = g_new0 (ws_ctx_t, 1);
    ctx->app = app;
    ctx->ws = ws->id;
    ctx->locked = ws->is_locked;
    ctx->has_rule = ws->has_rule;

    GtkWidget *card = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_add_css_class (card, "gf-wscard");
    g_object_set_data_full (G_OBJECT (card), "ctx", ctx, g_free);
    gtk_box_append (GTK_BOX (card), build_number (ws->id));

    GtkWidget *info = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand (info, TRUE);
    gtk_widget_set_valign (info, GTK_ALIGN_CENTER);

    char status[96];
    compose_status (ws, status, sizeof (status));
    GtkWidget *name = gtk_label_new (status);
    gtk_widget_add_css_class (name, "gf-wsname");
    gtk_widget_set_halign (name, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (info), name);
    gtk_box_append (GTK_BOX (info), build_chips (app, ws, windows));
    gtk_box_append (GTK_BOX (card), info);

    gtk_box_append (GTK_BOX (card), build_right_column (ws, ctx));

    if (!ws->is_locked && !ws->has_rule)
    {
        GtkDropTarget *target = gtk_drop_target_new (G_TYPE_UINT64, GDK_ACTION_MOVE);
        g_signal_connect (target, "drop", G_CALLBACK (on_card_drop), ctx);
        gtk_widget_add_controller (card, GTK_EVENT_CONTROLLER (target));
    }
    return card;
}
