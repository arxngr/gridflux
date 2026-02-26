#include "rules_panel.h"
#include "../../config/rules.h"
#include "../bridge/ipc_client.h"
#include "../bridge/refresh.h"
#include "../platform/async.h"
#include "../utils/icon_win32.h"
#include "settings_panel.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

#ifdef __linux__
#include <dirent.h>
#endif

#ifdef _WIN32
#include <objbase.h>
#include <shlobj.h>
#include <windows.h>

// Hash table to store wm_class (executable name) -> full path mapping
// Used by get_app_icon to retrieve the icon from the executable
static GHashTable *win32_app_paths = NULL;
#endif

typedef struct
{
    GtkWidget *rules_window;
    GtkWidget *rules_grid;
    GtkWidget *app_dropdown;
    GtkStringList *app_model;
    GtkWidget *ws_spin;
    gf_app_state_t *app;
} rules_panel_data_t;

// Forward declarations
static void refresh_rules_list (rules_panel_data_t *data);

static void
on_remove_rule_clicked (GtkButton *btn, gpointer user_data)
{
    (void)user_data;
    const char *wm_class = g_object_get_data (G_OBJECT (btn), "wm_class");
    rules_panel_data_t *pd = g_object_get_data (G_OBJECT (btn), "panel_data");

    char command[256];
    snprintf (command, sizeof (command), "rule remove %s", wm_class);
    gf_ipc_response_t resp = gf_run_client_command (command);
    (void)resp;

    refresh_rules_list (pd);
}

static GdkPaintable *
get_app_icon (const char *wm_class)
{
#ifdef __linux__
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
#endif

#ifdef _WIN32
    if (!win32_app_paths || !wm_class)
        return NULL;

    char lower_class[MAX_PATH];
    size_t i;
    for (i = 0; wm_class[i] && i < sizeof (lower_class) - 1; i++)
        lower_class[i] = g_ascii_tolower (wm_class[i]);
    lower_class[i] = '\0';

    const char *full_path = g_hash_table_lookup (win32_app_paths, lower_class);
    if (full_path)
    {
        SHFILEINFOA sfi;
        if (SHGetFileInfoA (full_path, 0, &sfi, sizeof (sfi),
                            SHGFI_ICON | SHGFI_LARGEICON))
        {
            GdkPaintable *paintable = gf_hicon_to_paintable (sfi.hIcon);
            DestroyIcon (sfi.hIcon);
            if (paintable)
                return paintable;
        }
    }

    return NULL;
#endif
}

static void
refresh_rules_list (rules_panel_data_t *data)
{
    GtkWidget *grid = data->rules_grid;

    // Clear existing children
    GtkWidget *child = gtk_widget_get_first_child (grid);
    while (child != NULL)
    {
        GtkWidget *next = gtk_widget_get_next_sibling (child);
        gtk_grid_remove (GTK_GRID (grid), child);
        child = next;
    }

    // Headers: Icon | Application | Workspace | Action
    const char *headers[] = { "Application", "Workspace", "Action" };
    for (int i = 0; i < 3; i++)
    {
        GtkWidget *h = gtk_label_new (headers[i]);
        gtk_widget_add_css_class (h, "table-header");
        gtk_widget_set_halign (h, GTK_ALIGN_START);
        gtk_grid_attach (GTK_GRID (grid), h, i, 0, 1, 1);
    }

    // Load rules from config
    const char *config_path = gf_config_get_path ();
    if (!config_path)
        return;

    gf_config_t config = load_or_create_config (config_path);

    for (uint32_t i = 0; i < config.window_rules_count; i++)
    {
        int row = (int)i + 1;

        // Application column (Icon + Name)
        GtkWidget *app_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_halign (app_box, GTK_ALIGN_START);

        GdkPaintable *icon = get_app_icon (config.window_rules[i].wm_class);
        if (icon)
        {
            GtkWidget *img = gtk_image_new_from_paintable (icon);
            gtk_widget_set_size_request (img, 24, 24);
            gtk_box_append (GTK_BOX (app_box), img);
        }
        else
        {
            gtk_box_append (GTK_BOX (app_box), gtk_label_new ("📦"));
        }

        GtkWidget *class_label = gtk_label_new (config.window_rules[i].wm_class);
        gtk_widget_add_css_class (class_label, "table-cell");
        gtk_box_append (GTK_BOX (app_box), class_label);

        gtk_grid_attach (GTK_GRID (grid), app_box, 0, row, 1, 1);

        // Workspace ID
        char ws_str[16];
        snprintf (ws_str, sizeof (ws_str), "%d", config.window_rules[i].workspace_id);
        GtkWidget *ws_label = gtk_label_new (ws_str);
        gtk_widget_add_css_class (ws_label, "table-cell");
        gtk_widget_set_halign (ws_label, GTK_ALIGN_START);
        gtk_grid_attach (GTK_GRID (grid), ws_label, 1, row, 1, 1);

        // Remove button
        GtkWidget *remove_btn = gtk_button_new_with_label ("✕ Remove");
        char *class_copy = g_strdup (config.window_rules[i].wm_class);
        g_object_set_data_full (G_OBJECT (remove_btn), "wm_class", class_copy, g_free);
        g_object_set_data (G_OBJECT (remove_btn), "panel_data", data);
        g_signal_connect (remove_btn, "clicked", G_CALLBACK (on_remove_rule_clicked),
                          NULL);
        gtk_grid_attach (GTK_GRID (grid), remove_btn, 2, row, 1, 1);
    }

    if (config.window_rules_count == 0)
    {
        GtkWidget *empty = gtk_label_new ("No rules configured yet.");
        gtk_widget_set_halign (empty, GTK_ALIGN_CENTER);
        gtk_widget_set_margin_top (empty, 20);
        gtk_grid_attach (GTK_GRID (grid), empty, 0, 1, 3, 1);
    }
}

#ifdef __linux__
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
#endif

#ifdef _WIN32
static bool
resolve_shortcut (const char *lnk_path, char *target_path, size_t target_len)
{
    IShellLinkA *shell_link = NULL;
    IPersistFile *persist_file = NULL;
    bool success = false;

    if (SUCCEEDED (CoCreateInstance (&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                     &IID_IShellLinkA, (void **)&shell_link)))
    {
        if (SUCCEEDED (shell_link->lpVtbl->QueryInterface (shell_link, &IID_IPersistFile,
                                                           (void **)&persist_file)))
        {
            // Convert path to wide string for IPersistFile
            WCHAR wpath[MAX_PATH];
            MultiByteToWideChar (CP_UTF8, 0, lnk_path, -1, wpath, MAX_PATH);

            if (SUCCEEDED (persist_file->lpVtbl->Load (persist_file, wpath, STGM_READ)))
            {
                if (SUCCEEDED (shell_link->lpVtbl->GetPath (shell_link, target_path,
                                                            (int)target_len, NULL,
                                                            SLGP_UNCPRIORITY)))
                {
                    success = (target_path[0] != '\0');
                }
            }
            persist_file->lpVtbl->Release (persist_file);
        }
        shell_link->lpVtbl->Release (shell_link);
    }
    return success;
}

static void
scan_windows_start_menu (GtkStringList *model, const char *dir_path)
{
    char search_path[MAX_PATH];
    snprintf (search_path, sizeof (search_path), "%s\\*.lnk", dir_path);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA (search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        char lnk_path[MAX_PATH];
        snprintf (lnk_path, sizeof (lnk_path), "%s\\%s", dir_path, fd.cFileName);

        char target_path[MAX_PATH] = { 0 };
        if (resolve_shortcut (lnk_path, target_path, sizeof (target_path)))
        {
            // Only add executables
            size_t tlen = strlen (target_path);
            if (tlen > 4 && _stricmp (target_path + tlen - 4, ".exe") == 0)
            {
                // Extract filename
                const char *filename = strrchr (target_path, '\\');
                filename = filename ? filename + 1 : target_path;

                char wm_class[MAX_PATH];
                strncpy (wm_class, filename, sizeof (wm_class) - 1);
                wm_class[sizeof (wm_class) - 1] = '\0';

                // Check for duplicates
                guint n = g_list_model_get_n_items (G_LIST_MODEL (model));
                bool duplicate = false;
                for (guint i = 0; i < n; i++)
                {
                    const char *existing = gtk_string_list_get_string (model, i);
                    if (existing && _stricmp (existing, wm_class) == 0)
                    {
                        duplicate = true;
                        break;
                    }
                }

                if (!duplicate && wm_class[0] != '\0')
                {
                    gtk_string_list_append (model, wm_class);

                    // Map the lowercased wm_class to the target_path for icon extraction
                    char lower_class[MAX_PATH];
                    size_t j;
                    for (j = 0; wm_class[j] && j < sizeof (lower_class) - 1; j++)
                        lower_class[j] = g_ascii_tolower (wm_class[j]);
                    lower_class[j] = '\0';

                    g_hash_table_insert (win32_app_paths, g_strdup (lower_class),
                                         g_strdup (target_path));
                }
            }
        }
    } while (FindNextFileA (hFind, &fd));

    FindClose (hFind);

    // Scan one level deep into subdirectories
    snprintf (search_path, sizeof (search_path), "%s\\*", dir_path);
    hFind = FindFirstFileA (search_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                && strcmp (fd.cFileName, ".") != 0 && strcmp (fd.cFileName, "..") != 0)
            {
                char subdir_path[MAX_PATH];
                snprintf (subdir_path, sizeof (subdir_path), "%s\\%s", dir_path,
                          fd.cFileName);

                // Read .lnk files inside this directory
                char subdir_search[MAX_PATH];
                snprintf (subdir_search, sizeof (subdir_search), "%s\\*.lnk",
                          subdir_path);
                WIN32_FIND_DATAA subd_fd;
                HANDLE hdFind = FindFirstFileA (subdir_search, &subd_fd);
                if (hdFind != INVALID_HANDLE_VALUE)
                {
                    do
                    {
                        char lnk_path[MAX_PATH];
                        snprintf (lnk_path, sizeof (lnk_path), "%s\\%s", subdir_path,
                                  subd_fd.cFileName);

                        char target_path[MAX_PATH] = { 0 };
                        if (resolve_shortcut (lnk_path, target_path,
                                              sizeof (target_path)))
                        {
                            size_t tlen = strlen (target_path);
                            if (tlen > 4
                                && _stricmp (target_path + tlen - 4, ".exe") == 0)
                            {
                                const char *filename = strrchr (target_path, '\\');
                                filename = filename ? filename + 1 : target_path;

                                char wm_class[MAX_PATH];
                                strncpy (wm_class, filename, sizeof (wm_class) - 1);
                                wm_class[sizeof (wm_class) - 1] = '\0';

                                guint n = g_list_model_get_n_items (G_LIST_MODEL (model));
                                bool duplicate = false;
                                for (guint i = 0; i < n; i++)
                                {
                                    const char *existing
                                        = gtk_string_list_get_string (model, i);
                                    if (existing && _stricmp (existing, wm_class) == 0)
                                    {
                                        duplicate = true;
                                        break;
                                    }
                                }

                                if (!duplicate && wm_class[0] != '\0')
                                {
                                    gtk_string_list_append (model, wm_class);

                                    char lower_class[MAX_PATH];
                                    size_t j;
                                    for (j = 0;
                                         wm_class[j] && j < sizeof (lower_class) - 1; j++)
                                        lower_class[j] = g_ascii_tolower (wm_class[j]);
                                    lower_class[j] = '\0';

                                    g_hash_table_insert (win32_app_paths,
                                                         g_strdup (lower_class),
                                                         g_strdup (target_path));
                                }
                            }
                        }
                    } while (FindNextFileA (hdFind, &subd_fd));
                    FindClose (hdFind);
                }
            }
        } while (FindNextFileA (hFind, &fd));
        FindClose (hFind);
    }
}
#endif

static void
populate_app_dropdown (GtkStringList *model)
{
    // First: add apps from running windows via IPC
    gf_ipc_response_t resp = gf_run_client_command ("query apps");
    if (resp.status == GF_IPC_SUCCESS)
    {
        gf_command_response_t *cmd_resp = (gf_command_response_t *)resp.message;
        // Copy message to avoid strtok modifying original
        char apps_buf[sizeof (cmd_resp->message)];
        strncpy (apps_buf, cmd_resp->message, sizeof (apps_buf) - 1);
        apps_buf[sizeof (apps_buf) - 1] = '\0';

        char *line = strtok (apps_buf, "\n");
        while (line)
        {
            if (line[0] != '\0')
            {
                guint n = g_list_model_get_n_items (G_LIST_MODEL (model));
                bool duplicate = false;
                for (guint i = 0; i < n; i++)
                {
                    const char *existing = gtk_string_list_get_string (model, i);
                    if (existing && strcmp (existing, line) == 0)
                    {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate)
                    gtk_string_list_append (model, line);
            }
            line = strtok (NULL, "\n");
        }
    }

#ifdef __linux__
    scan_desktop_files (model, "/usr/share/applications");

    const char *home = getenv ("HOME");
    if (home)
    {
        char local_apps[512];
        snprintf (local_apps, sizeof (local_apps), "%s/.local/share/applications", home);
        scan_desktop_files (model, local_apps);
    }
#endif

#ifdef _WIN32
    if (!win32_app_paths)
    {
        win32_app_paths = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    }

    CoInitializeEx (NULL, COINIT_APARTMENTTHREADED);

    char common_programs[MAX_PATH];
    if (SHGetSpecialFolderPathA (NULL, common_programs, CSIDL_COMMON_PROGRAMS, FALSE))
    {
        scan_windows_start_menu (model, common_programs);
    }

    char user_programs[MAX_PATH];
    if (SHGetSpecialFolderPathA (NULL, user_programs, CSIDL_PROGRAMS, FALSE))
    {
        scan_windows_start_menu (model, user_programs);
    }

    // Do NOT call CoUninitialize() here if COM is expected to be used by other GTK
    // features or by subsequent refresh scans in the same apartment thread, but for
    // safety in this scope we'll leave it out or call it once during app shutdown if
    // necessary.
#endif
}

static void
setup_app_list_item (GtkSignalListItemFactory *factory, GtkListItem *list_item,
                     gpointer data)
{
    (void)factory;
    (void)data;
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *icon = gtk_image_new ();
    GtkWidget *label = gtk_label_new ("");
    gtk_widget_set_size_request (icon, 20, 20);
    gtk_box_append (GTK_BOX (box), icon);
    gtk_box_append (GTK_BOX (box), label);
    gtk_list_item_set_child (list_item, box);
}

static void
bind_app_list_item (GtkSignalListItemFactory *factory, GtkListItem *list_item,
                    gpointer data)
{
    (void)factory;
    (void)data;
    GtkWidget *box = gtk_list_item_get_child (list_item);
    if (!box)
        return;

    GtkWidget *icon = gtk_widget_get_first_child (box);
    GtkWidget *label = gtk_widget_get_next_sibling (icon);

    gpointer item = gtk_list_item_get_item (list_item);
    if (!item || !GTK_IS_STRING_OBJECT (item))
        return;

    const char *str = gtk_string_object_get_string (GTK_STRING_OBJECT (item));
    gtk_label_set_text (GTK_LABEL (label), str);

    GdkPaintable *paintable = get_app_icon (str);
    if (paintable)
    {
        gtk_image_set_from_paintable (GTK_IMAGE (icon), paintable);
    }
    else
    {
        gtk_image_clear (GTK_IMAGE (icon));
    }
}

static void
on_add_rule_clicked (GtkButton *btn, gpointer user_data)
{
    (void)btn;
    rules_panel_data_t *data = (rules_panel_data_t *)user_data;

    GtkStringObject *item = GTK_STRING_OBJECT (
        gtk_drop_down_get_selected_item (GTK_DROP_DOWN (data->app_dropdown)));
    if (!item)
        return;

    const char *wm_class = gtk_string_object_get_string (item);
    int workspace_id = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (data->ws_spin));

    char command[256];
    snprintf (command, sizeof (command), "rule add %s %d", wm_class, workspace_id);
    gf_ipc_response_t resp = gf_run_client_command (command);
    gf_command_response_t *cmd_resp = (gf_command_response_t *)resp.message;

    if (resp.status == GF_IPC_SUCCESS && cmd_resp->type == 0)
    {
        refresh_rules_list (data);
    }
    else
    {
        GtkAlertDialog *dialog = gtk_alert_dialog_new ("%s", cmd_resp->message);
        gtk_alert_dialog_show (dialog, GTK_WINDOW (data->rules_window));
    }
}

static void
on_rules_window_destroy (GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    rules_panel_data_t *data = (rules_panel_data_t *)user_data;
    g_free (data);
}

void
on_rules_button_clicked (GtkButton *btn, gpointer data)
{
    (void)btn;
    gf_app_state_t *app = (gf_app_state_t *)data;

    rules_panel_data_t *panel_data = g_new0 (rules_panel_data_t, 1);
    panel_data->app = app;

    GtkWidget *rules_window = gtk_window_new ();
    gtk_window_set_title (GTK_WINDOW (rules_window), "Window Rules");
    gtk_window_set_default_size (GTK_WINDOW (rules_window), 550, 500);
    gtk_window_set_modal (GTK_WINDOW (rules_window), TRUE);
    gtk_window_set_transient_for (GTK_WINDOW (rules_window), GTK_WINDOW (app->window));
    panel_data->rules_window = rules_window;

    g_signal_connect (rules_window, "destroy", G_CALLBACK (on_rules_window_destroy),
                      panel_data);

    GtkWidget *main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start (main_box, 20);
    gtk_widget_set_margin_end (main_box, 20);
    gtk_widget_set_margin_top (main_box, 20);
    gtk_widget_set_margin_bottom (main_box, 20);
    gtk_window_set_child (GTK_WINDOW (rules_window), main_box);

    // --- Add Rule Section ---
    GtkWidget *add_label = gtk_label_new ("Add New Rule");
    gtk_widget_set_halign (add_label, GTK_ALIGN_START);
    PangoAttrList *attrs = pango_attr_list_new ();
    pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
    pango_attr_list_insert (attrs, pango_attr_size_new (14 * PANGO_SCALE));
    gtk_label_set_attributes (GTK_LABEL (add_label), attrs);
    pango_attr_list_unref (attrs);
    gtk_box_append (GTK_BOX (main_box), add_label);

    // Form row 1: Application dropdown
    GtkWidget *form1 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (main_box), form1);

    GtkWidget *app_label = gtk_label_new ("Application:");
    gtk_widget_set_size_request (app_label, 100, -1);
    gtk_widget_set_halign (app_label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (form1), app_label);

    panel_data->app_model = gtk_string_list_new (NULL);
    populate_app_dropdown (panel_data->app_model);

    // Use GtkExpression for search to work properly
    GtkExpression *expression
        = gtk_property_expression_new (GTK_TYPE_STRING_OBJECT, NULL, "string");

    GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
    g_signal_connect (factory, "setup", G_CALLBACK (setup_app_list_item), NULL);
    g_signal_connect (factory, "bind", G_CALLBACK (bind_app_list_item), NULL);

    panel_data->app_dropdown
        = gtk_drop_down_new (G_LIST_MODEL (panel_data->app_model), expression);

    gtk_drop_down_set_factory (GTK_DROP_DOWN (panel_data->app_dropdown), factory);
    gtk_drop_down_set_list_factory (GTK_DROP_DOWN (panel_data->app_dropdown), factory);
    g_object_unref (factory);

    gtk_widget_set_size_request (panel_data->app_dropdown, 280, -1);
    gtk_drop_down_set_enable_search (GTK_DROP_DOWN (panel_data->app_dropdown), TRUE);
    gtk_widget_set_hexpand (panel_data->app_dropdown, TRUE);
    gtk_box_append (GTK_BOX (form1), panel_data->app_dropdown);

    // Form row 2: Workspace + Add button
    GtkWidget *form2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append (GTK_BOX (main_box), form2);

    GtkWidget *ws_label = gtk_label_new ("Workspace:");
    gtk_widget_set_size_request (ws_label, 100, -1);
    gtk_widget_set_halign (ws_label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (form2), ws_label);

    panel_data->ws_spin = gtk_spin_button_new_with_range (0, 32, 1);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (panel_data->ws_spin), 1);
    gtk_box_append (GTK_BOX (form2), panel_data->ws_spin);

    GtkWidget *add_btn = gtk_button_new_with_label ("➕ Add Rule");
    gtk_widget_set_hexpand (add_btn, TRUE);
    g_signal_connect (add_btn, "clicked", G_CALLBACK (on_add_rule_clicked), panel_data);
    gtk_box_append (GTK_BOX (form2), add_btn);

    // --- Separator ---
    gtk_box_append (GTK_BOX (main_box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));

    // --- Current Rules Section ---
    GtkWidget *rules_label = gtk_label_new ("Current Rules");
    gtk_widget_set_halign (rules_label, GTK_ALIGN_START);
    PangoAttrList *attrs2 = pango_attr_list_new ();
    pango_attr_list_insert (attrs2, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
    pango_attr_list_insert (attrs2, pango_attr_size_new (14 * PANGO_SCALE));
    gtk_label_set_attributes (GTK_LABEL (rules_label), attrs2);
    pango_attr_list_unref (attrs2);
    gtk_box_append (GTK_BOX (main_box), rules_label);

    GtkWidget *scrolled = gtk_scrolled_window_new ();
    gtk_widget_set_vexpand (scrolled, TRUE);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    gtk_box_append (GTK_BOX (main_box), scrolled);

    panel_data->rules_grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (panel_data->rules_grid), 6);
    gtk_grid_set_column_spacing (GTK_GRID (panel_data->rules_grid), 16);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled),
                                   panel_data->rules_grid);

    refresh_rules_list (panel_data);

    // --- Bottom buttons ---
    GtkWidget *btn_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign (btn_box, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (main_box), btn_box);

    GtkWidget *close_btn = gtk_button_new_with_label ("Close");
    g_signal_connect_swapped (close_btn, "clicked", G_CALLBACK (gtk_window_destroy),
                              rules_window);
    gtk_box_append (GTK_BOX (btn_box), close_btn);

    gtk_window_present (GTK_WINDOW (rules_window));
}
