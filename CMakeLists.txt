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

# Development mode option
option(GF_DEV_MODE "Enable development mode (read config from current dir)" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONC REQUIRED json-c)

include_directories(src ${JSONC_INCLUDE_DIRS})

# Automatically collect common source files
file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS
    "src/*.c"
    "src/main.c"
)

# Platform-specific configuration
if(UNIX AND NOT APPLE)
    # Linux
    find_package(X11 REQUIRED)
    
    # Check for D-Bus (for KWin backend)
    pkg_check_modules(DBUS dbus-1)
    
    # Collect Unix platform sources
    file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/platform/unix/*.c"
        "${CMAKE_SOURCE_DIR}/src/platform/platform_env.c"
    )
    
    # Add KWin backend if D-Bus is available
    if(DBUS_FOUND)
        message(STATUS "D-Bus found - KWin backend will be available")
        file(GLOB_RECURSE KWIN_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/src/platform/unix/kwin/*.c"
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
        
        # Install KWin QML script (only in production mode)
        if(NOT GF_DEV_MODE)
            install(FILES 
                "${CMAKE_SOURCE_DIR}/src/platform/unix/kwin/main.qml"
                DESTINATION share/gridflux/kwin
                PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
            )
        endif()
        
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

    set(GF_GNOME_EXT_UUID "gridflux@gridflux.dev")
    set(GF_GNOME_EXT_DIR
        "$ENV{HOME}/.local/share/gnome-shell/extensions/${GF_GNOME_EXT_UUID}"
    )

    add_custom_target(gnome_shell_extension ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GF_GNOME_EXT_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/src/platform/unix/mutter
                ${GF_GNOME_EXT_DIR}
        COMMENT "Installing GNOME Shell extension"
    )

elseif(WIN32)
    # Windows
    file(GLOB PLATFORM_SOURCES "src/platform/windows/*.c")
    set(PLATFORM_LIBRARIES user32 gdi32 kernel32 ${JSONC_LIBRARIES})
    
    set(CPACK_GENERATOR "NSIS;ZIP")
    
elseif(APPLE)
    # macOS
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

# Add dev mode definition if enabled
if(GF_DEV_MODE)
    target_compile_definitions(gridflux PRIVATE GF_DEV_MODE)
endif()

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

# Install binary (only in production mode)
if(NOT GF_DEV_MODE)
    install(TARGETS gridflux DESTINATION bin)
    
    # Install example config file
    if(EXISTS "${CMAKE_SOURCE_DIR}/config.json")
        install(FILES 
            "${CMAKE_SOURCE_DIR}/config.json"
            DESTINATION share/gridflux/examples
            RENAME config.json.example
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
endif()

# Print build summary
message(STATUS "")
message(STATUS "GridFlux Build Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Development mode: ${GF_DEV_MODE}")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
if(UNIX AND NOT APPLE)
    message(STATUS "  Unix support: YES")
    if(DBUS_FOUND)
        message(STATUS "  KWin backend: YES")
        message(STATUS "  D-Bus version: ${DBUS_VERSION}")
    else()
        message(STATUS "  KWin backend: NO (D-Bus not found)")
    endif()
endif()
if(GF_DEV_MODE)
    message(STATUS "")
    message(STATUS "  ** DEVELOPMENT MODE ENABLED **")
    message(STATUS "  Config will be read from: ./config.json")
    message(STATUS "  Installation targets are disabled")
    message(STATUS "")
else()
    message(STATUS "")
    message(STATUS "  Config will be read from: ~/.config/gridflux/config.json")
    message(STATUS "")
endif()
message(STATUS "")
