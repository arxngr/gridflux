#ifndef GF_CORE_TYPES_H
#define GF_CORE_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// Cross-platform types
typedef int32_t gf_ws_id_t;
typedef int32_t gf_coordinate_t;
typedef uint32_t gf_dimension_t;
typedef uint32_t gf_color_t;

// Platform-specific defines with proper guards
#ifdef __linux__
#include <X11/Xlib.h>
typedef Window gf_handle_t;
typedef Display *gf_display_t;
#ifndef GF_SESSION_TYPE
#define GF_SESSION_TYPE "x11"
#endif
#elif _WIN32
#include <windows.h>
typedef HWND gf_handle_t;
typedef HDC gf_display_t;
#ifndef GF_PLATFORM_WIN32
#define GF_PLATFORM_WIN32
#endif
#ifndef GF_SESSION_TYPE
#define GF_SESSION_TYPE "win32"
#endif
#elif __APPLE__
typedef void *gf_handle_t;
typedef void *gf_display_t;
#ifndef GF_PLATFORM_MACOS
#define GF_PLATFORM_MACOS
#endif
#ifndef GF_SESSION_TYPE
#define GF_SESSION_TYPE "macos"
#endif
#endif

// Callbacks
typedef void (*gf_window_destroy_callback_t) (gf_handle_t window, void *user_data);

// Constants
#define GF_MAX_WINDOWS_PER_WORKSPACE 10
#define GF_MAX_WORKSPACES 32
#define GF_FIRST_WORKSPACE_ID 1
#define GF_DEFAULT_PADDING 10
#define GF_MIN_WINDOW_SIZE 10

// Error codes
typedef enum
{
    GF_SUCCESS = 0,
    GF_ERROR_MEMORY_ALLOCATION = -1,
    GF_ERROR_INVALID_PARAMETER = -2,
    GF_ERROR_PLATFORM_ERROR = -3,
    GF_ERROR_WINDOW_NOT_FOUND = -4,
    GF_ERROR_DISPLAY_CONNECTION = -5,
    GF_ERROR_INITIALIZATION_FAILED = -6,
    GF_ERROR_WORKSPACE_LOCKED = -7,
    GF_ERROR_WORKSPACE_FULL = -8,
    GF_ERROR_ALREADY_LOCKED = -9,
    GF_ERROR_ALREADY_UNLOCKED = -10
} gf_err_t;

typedef enum
{
    GF_LOG_ERROR = 0,
    GF_LOG_WARN = 1,
    GF_LOG_INFO = 2,
    GF_LOG_DEBUG = 3
} gf_log_level_t;

// Geometry structure
typedef struct
{
    gf_coordinate_t x, y;
    gf_dimension_t width, height;
} gf_rect_t;

typedef struct
{
    gf_handle_t id;
    gf_ws_id_t workspace_id;
    gf_rect_t geometry;
    bool is_maximized;
    bool is_minimized;
    bool needs_update;
    bool is_valid;
    time_t last_modified;
    char name[256];
} gf_win_info_t;

typedef struct
{
    gf_handle_t target;  // The window we’re tracking
    gf_handle_t overlay; // The overlay border window
    gf_color_t color;
    int thickness;
#if defined(_WIN32)
    RECT last_rect;
#elif defined(__linux__)
    gf_rect_t last_rect;
    int last_intersect_count;
    XRectangle last_intersections[16];
#endif
} gf_border_t;

// Workspace information
typedef struct
{
    gf_ws_id_t id;
    uint32_t window_count;
    uint32_t max_windows;
    int32_t available_space;
    bool is_locked;
    bool has_maximized_state;
} gf_ws_info_t;

// Geometry flags
typedef enum
{
    GF_GEOMETRY_CHANGE_X = (1 << 0),
    GF_GEOMETRY_CHANGE_Y = (1 << 1),
    GF_GEOMETRY_CHANGE_WIDTH = (1 << 2),
    GF_GEOMETRY_CHANGE_HEIGHT = (1 << 3),
    GF_GEOMETRY_APPLY_PADDING = (1 << 4),
    GF_GEOMETRY_CHANGE_ALL = (GF_GEOMETRY_CHANGE_X | GF_GEOMETRY_CHANGE_Y
                              | GF_GEOMETRY_CHANGE_WIDTH | GF_GEOMETRY_CHANGE_HEIGHT)
} gf_geom_flags_t;

#endif // GF_CORE_TYPES_H
