#ifndef GF_GUI_PROCESS_MANAGER_H
#define GF_GUI_PROCESS_MANAGER_H

#include <stdbool.h>

// Check if the gridflux server process is currently running.
bool gf_server_is_running (void);

// Start the gridflux server process.
// Looks for the gridflux executable in the same directory as the GUI binary.
// Returns true on success, false on failure.
bool gf_server_start (void);

// Stop the gridflux server process.
// On Win32: finds PID via snapshot and terminates.
// On Unix: finds PID via /proc and sends SIGTERM.
// Returns true on success, false if not running or failed.
bool gf_server_stop (void);

#endif // GF_GUI_PROCESS_MANAGER_H
