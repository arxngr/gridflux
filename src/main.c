#include "core/config.h"
#include "core/geometry.h"
#include "core/logger.h"
#include "core/window_manager.h"
#include "platform/platform.h"
#include "utils/memory.h"
#ifdef __linux__
#include "platform/linux/platform.h"
#endif
#include <signal.h>
#include <stdlib.h>
#include <string.h>

static gf_window_manager_t *g_manager = NULL;

static void
signal_handler (int sig)
{
    GF_LOG_INFO ("Received signal %d, shutting down...", sig);
    if (g_manager)
    {
        gf_window_manager_cleanup (g_manager);
        gf_window_manager_destroy (g_manager);
        g_manager = NULL;
    }
    exit (0);
}

int
main ()
{
    gf_log_init (GF_LOG_DEBUG);
    signal (SIGINT, signal_handler);
    signal (SIGTERM, signal_handler);

    gf_platform_interface_t *platform = NULL;
    gf_geometry_calculator_t *geometry_calc = NULL;
    gf_config_t *config = NULL;

    GF_LOG_INFO ("Starting GridFlux Window Manager v2.0");

    // Load configuration first
    config = gf_malloc (sizeof (gf_config_t));
    if (!config)
    {
        GF_LOG_ERROR ("Failed to allocate config");
        return 1;
    }

    *config = load_or_create_config (GLOB_CFG);
    GF_LOG_INFO ("Configuration loaded:");
    GF_LOG_INFO ("  max_windows_per_workspace: %u", config->max_windows_per_workspace);
    GF_LOG_INFO ("  max_workspaces: %u", config->max_workspaces);
    GF_LOG_INFO ("  default_padding: %u", config->default_padding);
    GF_LOG_INFO ("  min_window_size: %u", config->min_window_size);

#ifdef __linux__
    char *session_type = getenv ("XDG_SESSION_TYPE");
    if (!session_type || strcmp (session_type, "x11") != 0)
    {
        GF_LOG_ERROR ("X11 session required, found: %s",
                      session_type ? session_type : "none");
        gf_free (config);
        return 1;
    }
#endif

    platform = gf_platform_create ();

    if (!platform)
    {
        GF_LOG_ERROR ("Failed to create platform interface");
        gf_free (config);
        return 1;
    }

    geometry_calc = gf_bsp_geometry_calculator_create (config);
    if (!geometry_calc)
    {
        GF_LOG_ERROR ("Failed to create geometry calculator");
        goto cleanup;
    }

    gf_error_code_t result
        = gf_window_manager_create (&g_manager, platform, geometry_calc);
    if (result != GF_SUCCESS)
    {
        GF_LOG_ERROR ("Failed to create window manager: %d", result);
        goto cleanup;
    }

    g_manager->config = config;
    GF_LOG_INFO ("Config assigned to window manager");

    result = gf_window_manager_init (g_manager);
    if (result != GF_SUCCESS)
    {
        GF_LOG_ERROR ("Failed to initialize window manager: %d", result);
        goto cleanup;
    }

    GF_LOG_INFO ("Window manager initialized, entering main loop");
    result = gf_window_manager_run (g_manager);

cleanup:
    if (g_manager)
    {
        gf_window_manager_cleanup (g_manager);
        gf_window_manager_destroy (g_manager);
    }
    else if (config)
    {
        gf_free (config);
    }

    if (geometry_calc)
    {
        gf_bsp_geometry_calculator_destroy (geometry_calc);
    }

#ifdef __linux__
    if (platform)
    {
        gf_platform_destroy (platform);
    }
#endif

    GF_LOG_INFO ("GridFlux Window Manager shutdown complete");
    return (result == GF_SUCCESS) ? 0 : 1;
}
