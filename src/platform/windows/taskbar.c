#include "taskbar.h"
#include "window_state.h"

void
gf_taskbar_restore_window (HWND window)
{
    if (window && GetPropA (window, GF_TASKBAR_HIDDEN_PROP))
    {
        if (!IsWindowVisible (window))
            ShowWindow (window, SW_SHOWNOACTIVATE);
        RemovePropA (window, GF_TASKBAR_HIDDEN_PROP);
    }
}

void
gf_taskbar_restore_all (void)
{
    gf_taskbar_restore_window (FindWindowA ("Shell_TrayWnd", NULL));
    HWND window = NULL;
    while ((window = FindWindowExA (NULL, window, "Shell_SecondaryTrayWnd", NULL)))
        gf_taskbar_restore_window (window);
    gf_window_state_restore ();
}
