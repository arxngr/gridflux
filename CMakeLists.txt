set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.12)
project(gridflux VERSION 2.0.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -Wall -Wextra -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# Include directories
include_directories(include)

# Common source files
set(COMMON_SOURCES
    src/core/logger.c
    src/core/geometry.c
    src/core/workspace.c
    src/core/window_manager.c
    src/utils/memory.c
    src/utils/collections.c
    src/main.c
)

# Platform-specific sources
if(UNIX AND NOT APPLE)
    # Linux/X11
    find_package(X11 REQUIRED)
    add_definitions(-DGF_PLATFORM_X11)
    
    set(PLATFORM_SOURCES
        src/platform/x11/x11_atoms.c
        src/platform/x11/x11_window_manager.c
    )
    
    set(PLATFORM_LIBRARIES ${X11_LIBRARIES})
    set(PLATFORM_INCLUDES ${X11_INCLUDE_DIR})
    
elseif(WIN32)
    # Windows
    add_definitions(-DGF_PLATFORM_WIN32)
    set(PLATFORM_SOURCES
        src/platform/win32/win32_window_manager.c
    )
    set(PLATFORM_LIBRARIES user32 gdi32 kernel32)
    
elseif(APPLE)
    # macOS
    add_definitions(-DGF_PLATFORM_MACOS)
    set(PLATFORM_SOURCES
        src/platform/macos/macos_window_manager.c
    )
    find_library(COCOA_FRAMEWORK Cocoa)
    set(PLATFORM_LIBRARIES ${COCOA_FRAMEWORK})
endif()

# Create executable
add_executable(gridflux 
    ${COMMON_SOURCES}
    ${PLATFORM_SOURCES}
)

# Link libraries
target_link_libraries(gridflux ${PLATFORM_LIBRARIES})

# Include directories
if(PLATFORM_INCLUDES)
    target_include_directories(gridflux PRIVATE ${PLATFORM_INCLUDES})
endif()

# Install
install(TARGETS gridflux DESTINATION bin)

set(CPACK_PACKAGE_NAME "GridFlux")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION "Cross-platform tiling window manager")
set(CPACK_PACKAGE_CONTACT "GridFlux Team")

if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB;RPM")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libx11-6")
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
endif()

include(CPack)
