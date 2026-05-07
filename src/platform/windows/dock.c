#include "internal.h"

// Hide the taskbar by pushing it behind other windows.
// SHAppBarMessage(ABM_SETSTATE) is unreliable from elevated processes
// due to UIPI, so we manipulate the taskbar window directly.
void
gf_dock_hide (gf_platform_t *platform)
{
    (void)platform;

    HWND taskbar = FindWindowA ("Shell_TrayWnd", NULL);
    if (taskbar)
    {
        // Set auto-hide via the appbar API (works when not elevated)
        APPBARDATA abd = { .cbSize = sizeof (abd), .hWnd = taskbar };
        abd.lParam = ABS_AUTOHIDE;
        SHAppBarMessage (ABM_SETSTATE, &abd);

        // Also directly hide the taskbar as a reliable fallback
        ShowWindow (taskbar, SW_HIDE);
    }

    // Handle multi-monitor secondary taskbars (Windows 10/11)
    HWND secondary = NULL;
    while ((secondary = FindWindowExA (NULL, secondary, "Shell_SecondaryTrayWnd", NULL)))
    {
        ShowWindow (secondary, SW_HIDE);
    }
}

void
gf_dock_restore (gf_platform_t *platform)
{
    (void)platform;

    HWND taskbar = FindWindowA ("Shell_TrayWnd", NULL);
    if (taskbar)
    {
        // Restore always-on-top via appbar API
        APPBARDATA abd = { .cbSize = sizeof (abd), .hWnd = taskbar };
        abd.lParam = ABS_ALWAYSONTOP;
        SHAppBarMessage (ABM_SETSTATE, &abd);

        // Show the taskbar window again
        ShowWindow (taskbar, SW_SHOW);
    }

    // Restore secondary taskbars
    HWND secondary = NULL;
    while ((secondary = FindWindowExA (NULL, secondary, "Shell_SecondaryTrayWnd", NULL)))
    {
        ShowWindow (secondary, SW_SHOW);
    }
}
