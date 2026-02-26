#include "../gui_platform.h"
#include "../../utils/logger.h"
#include <dirent.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool
unix_init (gf_gui_platform_t *platform)
{
    (void)platform;
    return true;
}

static void
unix_cleanup (gf_gui_platform_t *platform)
{
    (void)platform;
    g_free (platform);
}

static GdkPaintable *
unix_get_app_icon (gf_gui_platform_t *platform, const char *wm_class)
{
    (void)platform;
    GtkIconTheme *theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());

    // Try the wm_class name directly (lowercased)
    char lower[128];
    size_t i;
    for (i = 0; wm_class[i] && i < sizeof (lower) - 1; i++)
        lower[i] = g_ascii_tolower (wm_class[i]);
    lower[i] = '\0';

    // Try several icon name variations
    const char *tries[] = { lower, wm_class, "application-x-executable", NULL };

    for (int t = 0; tries[t]; t++)
    {
        if (gtk_icon_theme_has_icon (theme, tries[t]))
        {
            GtkIconPaintable *icon = gtk_icon_theme_lookup_icon (
                theme, tries[t], NULL, 24, 1, GTK_TEXT_DIR_NONE, 0);
            if (icon)
                return GDK_PAINTABLE (icon);
        }
    }

    // Fallback
    GtkIconPaintable *fallback = gtk_icon_theme_lookup_icon (
        theme, "application-x-executable", NULL, 24, 1, GTK_TEXT_DIR_NONE, 0);
    return fallback ? GDK_PAINTABLE (fallback) : NULL;
}

static void
scan_desktop_files (GtkStringList *model, const char *dir_path)
{
    DIR *dir = opendir (dir_path);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir (dir)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        size_t len = strlen (entry->d_name);
        if (len < 8 || strcmp (entry->d_name + len - 8, ".desktop") != 0)
            continue;

        char filepath[1024];
        snprintf (filepath, sizeof (filepath), "%s/%s", dir_path, entry->d_name);

        FILE *f = fopen (filepath, "r");
        if (!f)
            continue;

        char app_name[256] = { 0 };
        char wm_class[256] = { 0 };
        bool no_display = false;
        char line[1024];

        while (fgets (line, sizeof (line), f))
        {
            if (line[0] == '[' && strncmp (line, "[Desktop Entry]", 15) != 0)
                break;

            if (strncmp (line, "Name=", 5) == 0 && app_name[0] == '\0')
            {
                strncpy (app_name, line + 5, sizeof (app_name) - 1);
                size_t slen = strlen (app_name);
                if (slen > 0 && app_name[slen - 1] == '\n')
                    app_name[slen - 1] = '\0';
            }
            else if (strncmp (line, "StartupWMClass=", 15) == 0)
            {
                strncpy (wm_class, line + 15, sizeof (wm_class) - 1);
                size_t slen = strlen (wm_class);
                if (slen > 0 && wm_class[slen - 1] == '\n')
                    wm_class[slen - 1] = '\0';
            }
            else if (strncmp (line, "NoDisplay=true", 14) == 0)
            {
                no_display = true;
            }
        }
        fclose (f);

        if (no_display || app_name[0] == '\0')
            continue;

        const char *class_to_use = wm_class[0] ? wm_class : NULL;
        if (!class_to_use)
        {
            static char derived[256];
            strncpy (derived, entry->d_name, sizeof (derived) - 1);
            size_t dlen = strlen (derived);
            if (dlen > 8)
                derived[dlen - 8] = '\0';
            class_to_use = derived;
        }

        guint n = g_list_model_get_n_items (G_LIST_MODEL (model));
        bool duplicate = false;
        for (guint i = 0; i < n; i++)
        {
            const char *existing = gtk_string_list_get_string (model, i);
            if (existing && strcmp (existing, class_to_use) == 0)
            {
                duplicate = true;
                break;
            }
        }

        if (!duplicate && class_to_use && class_to_use[0] != '\0')
        {
            gtk_string_list_append (model, class_to_use);
        }
    }
    closedir (dir);
}

static void
unix_populate_app_dropdown (gf_gui_platform_t *platform, GtkStringList *model)
{
    (void)platform;
    scan_desktop_files (model, "/usr/share/applications");

    const char *home = getenv ("HOME");
    if (home)
    {
        char local_apps[512];
        snprintf (local_apps, sizeof (local_apps), "%s/.local/share/applications", home);
        scan_desktop_files (model, local_apps);
    }
}

static GdkPaintable *
unix_get_window_icon (gf_gui_platform_t *platform, gf_handle_t window)
{
    (void)platform;
    (void)window;
    GtkIconTheme *theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());
    GtkIconPaintable *fallback = gtk_icon_theme_lookup_icon (
        theme, "application-x-executable", NULL, 16, 1, GTK_TEXT_DIR_NONE, 0);
    return fallback ? GDK_PAINTABLE (fallback) : NULL;
}

gf_gui_platform_t *
gf_gui_platform_create (void)
{
    gf_gui_platform_t *platform = g_malloc0 (sizeof (gf_gui_platform_t));

    platform->init = unix_init;
    platform->cleanup = unix_cleanup;
    platform->get_app_icon = unix_get_app_icon;
    platform->populate_app_dropdown = unix_populate_app_dropdown;
    platform->get_window_icon = unix_get_window_icon;

    return platform;
}
