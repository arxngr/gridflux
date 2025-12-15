set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.12)
project(gridflux VERSION 2.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS_DEBUG "-g -O0 -Wall -Wextra -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONC REQUIRED json-c)

include_directories(src ${JSONC_INCLUDE_DIRS})

# Automatically collect common source files
file(GLOB_RECURSE COMMON_SOURCES
    "src/core/*.c"
    "src/utils/*.c"
    "src/main.c"
)

# Platform-specific configuration
if(UNIX AND NOT APPLE)
    # Linux/X11
    find_package(X11 REQUIRED)
    add_definitions(-DGF_PLATFORM_X11)
    
    file(GLOB_RECURSE PLATFORM_SOURCES
        "${CMAKE_SOURCE_DIR}/src/platform/x11/*.c"
        "${CMAKE_SOURCE_DIR}/src/platform/platform_env.c"   # ← ADD THIS
    )

    set(PLATFORM_LIBRARIES ${X11_LIBRARIES} m ${JSONC_LIBRARIES})
    set(PLATFORM_INCLUDES ${X11_INCLUDE_DIR})
    
    # Package dependencies
    set(CPACK_GENERATOR "DEB;RPM")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libx11-6, libjson-c5")
    set(CPACK_RPM_PACKAGE_REQUIRES "libX11, json-c")
    
elseif(WIN32)
    # Windows
    add_definitions(-DGF_PLATFORM_WIN32)
    file(GLOB PLATFORM_SOURCES "src/platform/win32/*.c")
    set(PLATFORM_LIBRARIES user32 gdi32 kernel32 ${JSONC_LIBRARIES})
    
    set(CPACK_GENERATOR "NSIS;ZIP")
    
elseif(APPLE)
    # macOS
    add_definitions(-DGF_PLATFORM_MACOS)
    file(GLOB PLATFORM_SOURCES "src/platform/macos/*.c")
    find_library(COCOA_FRAMEWORK Cocoa)
    set(PLATFORM_LIBRARIES ${COCOA_FRAMEWORK} ${JSONC_LIBRARIES})
    
    set(CPACK_GENERATOR "DragNDrop")
endif()

# Create executable
add_executable(gridflux 
    ${COMMON_SOURCES}
    ${PLATFORM_SOURCES}
)

# Link libraries
target_link_libraries(gridflux ${PLATFORM_LIBRARIES})

if(PLATFORM_INCLUDES)
    target_include_directories(gridflux PRIVATE ${PLATFORM_INCLUDES})
endif()

if(JSONC_LIBRARY_DIRS)
    target_link_directories(gridflux PRIVATE ${JSONC_LIBRARY_DIRS})
endif()

install(TARGETS gridflux DESTINATION bin)

set(CPACK_PACKAGE_NAME "GridFlux")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION "Cross-platform tiling window manager")
set(CPACK_PACKAGE_CONTACT "GridFlux Team")

include(CPack)
