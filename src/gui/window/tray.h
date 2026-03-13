#ifndef GF_GUI_TRAY_H
#define GF_GUI_TRAY_H

#include "../app_state.h"

#ifdef _WIN32

// Initialize the system tray icon.
// Creates a tray icon with context menu for Start/Stop/Show/Quit.
// Must be called after the main window is created.
void gf_gui_tray_init (gf_app_state_t *app);

// Destroy the system tray icon and clean up resources.
void gf_gui_tray_destroy (gf_app_state_t *app);

// Update the tray tooltip to reflect current server status.
void gf_gui_tray_update_status (gf_app_state_t *app, gboolean server_running);

#endif // _WIN32

#endif // GF_GUI_TRAY_H
