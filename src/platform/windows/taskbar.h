#ifndef GF_WINDOWS_TASKBAR_H
#define GF_WINDOWS_TASKBAR_H

#include <windows.h>

// Survives the manager process so Stop/Exit and the launcher can recover bars.
#define GF_TASKBAR_HIDDEN_PROP "GridFlux.ManagedTaskbar"

void gf_taskbar_restore_window (HWND window);
void gf_taskbar_restore_all (void);

#endif