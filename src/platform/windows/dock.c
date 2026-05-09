#include "internal.h"

// Hide the taskbar by setting auto-hide mode and forcing it off-screen.
// SHAppBarMessage(ABM_SETSTATE) alone is unreliable — it changes the setting
// but doesn't force the taskbar to actually hide. We must also set the
// window position to trigger the taskbar to slide away, and broadcast
// a taskbar-created message so the shell re-evaluates the state.
void
gf_dock_hide (gf_platform_t *platform)
{
    (void)platform;

    HWND taskbar = FindWindowA ("Shell_TrayWnd", NULL);
    if (taskbar)
    {
        // 1. Set auto-hide via the appbar API
        APPBARDATA abd = { .cbSize = sizeof (abd), .hWnd = taskbar };
        abd.lParam = ABS_AUTOHIDE;
        SHAppBarMessage (ABM_SETSTATE, &abd);

        // 2. Force the taskbar to re-evaluate its position by poking its
        //    window placement. SetWindowPos with NOMOVE|NOSIZE triggers
        //    the shell to honour the newly set auto-hide flag.
        SetWindowPos (taskbar, HWND_BOTTOM, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        // 3. Broadcast a settings-change notification so Explorer
        //    re-reads the taskbar state immediately.
        SendNotifyMessageA (HWND_BROADCAST, WM_SETTINGCHANGE,
                            SPI_SETWORKAREA, 0);
    }

    // Handle multi-monitor secondary taskbars (Windows 10/11)
    // Note: Secondary taskbars don't reliably support the AppBar API auto-hide
    // toggle, so we keep them hidden for now to ensure a clean maximized
    // experience.
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
        // 1. Restore always-on-top via appbar API
        APPBARDATA abd = { .cbSize = sizeof (abd), .hWnd = taskbar };
        abd.lParam = ABS_ALWAYSONTOP;
        SHAppBarMessage (ABM_SETSTATE, &abd);

        // 2. Ensure the taskbar is visible
        ShowWindow (taskbar, SW_SHOW);

        // 3. Bring it back on top so it's visible above normal windows
        SetWindowPos (taskbar, HWND_TOPMOST, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);

        // 4. Broadcast settings-change so Explorer refreshes the work area
        //    and the taskbar redraws at its normal position.
        SendNotifyMessageA (HWND_BROADCAST, WM_SETTINGCHANGE,
                            SPI_SETWORKAREA, 0);
    }

    // Restore secondary taskbars
    HWND secondary = NULL;
    while ((secondary = FindWindowExA (NULL, secondary, "Shell_SecondaryTrayWnd", NULL)))
    {
        ShowWindow (secondary, SW_SHOW);
    }
}
