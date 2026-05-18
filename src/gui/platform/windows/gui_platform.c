#include "../gui_platform.h"
#include "../../../utils/logger.h"
#include "../../bridge/ipc_client.h"
#include <objbase.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <appmodel.h>

typedef struct
{
    GHashTable *app_paths;
    GHashTable *friendly_names;
} gf_win32_gui_data_t;

typedef LONG (WINAPI *GetPackagePathByFullName_t)(PCWSTR packageFullName, UINT32 *pathLength, PWSTR packagePath);

static bool
find_uwp_logo_file (const char *pkg_path, const char *logo_rel_path, char *out_path, size_t out_len)
{
    if (!pkg_path || !logo_rel_path || logo_rel_path[0] == '\0')
        return false;

    char base_logo[MAX_PATH];
    strncpy (base_logo, logo_rel_path, sizeof (base_logo) - 1);
    base_logo[sizeof (base_logo) - 1] = '\0';

    char *dot = strrchr (base_logo, '.');
    if (dot)
        *dot = '\0';

    char full_base_path[MAX_PATH];
    snprintf (full_base_path, sizeof (full_base_path), "%s\\%s", pkg_path, base_logo);

    char dir_path[MAX_PATH];
    strncpy (dir_path, full_base_path, sizeof (dir_path) - 1);
    dir_path[sizeof (dir_path) - 1] = '\0';
    char *last_slash = strrchr (dir_path, '\\');
    const char *search_pattern = base_logo;
    if (last_slash)
    {
        *last_slash = '\0';
        search_pattern = strrchr (full_base_path, '\\') + 1;
    }
    else
    {
        strncpy (dir_path, pkg_path, sizeof (dir_path) - 1);
        dir_path[sizeof (dir_path) - 1] = '\0';
    }

    char search_expr[MAX_PATH];
    snprintf (search_expr, sizeof (search_expr), "%s\\%s*", dir_path, search_pattern);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA (search_expr, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return false;

    bool found = false;
    char best_match[MAX_PATH] = { 0 };
    int best_score = -1;

    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            size_t flen = strlen (fd.cFileName);
            if (flen > 4 && _stricmp (fd.cFileName + flen - 4, ".png") == 0)
            {
                int score = 0;
                if (strstr (fd.cFileName, "targetsize-48") != NULL) score = 100;
                else if (strstr (fd.cFileName, "targetsize") != NULL) score = 90;
                else if (strstr (fd.cFileName, "scale-200") != NULL) score = 80;
                else if (strstr (fd.cFileName, "scale-100") != NULL) score = 70;
                else score = 50;

                if (score > best_score)
                {
                    best_score = score;
                    snprintf (best_match, sizeof (best_match), "%s\\%s", dir_path, fd.cFileName);
                    found = true;
                }
            }
        }
    } while (FindNextFileA (hFind, &fd));

    FindClose (hFind);

    if (found)
    {
        strncpy (out_path, best_match, out_len - 1);
        out_path[out_len - 1] = '\0';
    }
    return found;
}

static bool
find_file_in_subdirs (const char *parent_dir, const char *filename, char *out_path, size_t out_len)
{
    char search_pattern[MAX_PATH];
    snprintf (search_pattern, sizeof (search_pattern), "%s\\*", parent_dir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA (search_pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return false;

    bool found = false;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (strcmp (fd.cFileName, ".") != 0 && strcmp (fd.cFileName, "..") != 0)
            {
                char sub_path[MAX_PATH];
                snprintf (sub_path, sizeof (sub_path), "%s\\%s\\%s", parent_dir, fd.cFileName, filename);
                if (GetFileAttributesA (sub_path) != INVALID_FILE_ATTRIBUTES)
                {
                    strncpy (out_path, sub_path, out_len - 1);
                    out_path[out_len - 1] = '\0';
                    found = true;
                    break;
                }
            }
        }
    } while (FindNextFileA (hFind, &fd));

    FindClose (hFind);
    return found;
}

static const char *
lookup_app_path_fuzzy (GHashTable *app_paths, const char *clean_key)
{
    const char *path = g_hash_table_lookup (app_paths, clean_key);
    if (path)
        return path;

    if (strstr (clean_key, ".root.exe") != NULL)
    {
        char fallback_key[MAX_PATH];
        strncpy (fallback_key, clean_key, sizeof (fallback_key) - 1);
        fallback_key[sizeof (fallback_key) - 1] = '\0';
        char *p = strstr (fallback_key, ".root.exe");
        if (p)
        {
            strcpy (p, ".exe");
            path = g_hash_table_lookup (app_paths, fallback_key);
            if (path)
                return path;
        }
    }

    char base_name[MAX_PATH];
    strncpy (base_name, clean_key, sizeof (base_name) - 1);
    base_name[sizeof (base_name) - 1] = '\0';
    char *first_dot = strchr (base_name, '.');
    if (first_dot)
        *first_dot = '\0';

    if (base_name[0] != '\0')
    {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init (&iter, app_paths);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            const char *k = (const char *)key;
            char k_base[MAX_PATH];
            strncpy (k_base, k, sizeof (k_base) - 1);
            k_base[sizeof (k_base) - 1] = '\0';
            char *kd = strchr (k_base, '.');
            if (kd)
                *kd = '\0';

            if (_stricmp (base_name, k_base) == 0)
            {
                return (const char *)value;
            }
        }
    }

    return NULL;
}

static const char *
lookup_friendly_name_fuzzy (GHashTable *friendly_names, const char *clean_key)
{
    const char *name = g_hash_table_lookup (friendly_names, clean_key);
    if (name)
        return name;

    if (strstr (clean_key, ".root.exe") != NULL)
    {
        char fallback_key[MAX_PATH];
        strncpy (fallback_key, clean_key, sizeof (fallback_key) - 1);
        fallback_key[sizeof (fallback_key) - 1] = '\0';
        char *p = strstr (fallback_key, ".root.exe");
        if (p)
        {
            strcpy (p, ".exe");
            name = g_hash_table_lookup (friendly_names, fallback_key);
            if (name)
                return name;
        }
    }

    char base_name[MAX_PATH];
    strncpy (base_name, clean_key, sizeof (base_name) - 1);
    base_name[sizeof (base_name) - 1] = '\0';
    char *first_dot = strchr (base_name, '.');
    if (first_dot)
        *first_dot = '\0';

    if (base_name[0] != '\0')
    {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init (&iter, friendly_names);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            const char *k = (const char *)key;
            char k_base[MAX_PATH];
            strncpy (k_base, k, sizeof (k_base) - 1);
            k_base[sizeof (k_base) - 1] = '\0';
            char *kd = strchr (k_base, '.');
            if (kd)
                *kd = '\0';

            if (_stricmp (base_name, k_base) == 0)
            {
                return (const char *)value;
            }
        }
    }

    return NULL;
}

static GdkPaintable *
gf_hicon_to_paintable (HICON hicon)
{
    if (!hicon)
        return NULL;

    ICONINFO icon_info;
    if (!GetIconInfo (hicon, &icon_info))
        return NULL;

    BITMAP bmp;
    GetObject (icon_info.hbmColor, sizeof (BITMAP), &bmp);

    int width = bmp.bmWidth;
    int height = bmp.bmHeight;

    HDC hdc = GetDC (NULL);
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    size_t data_size = width * height * 4;
    guchar *pixels = g_malloc (data_size);

    if (GetDIBits (hdc, icon_info.hbmColor, 0, height, pixels, &bmi, DIB_RGB_COLORS))
    {

        GBytes *bytes = g_bytes_new_take (pixels, data_size);
        GdkTexture *texture = gdk_memory_texture_new (width, height, GDK_MEMORY_DEFAULT,
                                                      bytes, width * 4);
        g_bytes_unref (bytes);

        ReleaseDC (NULL, hdc);
        DeleteObject (icon_info.hbmColor);
        DeleteObject (icon_info.hbmMask);

        return GDK_PAINTABLE (texture);
    }

    g_free (pixels);
    ReleaseDC (NULL, hdc);
    DeleteObject (icon_info.hbmColor);
    DeleteObject (icon_info.hbmMask);

    return NULL;
}

static GdkPaintable *
gf_get_hwnd_icon (HWND hwnd)
{
    if (!hwnd || !IsWindow (hwnd))
        return NULL;

    HICON hicon = NULL;

        // 1. Try SendMessageTimeout to avoid hanging the GUI if the app is unresponsive
    DWORD_PTR result = 0;
    if (SendMessageTimeout (hwnd, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG, 50,
                            &result))
        hicon = (HICON)result;

    if (!hicon
        && SendMessageTimeout (hwnd, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG, 50,
                               &result))
        hicon = (HICON)result;

    if (!hicon
        && SendMessageTimeout (hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 50,
                               &result))
        hicon = (HICON)result;

    if (!hicon)
        hicon = (HICON)GetClassLongPtr (hwnd, GCLP_HICONSM);
    if (!hicon)
        hicon = (HICON)GetClassLongPtr (hwnd, GCLP_HICON);

    if (hicon)
        return gf_hicon_to_paintable (hicon);

        // 2. Fallback: Get the associated process executable and extract its icon
    DWORD pid = 0;
    GetWindowThreadProcessId (hwnd, &pid);
    if (pid)
    {
        HANDLE hProcess = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProcess)
        {
            char exe_path[MAX_PATH] = { 0 };
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameA (hProcess, 0, exe_path, &size))
            {
                GF_LOG_DEBUG ("Fallback icon extraction from executable: %s", exe_path);
                SHFILEINFOA sfi;
                if (SHGetFileInfoA (exe_path, 0, &sfi, sizeof (sfi),
                                    SHGFI_ICON | SHGFI_LARGEICON))
                {
                    GdkPaintable *paintable = gf_hicon_to_paintable (sfi.hIcon);
                    DestroyIcon (sfi.hIcon);
                    CloseHandle (hProcess);
                    if (paintable)
                    {
                        GF_LOG_DEBUG ("Successfully extracted icon from executable %s",
                                      exe_path);
                        return paintable;
                    }
                    else
                    {
                        GF_LOG_WARN ("gf_hicon_to_paintable failed for %s", exe_path);
                    }
                }
                else
                {
                    GF_LOG_WARN ("SHGetFileInfoA failed to get icon for executable: %s",
                                 exe_path);
                }
            }
            CloseHandle (hProcess);
        }
    }

    return NULL;
}

static void
handle_squirrel_update_shortcut (IShellLinkA *shell_link, char *target_path, size_t target_len)
{
    const char *filename = strrchr (target_path, '\\');
    filename = filename ? filename + 1 : target_path;

    if (_stricmp (filename, "Update.exe") != 0)
        return;

    char args[MAX_PATH] = { 0 };
    if (FAILED (shell_link->lpVtbl->GetArguments (shell_link, args, sizeof (args))))
        return;

    const char *p = strstr (args, "--processStart");
    if (!p)
        return;

    p += 14;
    while (*p == ' ' || *p == '"')
        p++;

    char exe_name[MAX_PATH] = { 0 };
    size_t idx = 0;
    while (*p && *p != ' ' && *p != '"' && idx < sizeof (exe_name) - 1)
    {
        exe_name[idx++] = *p++;
    }
    exe_name[idx] = '\0';

    size_t elen = strlen (exe_name);
    if (elen > 4 && _stricmp (exe_name + elen - 4, ".exe") == 0)
    {
        char parent_dir[MAX_PATH];
        strncpy (parent_dir, target_path, sizeof (parent_dir) - 1);
        parent_dir[sizeof (parent_dir) - 1] = '\0';
        char *last_slash = strrchr (parent_dir, '\\');
        if (last_slash)
            *last_slash = '\0';

        char real_exe_path[MAX_PATH] = { 0 };
        if (find_file_in_subdirs (parent_dir, exe_name, real_exe_path, sizeof (real_exe_path)))
        {
            strncpy (target_path, real_exe_path, target_len - 1);
            target_path[target_len - 1] = '\0';
        }
        else
        {
            snprintf (target_path, target_len, "C:\\SquirrelApps\\%s", exe_name);
        }
    }
}

static bool
resolve_shortcut (const char *lnk_path, char *target_path, size_t target_len, char *icon_path, size_t icon_len)
{
    IShellLinkA *shell_link = NULL;
    IPersistFile *persist_file = NULL;
    bool success = false;

    if (icon_path && icon_len > 0)
        icon_path[0] = '\0';

    if (SUCCEEDED (CoCreateInstance (&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                     &IID_IShellLinkA, (void **)&shell_link)))
    {
        if (SUCCEEDED (shell_link->lpVtbl->QueryInterface (shell_link, &IID_IPersistFile,
                                                            (void **)&persist_file)))
        {
            WCHAR wpath[MAX_PATH];
            MultiByteToWideChar (CP_UTF8, 0, lnk_path, -1, wpath, MAX_PATH);

            if (SUCCEEDED (persist_file->lpVtbl->Load (persist_file, wpath, STGM_READ)))
            {
                if (SUCCEEDED (shell_link->lpVtbl->GetPath (shell_link, target_path,
                                                            (int)target_len, NULL,
                                                            SLGP_UNCPRIORITY)))
                {
                    success = (target_path[0] != '\0');

                    if (success)
                    {
                        handle_squirrel_update_shortcut (shell_link, target_path, target_len);

                        if (icon_path && icon_len > 0)
                        {
                            char raw_icon_path[MAX_PATH] = { 0 };
                            int icon_idx = 0;
                            if (SUCCEEDED (shell_link->lpVtbl->GetIconLocation (shell_link, raw_icon_path, sizeof (raw_icon_path), &icon_idx)) && raw_icon_path[0] != '\0')
                            {
                                ExpandEnvironmentStringsA (raw_icon_path, icon_path, (DWORD)icon_len);
                            }
                            else
                            {
                                strncpy (icon_path, target_path, icon_len - 1);
                                icon_path[icon_len - 1] = '\0';
                            }
                        }
                    }
                }
            }
            persist_file->lpVtbl->Release (persist_file);
        }
        shell_link->lpVtbl->Release (shell_link);
    }
    return success;
}

static void
extract_class_from_display_string (const char *display_string, char *out_class, size_t out_size)
{
    if (!display_string || !out_class || out_size == 0)
        return;

    out_class[0] = '\0';
    const char *open = strchr (display_string, '[');
    const char *close = strchr (display_string, ']');
    if (open && close && close > open)
    {
        size_t len = close - open - 1;
        if (len >= out_size)
            len = out_size - 1;
        memcpy (out_class, open + 1, len);
        out_class[len] = '\0';
    }
}

static bool
model_has_class (GtkStringList *model, const char *wm_class)
{
    if (!model || !wm_class)
        return false;

    guint n = g_list_model_get_n_items (G_LIST_MODEL (model));
    for (guint i = 0; i < n; i++)
    {
        const char *existing = gtk_string_list_get_string (model, i);
        if (existing)
        {
            char existing_class[MAX_PATH];
            extract_class_from_display_string (existing, existing_class, sizeof (existing_class));
            if (_stricmp (existing_class, wm_class) == 0)
                return true;
        }
    }
    return false;
}

static void
get_clean_exe_key (const char *wm_class, char *out_key, size_t out_size)
{
    if (!wm_class || !out_key || out_size == 0)
        return;

    char temp[MAX_PATH];
    strncpy (temp, wm_class, sizeof (temp) - 1);
    temp[sizeof (temp) - 1] = '\0';

    char *open = strchr (temp, '[');
    char *close = strchr (temp, ']');
    if (open && close && close > open)
    {
        *close = '\0';
        memmove (temp, open + 1, strlen (open + 1) + 1);
    }

    char *pipe = strchr (temp, '|');
    char *exe_part = pipe ? pipe + 1 : temp;

    size_t i;
    for (i = 0; exe_part[i] && i < out_size - 1; i++)
    {
        out_key[i] = g_ascii_tolower (exe_part[i]);
    }
    out_key[i] = '\0';
}

static void
get_fallback_friendly_name (const char *exe_name, char *out_name, size_t out_size)
{
    if (!exe_name || !out_name || out_size == 0)
        return;

    strncpy (out_name, exe_name, out_size - 1);
    out_name[out_size - 1] = '\0';
    char *ext = strrchr (out_name, '.');
    if (ext)
        *ext = '\0';

    if (out_name[0] >= 'a' && out_name[0] <= 'z')
        out_name[0] = out_name[0] - 'a' + 'A';
}

static bool
extract_xml_attribute (const char *xml_block, const char *attribute_name, char *out_val, size_t out_size)
{
    if (!xml_block || !attribute_name || !out_val || out_size == 0)
        return false;

    out_val[0] = '\0';
    char search_pattern[128];
    snprintf (search_pattern, sizeof (search_pattern), "%s=\"", attribute_name);

    const char *p = strstr (xml_block, search_pattern);
    if (p)
    {
        p += strlen (search_pattern);
        size_t idx = 0;
        while (*p && *p != '"' && idx < out_size - 1)
        {
            out_val[idx++] = *p++;
        }
        out_val[idx] = '\0';
        return true;
    }
    return false;
}

static bool
extract_xml_tag_content (const char *xml_block, const char *tag_name, char *out_val, size_t out_size)
{
    if (!xml_block || !tag_name || !out_val || out_size == 0)
        return false;

    out_val[0] = '\0';
    char open_tag[128];
    char close_tag[128];
    snprintf (open_tag, sizeof (open_tag), "<%s>", tag_name);
    snprintf (close_tag, sizeof (close_tag), "</%s>", tag_name);

    const char *p = strstr (xml_block, open_tag);
    if (p)
    {
        p += strlen (open_tag);
        const char *end = strstr (p, close_tag);
        if (end)
        {
            size_t len = end - p;
            if (len >= out_size)
                len = out_size - 1;
            memcpy (out_val, p, len);
            out_val[len] = '\0';
            return true;
        }
    }
    return false;
}

static void
add_app_to_model (gf_win32_gui_data_t *data, GtkStringList *model, 
                  const char *friendly_name, const char *wm_class, const char *icon_or_exe_path)
{
    if (model_has_class (model, wm_class))
        return;

    char display_string[MAX_PATH * 2];
    snprintf (display_string, sizeof (display_string), "%s [%s]", friendly_name, wm_class);
    gtk_string_list_append (model, display_string);

    char clean_key[MAX_PATH];
    get_clean_exe_key (wm_class, clean_key, sizeof (clean_key));

    if (icon_or_exe_path && icon_or_exe_path[0] != '\0')
    {
        if (!g_hash_table_contains (data->app_paths, clean_key))
        {
            g_hash_table_insert (data->app_paths, g_strdup (clean_key), g_strdup (icon_or_exe_path));
        }
    }
    if (!g_hash_table_contains (data->friendly_names, clean_key))
    {
        g_hash_table_insert (data->friendly_names, g_strdup (clean_key), g_strdup (friendly_name));
    }
}

static void
process_shortcut_file (gf_win32_gui_data_t *data, GtkStringList *model, 
                       const char *dir_path, const char *lnk_filename)
{
    char lnk_path[MAX_PATH];
    snprintf (lnk_path, sizeof (lnk_path), "%s\\%s", dir_path, lnk_filename);

    char target_path[MAX_PATH] = { 0 };
    char icon_path[MAX_PATH] = { 0 };
    if (resolve_shortcut (lnk_path, target_path, sizeof (target_path), icon_path, sizeof (icon_path)))
    {
        size_t tlen = strlen (target_path);
        if (tlen > 4 && _stricmp (target_path + tlen - 4, ".exe") == 0)
        {
            const char *filename = strrchr (target_path, '\\');
            filename = filename ? filename + 1 : target_path;

            char friendly_name[MAX_PATH];
            strncpy (friendly_name, lnk_filename, sizeof (friendly_name) - 1);
            friendly_name[sizeof (friendly_name) - 1] = '\0';
            char *dot = strrchr (friendly_name, '.');
            if (dot && _stricmp (dot, ".lnk") == 0)
                *dot = '\0';

            add_app_to_model (data, model, friendly_name, filename, icon_path);
        }
    }
}

static void
scan_windows_start_menu (gf_win32_gui_data_t *data, GtkStringList *model,
                         const char *dir_path)
{
    char search_path[MAX_PATH];
    snprintf (search_path, sizeof (search_path), "%s\\*.lnk", dir_path);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA (search_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            process_shortcut_file (data, model, dir_path, fd.cFileName);
        } while (FindNextFileA (hFind, &fd));
        FindClose (hFind);
    }

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
                snprintf (subdir_path, sizeof (subdir_path), "%s\\%s", dir_path, fd.cFileName);

                char subdir_search[MAX_PATH];
                snprintf (subdir_search, sizeof (subdir_search), "%s\\*.lnk", subdir_path);
                WIN32_FIND_DATAA subd_fd;
                HANDLE hdFind = FindFirstFileA (subdir_search, &subd_fd);
                if (hdFind != INVALID_HANDLE_VALUE)
                {
                    do
                    {
                        process_shortcut_file (data, model, subdir_path, subd_fd.cFileName);
                    } while (FindNextFileA (hdFind, &subd_fd));
                    FindClose (hdFind);
                }
            }
        } while (FindNextFileA (hFind, &fd));
        FindClose (hFind);
    }
}

static bool
is_system_package (const char *exe_name)
{
    return (_stricmp (exe_name, "ShellExperienceHost.exe") == 0
            || _stricmp (exe_name, "StartMenuExperienceHost.exe") == 0
            || _stricmp (exe_name, "SecureAssessmentBrowser.exe") == 0
            || _stricmp (exe_name, "XGpuEjectDialog.exe") == 0
            || _stricmp (exe_name, "LockApp.exe") == 0
            || _stricmp (exe_name, "SystemSettingsBroker.exe") == 0
            || _stricmp (exe_name, "PickerHost.exe") == 0);
}

static bool
is_app_list_entry_none (const char *manifest_content)
{
    const char *p = strstr (manifest_content, "Executable=\"");
    if (!p)
        return false;

    const char *app_start = p;
    while (app_start > manifest_content && *app_start != '<')
    {
        app_start--;
    }

    bool is_none = false;
    const char *tag_end = strchr (app_start, '>');
    if (tag_end && tag_end > p)
    {
        size_t len = tag_end - app_start;
        char *temp_tag = malloc (len + 1);
        if (temp_tag)
        {
            memcpy (temp_tag, app_start, len);
            temp_tag[len] = '\0';
            if (strstr (temp_tag, "AppListEntry=\"none\"") != NULL)
            {
                is_none = true;
            }
            free (temp_tag);
        }
    }
    return is_none;
}

static void
parse_manifest_and_add_app (gf_win32_gui_data_t *data, GtkStringList *model,
                            const char *manifest_content, const char *pkg_path)
{
    char exe_name[MAX_PATH] = { 0 };
    if (!extract_xml_attribute (manifest_content, "Executable", exe_name, sizeof (exe_name)) || exe_name[0] == '\0')
        return;

    if (is_app_list_entry_none (manifest_content))
        return;

    if (is_system_package (exe_name))
        return;

    char friendly_name[MAX_PATH] = { 0 };
    extract_xml_tag_content (manifest_content, "DisplayName", friendly_name, sizeof (friendly_name));

    if (friendly_name[0] == '\0' || strncmp (friendly_name, "ms-resource:", 12) == 0)
    {
        extract_xml_attribute (manifest_content, "DisplayName", friendly_name, sizeof (friendly_name));
    }

    if (friendly_name[0] == '\0' || strncmp (friendly_name, "ms-resource:", 12) == 0)
    {
        get_fallback_friendly_name (exe_name, friendly_name, sizeof (friendly_name));
    }

    char icon_path[MAX_PATH] = { 0 };
    char logo_rel_path[MAX_PATH] = { 0 };
    extract_xml_attribute (manifest_content, "Square44x44Logo", logo_rel_path, sizeof (logo_rel_path));
    if (logo_rel_path[0] == '\0')
    {
        extract_xml_attribute (manifest_content, "Logo", logo_rel_path, sizeof (logo_rel_path));
    }

    if (logo_rel_path[0] != '\0' && find_uwp_logo_file (pkg_path, logo_rel_path, icon_path, sizeof (icon_path)))
    {
        // Successfully resolved PNG logo
    }
    else
    {
        snprintf (icon_path, sizeof (icon_path), "%s\\%s", pkg_path, exe_name);
    }

    add_app_to_model (data, model, friendly_name, exe_name, icon_path);
}

static void
process_uwp_package (gf_win32_gui_data_t *data, GtkStringList *model,
                     const char *subkey_name, GetPackagePathByFullName_t pGetPackagePathByFullName)
{
    WCHAR wfullname[MAX_PATH];
    MultiByteToWideChar (CP_UTF8, 0, subkey_name, -1, wfullname, MAX_PATH);

    UINT32 path_len = 0;
    LONG rc = pGetPackagePathByFullName (wfullname, &path_len, NULL);
    if (rc != ERROR_INSUFFICIENT_BUFFER)
        return;

    WCHAR *wpath = malloc (path_len * sizeof (WCHAR));
    if (!wpath)
        return;

    char path[MAX_PATH];
    char manifest_path[MAX_PATH];
    FILE *f = NULL;
    char *content = NULL;

    if (pGetPackagePathByFullName (wfullname, &path_len, wpath) != ERROR_SUCCESS)
        goto cleanup;

    WideCharToMultiByte (CP_UTF8, 0, wpath, -1, path, MAX_PATH, NULL, NULL);
    snprintf (manifest_path, sizeof (manifest_path), "%s\\AppxManifest.xml", path);

    f = fopen (manifest_path, "rb");
    if (!f)
        goto cleanup;

    fseek (f, 0, SEEK_END);
    long size = ftell (f);
    fseek (f, 0, SEEK_SET);

    content = malloc (size + 1);
    if (!content)
        goto cleanup;

    size_t bytes_read = fread (content, 1, size, f);
    content[bytes_read] = '\0';

    parse_manifest_and_add_app (data, model, content, path);

cleanup:
    if (content)
        free (content);
    if (f)
        fclose (f);
    free (wpath);
}

static void
scan_uwp_apps (gf_win32_gui_data_t *data, GtkStringList *model)
{
    HMODULE hAppModel = GetModuleHandleA ("kernel32.dll");
    if (!hAppModel)
        hAppModel = LoadLibraryA ("kernel32.dll");
    if (!hAppModel)
        return;

    GetPackagePathByFullName_t pGetPackagePathByFullName = 
        (GetPackagePathByFullName_t)GetProcAddress (hAppModel, "GetPackagePathByFullName");
    if (!pGetPackagePathByFullName)
        return;

    HKEY hKey;
    LONG result = RegOpenKeyExA (HKEY_CURRENT_USER,
                                 "Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages",
                                 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS)
        return;

    char subkey_name[MAX_PATH];
    DWORD subkey_len = sizeof (subkey_name);
    DWORD index = 0;

    while (RegEnumKeyExA (hKey, index, subkey_name, &subkey_len, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
    {
        process_uwp_package (data, model, subkey_name, pGetPackagePathByFullName);
        index++;
        subkey_len = sizeof (subkey_name);
    }

    RegCloseKey (hKey);
}

static bool
win32_init (gf_gui_platform_t *platform)
{
    gf_win32_gui_data_t *data = (gf_win32_gui_data_t *)platform->platform_data;
    data->app_paths = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    data->friendly_names = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    CoInitializeEx (NULL, COINIT_APARTMENTTHREADED);
    return true;
}

static void
win32_cleanup (gf_gui_platform_t *platform)
{
    gf_win32_gui_data_t *data = (gf_win32_gui_data_t *)platform->platform_data;
    if (data->app_paths)
    {
        g_hash_table_destroy (data->app_paths);
        data->app_paths = NULL;
    }
    if (data->friendly_names)
    {
        g_hash_table_destroy (data->friendly_names);
        data->friendly_names = NULL;
    }
    g_free (data);
    g_free (platform);
}

static GdkPaintable *
win32_get_app_icon (gf_gui_platform_t *platform, const char *wm_class)
{
    gf_win32_gui_data_t *data = (gf_win32_gui_data_t *)platform->platform_data;
    if (!data->app_paths || !wm_class)
        return NULL;

    char clean_key[MAX_PATH];
    get_clean_exe_key (wm_class, clean_key, sizeof (clean_key));

    const char *full_path = lookup_app_path_fuzzy (data->app_paths, clean_key);
    if (full_path)
    {
        size_t plen = strlen (full_path);
        if (plen > 4 && _stricmp (full_path + plen - 4, ".png") == 0)
        {
            GError *error = NULL;
            GdkTexture *texture = gdk_texture_new_from_filename (full_path, &error);
            if (texture)
            {
                return GDK_PAINTABLE (texture);
            }
            if (error)
            {
                g_error_free (error);
            }
        }
        else
        {
            SHFILEINFOA sfi;
            if (SHGetFileInfoA (full_path, 0, &sfi, sizeof (sfi), SHGFI_ICON | SHGFI_LARGEICON))
            {
                GdkPaintable *paintable = gf_hicon_to_paintable (sfi.hIcon);
                DestroyIcon (sfi.hIcon);
                return paintable;
            }
        }
    }
    return NULL;
}

static const char *
win32_get_friendly_name (gf_gui_platform_t *platform, const char *wm_class)
{
    gf_win32_gui_data_t *data = (gf_win32_gui_data_t *)platform->platform_data;
    if (!data->friendly_names || !wm_class)
        return NULL;

    char clean_key[MAX_PATH];
    get_clean_exe_key (wm_class, clean_key, sizeof (clean_key));

    return lookup_friendly_name_fuzzy (data->friendly_names, clean_key);
}

static void
win32_populate_app_dropdown (gf_gui_platform_t *platform, GtkStringList *model)
{
    gf_win32_gui_data_t *data = (gf_win32_gui_data_t *)platform->platform_data;

    // 1. Scan Start Menu and UWP apps directly into model
    char common_programs[MAX_PATH];
    if (SHGetSpecialFolderPathA (NULL, common_programs, CSIDL_COMMON_PROGRAMS, FALSE))
    {
        scan_windows_start_menu (data, model, common_programs);
    }

    char user_programs[MAX_PATH];
    if (SHGetSpecialFolderPathA (NULL, user_programs, CSIDL_PROGRAMS, FALSE))
    {
        scan_windows_start_menu (data, model, user_programs);
    }

    scan_uwp_apps (data, model);

    // 2. Query running apps from the server via IPC to get the clean active taskbar applications!
    gf_ipc_response_t resp = gf_run_client_command ("query apps");
    if (resp.status == GF_IPC_SUCCESS)
    {
        gf_command_response_t *cmd_resp = (gf_command_response_t *)resp.message;
        char apps_buf[sizeof (cmd_resp->message)];
        strncpy (apps_buf, cmd_resp->message, sizeof (apps_buf) - 1);
        apps_buf[sizeof (apps_buf) - 1] = '\0';

        char *line = strtok (apps_buf, "\n");
        while (line)
        {
            if (line[0] != '\0')
            {
                const char *pipe = strchr (line, '|');
                const char *exe_part = pipe ? pipe + 1 : line;

                char clean_key[MAX_PATH];
                get_clean_exe_key (exe_part, clean_key, sizeof (clean_key));

                const char *friendly = lookup_friendly_name_fuzzy (data->friendly_names, clean_key);
                char friendly_fallback[MAX_PATH];
                if (!friendly)
                {
                    get_fallback_friendly_name (exe_part, friendly_fallback, sizeof (friendly_fallback));
                    friendly = friendly_fallback;
                }

                add_app_to_model (data, model, friendly, exe_part, "");
            }
            line = strtok (NULL, "\n");
        }
    }
}

static GdkPaintable *
win32_get_window_icon (gf_gui_platform_t *platform, gf_handle_t window)
{
    (void)platform;
    return gf_get_hwnd_icon ((HWND)window);
}

gf_gui_platform_t *
gf_gui_platform_create (void)
{
    gf_gui_platform_t *platform = g_malloc0 (sizeof (gf_gui_platform_t));
    gf_win32_gui_data_t *data = g_malloc0 (sizeof (gf_win32_gui_data_t));

    platform->platform_data = data;
    platform->init = win32_init;
    platform->cleanup = win32_cleanup;
    platform->get_app_icon = win32_get_app_icon;
    platform->get_friendly_name = win32_get_friendly_name;
    platform->populate_app_dropdown = win32_populate_app_dropdown;
    platform->get_window_icon = win32_get_window_icon;

    return platform;
}
