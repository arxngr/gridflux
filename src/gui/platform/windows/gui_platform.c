#include "../gui_platform.h"
#include "../../../utils/logger.h"
#include <objbase.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

typedef struct
{
    GHashTable *app_paths;
} gf_win32_gui_data_t;

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
scan_windows_start_menu (gf_win32_gui_data_t *data, GtkStringList *model,
                         const char *dir_path)
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

                    g_hash_table_insert (data->app_paths, g_strdup (lower_class),
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

                                    g_hash_table_insert (data->app_paths,
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

static bool
win32_init (gf_gui_platform_t *platform)
{
    gf_win32_gui_data_t *data = (gf_win32_gui_data_t *)platform->platform_data;
    data->app_paths = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
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
    // We optionally CoUninitialize() here if required
    g_free (data);
    g_free (platform);
}

static GdkPaintable *
win32_get_app_icon (gf_gui_platform_t *platform, const char *wm_class)
{
    gf_win32_gui_data_t *data = (gf_win32_gui_data_t *)platform->platform_data;
    if (!data->app_paths || !wm_class)
        return NULL;

    char lower_class[MAX_PATH];
    size_t i;
    for (i = 0; wm_class[i] && i < sizeof (lower_class) - 1; i++)
        lower_class[i] = g_ascii_tolower (wm_class[i]);
    lower_class[i] = '\0';

    const char *full_path = g_hash_table_lookup (data->app_paths, lower_class);
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
}

static void
win32_populate_app_dropdown (gf_gui_platform_t *platform, GtkStringList *model)
{
    gf_win32_gui_data_t *data = (gf_win32_gui_data_t *)platform->platform_data;

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
    platform->populate_app_dropdown = win32_populate_app_dropdown;
    platform->get_window_icon = win32_get_window_icon;

    return platform;
}
