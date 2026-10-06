#ifndef GF_WINDOWS_TASKBAR_RECOVERY_H
#define GF_WINDOWS_TASKBAR_RECOVERY_H

#include "maximized_bounds.h"
#include <windows.h>

// The marker survives the manager process so Stop/Exit and the launcher can
// restore only bars GridFlux managed, including after forced termination.
#define GF_TASKBAR_HIDDEN_PROP "GridFlux.ManagedTaskbar"

static inline void
gf_taskbar_restore_window (HWND window)
{
    if (window && GetPropA (window, GF_TASKBAR_HIDDEN_PROP))
    {
        if (!IsWindowVisible (window))
            ShowWindow (window, SW_SHOWNOACTIVATE);
        RemovePropA (window, GF_TASKBAR_HIDDEN_PROP);
    }
}

static inline void
gf_taskbar_restore_all (void)
{
    gf_taskbar_restore_window (FindWindowA ("Shell_TrayWnd", NULL));
    HWND window = NULL;
    while ((window = FindWindowExA (NULL, window, "Shell_SecondaryTrayWnd", NULL)))
        gf_taskbar_restore_window (window);
    gf_maximized_restore_all ();
}

#endif
