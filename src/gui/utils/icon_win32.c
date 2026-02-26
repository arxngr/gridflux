#ifdef _WIN32
#include "icon_win32.h"
#include "../../utils/logger.h"

GdkPaintable *
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

GdkPaintable *
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
#endif
