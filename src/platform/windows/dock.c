#include "internal.h"

void
gf_dock_hide (gf_platform_t *platform)
{
    (void)platform;
    APPBARDATA abd = { .cbSize = sizeof (abd) };
    abd.hWnd = FindWindowA ("Shell_TrayWnd", NULL);
    if (abd.hWnd)
    {
        abd.lParam = ABS_AUTOHIDE;
        SHAppBarMessage (ABM_SETSTATE, &abd);
    }
}

void
gf_dock_restore (gf_platform_t *platform)
{
    (void)platform;
    APPBARDATA abd = { .cbSize = sizeof (abd) };
    abd.hWnd = FindWindowA ("Shell_TrayWnd", NULL);
    if (abd.hWnd)
    {
        abd.lParam = ABS_ALWAYSONTOP;
        SHAppBarMessage (ABM_SETSTATE, &abd);
    }
}
