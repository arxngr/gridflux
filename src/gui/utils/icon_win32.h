#ifndef GF_GUI_ICON_WIN32_H
#define GF_GUI_ICON_WIN32_H

#ifdef _WIN32
#include <gtk/gtk.h>
#include <windows.h>

// Shared utility to convert a Win32 HICON to a GTK4 GdkPaintable
GdkPaintable *gf_hicon_to_paintable (HICON hicon);

// Helper to extract the HICON for a running window handle
GdkPaintable *gf_get_hwnd_icon (HWND hwnd);
#endif

#endif // GF_GUI_ICON_WIN32_H
