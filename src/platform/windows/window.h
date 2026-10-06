#ifndef GF_WINDOWS_WINDOW_H
#define GF_WINDOWS_WINDOW_H

#include <windows.h>

#define GF_WINDOW_STATE_FILL_PROP "GridFlux.ExpandedMaximized"
#define GF_WINDOW_STATE_DPI_PROP "GridFlux.MaximizedDpi"
#define GF_WINDOW_STATE_REGION_PROP "GridFlux.MaximizedRegion"

void gf_window_state_reset (HWND window);
BOOL gf_window_state_apply (HWND window, BOOL fill_monitor);
void gf_window_state_restore (void);

#endif