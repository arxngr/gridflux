#ifndef GF_CORE_TYPES_H
#define GF_CORE_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// Cross-platform types
typedef uint64_t gf_window_id_t;
typedef int32_t gf_workspace_id_t;
typedef int32_t gf_coordinate_t;
typedef uint32_t gf_dimension_t;

// Platform-specific defines with proper guards
#ifdef __linux__
#include <X11/Xlib.h>
typedef Window gf_native_window_t;
typedef Display *gf_display_t;
#ifndef GF_SESSION_TYPE
#define GF_SESSION_TYPE "x11"
#endif
#elif _WIN32
#include <windows.h>
typedef HWND gf_native_window_t;
typedef HDC gf_display_t;
#ifndef GF_PLATFORM_WIN32
#define GF_PLATFORM_WIN32
#endif
#ifndef GF_SESSION_TYPE
#define GF_SESSION_TYPE "win32"
#endif
#elif __APPLE__
typedef void *gf_native_window_t;
typedef void *gf_display_t;
#ifndef GF_PLATFORM_MACOS
#define GF_PLATFORM_MACOS
#endif
#ifndef GF_SESSION_TYPE
#define GF_SESSION_TYPE "macos"
#endif
#endif

// Constants
#define GF_MAX_WINDOWS_PER_WORKSPACE 3
#define GF_MAX_WORKSPACES 32
#define GF_DEFAULT_PADDING 1
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
    GF_ERROR_INITIALIZATION_FAILED = -6
} gf_error_code_t;

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
    gf_window_id_t id;
    gf_native_window_t native_handle;
    gf_workspace_id_t workspace_id;
    gf_rect_t geometry;
    bool is_maximized;
    bool is_minimized;
    bool needs_update;
    bool is_valid;
    time_t last_modified;
} gf_window_info_t;

// Workspace information
typedef struct
{
    gf_workspace_id_t id;
    uint32_t window_count;
    uint32_t max_windows;
    int32_t available_space;
    bool is_locked;
} gf_workspace_info_t;

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
} gf_geometry_flags_t;

#endif // GF_CORE_TYPES_H
