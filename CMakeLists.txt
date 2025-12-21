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
    
    # Check for D-Bus (for KWin backend)
    pkg_check_modules(DBUS dbus-1)
    
    add_definitions(-DGF_PLATFORM_X11)
    
    # Collect X11 platform sources
    file(GLOB_RECURSE PLATFORM_SOURCES
        "${CMAKE_SOURCE_DIR}/src/platform/x11/*.c"
        "${CMAKE_SOURCE_DIR}/src/platform/platform_env.c"
    )
    
    # Add KWin backend if D-Bus is available
    if(DBUS_FOUND)
        message(STATUS "D-Bus found - KWin backend will be available")
        file(GLOB_RECURSE KWIN_SOURCES
            "${CMAKE_SOURCE_DIR}/src/platform/kwin/*.c"
        )
        list(APPEND PLATFORM_SOURCES ${KWIN_SOURCES})
        add_definitions(-DGF_KWIN_SUPPORT)
        
        set(PLATFORM_LIBRARIES 
            ${X11_LIBRARIES} 
            ${DBUS_LIBRARIES}
            m 
            ${JSONC_LIBRARIES}
        )
        set(PLATFORM_INCLUDES 
            ${X11_INCLUDE_DIR}
            ${DBUS_INCLUDE_DIRS}
        )
        
        # Install KWin QML script
        install(FILES 
            "${CMAKE_SOURCE_DIR}/src/platform/kwin/kwin_tiler.qml"
            DESTINATION share/gridflux
            PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
        )
        
        # Package dependencies (with D-Bus)
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "libx11-6, libjson-c5, libdbus-1-3")
        set(CPACK_RPM_PACKAGE_REQUIRES "libX11, json-c, dbus-libs")
    else()
        message(STATUS "D-Bus not found - KWin backend will NOT be available")
        set(PLATFORM_LIBRARIES ${X11_LIBRARIES} m ${JSONC_LIBRARIES})
        set(PLATFORM_INCLUDES ${X11_INCLUDE_DIR})
        
        # Package dependencies (without D-Bus)
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "libx11-6, libjson-c5")
        set(CPACK_RPM_PACKAGE_REQUIRES "libX11, json-c")
    endif()
    
    set(CPACK_GENERATOR "DEB;RPM")
    
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

# Link D-Bus directories if available
if(DBUS_FOUND AND DBUS_LIBRARY_DIRS)
    target_link_directories(gridflux PRIVATE ${DBUS_LIBRARY_DIRS})
endif()

# Install binary
install(TARGETS gridflux DESTINATION bin)

# Install config file if exists
if(EXISTS "${CMAKE_SOURCE_DIR}/config.json")
    install(FILES 
        "${CMAKE_SOURCE_DIR}/config.json"
        DESTINATION share/gridflux
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
    )
endif()

# CPack configuration
set(CPACK_PACKAGE_NAME "GridFlux")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION "Cross-platform tiling window manager with KDE/KWin support")
set(CPACK_PACKAGE_CONTACT "GridFlux Team")

# Add post-install script for KWin
if(DBUS_FOUND)
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA 
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/postinst"
    )
endif()

include(CPack)

# Print build summary
message(STATUS "")
message(STATUS "GridFlux Build Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
if(UNIX AND NOT APPLE)
    message(STATUS "  X11 support: YES")
    if(DBUS_FOUND)
        message(STATUS "  KWin backend: YES")
        message(STATUS "  D-Bus version: ${DBUS_VERSION}")
    else()
        message(STATUS "  KWin backend: NO (D-Bus not found)")
    endif()
endif()
message(STATUS "")
