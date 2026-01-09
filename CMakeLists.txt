cmake_minimum_required(VERSION 3.12)
project(gridflux VERSION 2.0.0 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(GF_DEV_MODE "Enable development mode (read config from current dir)" OFF)

# Windows MSVC configuration
if(WIN32)
    message(STATUS "Configuring build for Windows (MSVC)")
   
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)

    find_package(json-c CONFIG)
    if(NOT json-c_FOUND)
        if(NOT DEFINED ENV{VCPKG_ROOT})
            message(FATAL_ERROR "JSON-C not found and VCPKG_ROOT is not set. Set VCPKG_ROOT or install JSON-C manually.")
        endif()
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "Vcpkg toolchain file")
        find_package(json-c CONFIG REQUIRED)
    endif()
endif()

include_directories(src)

file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS "src/*.c")
list(REMOVE_ITEM COMMON_SOURCES "${CMAKE_SOURCE_DIR}/src/client.c")
list(REMOVE_ITEM COMMON_SOURCES "${CMAKE_SOURCE_DIR}/src/gui.c")

# GUI: GTK4
if(WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK4 gtk4)
    
    if(GTK4_FOUND)
        add_executable(gridflux-gui
            src/gui.c
            src/ipc_command.c
            src/window_manager.c
            src/list.c
            src/memory.c
            src/config.c
            src/logger.c
            src/layout.c
            src/file.c
            src/platform/windows/ipc.c
        )

        target_include_directories(gridflux-gui PRIVATE ${GTK4_INCLUDE_DIRS})
        target_link_libraries(gridflux-gui
            PRIVATE
                ${GTK4_LIBRARIES}
                json-c::json-c
        )

        if(NOT GF_DEV_MODE)
            install(TARGETS gridflux-gui DESTINATION bin)
        endif()
    else()
        message(WARNING "GTK4 not found. GUI will not be built on Windows.")
    endif()
elseif(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK4 gtk4)
    
    if(GTK4_FOUND)
        add_executable(gridflux-gui
            src/gui.c
            src/ipc_command.c
            src/window_manager.c
            src/list.c
            src/memory.c
            src/config.c
            src/logger.c
            src/layout.c
            src/file.c
        )

        target_include_directories(gridflux-gui PRIVATE ${GTK4_INCLUDE_DIRS})
        target_link_libraries(gridflux-gui PRIVATE ${GTK4_LIBRARIES})

        if(NOT GF_DEV_MODE)
            install(TARGETS gridflux-gui DESTINATION bin)
        endif()
    else()
        message(WARNING "GTK4 not found. GUI will not be built.")
    endif()
endif()

# Platform-specific sources
if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    pkg_check_modules(DBUS dbus-1)
    pkg_check_modules(JSONC REQUIRED json-c)
    
    file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/platform/unix/*.c"
        "${CMAKE_SOURCE_DIR}/src/platform/platform_env.c"
    )

    add_compile_definitions(_GNU_SOURCE)

    if(DBUS_FOUND)
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
            ${JSONC_INCLUDE_DIRS}
        )

        if(NOT GF_DEV_MODE)
            install(FILES 
                "${CMAKE_SOURCE_DIR}/src/platform/unix/kwin/main.qml"
                DESTINATION share/gridflux/kwin
                PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
            )
        endif()

        set(CPACK_DEBIAN_PACKAGE_DEPENDS "libx11-6, libjson-c5, libdbus-1-3")
        set(CPACK_RPM_PACKAGE_REQUIRES "libX11, json-c, dbus-libs")
    else()
        set(PLATFORM_LIBRARIES ${X11_LIBRARIES} m ${JSONC_LIBRARIES})
        set(PLATFORM_INCLUDES ${X11_INCLUDE_DIR} ${JSONC_INCLUDE_DIRS})

        set(CPACK_DEBIAN_PACKAGE_DEPENDS "libx11-6, libjson-c5")
        set(CPACK_RPM_PACKAGE_REQUIRES "libX11, json-c")
    endif()
    
    set(CPACK_GENERATOR "DEB;RPM")

    set(GF_GNOME_EXT_UUID "gridflux@gridflux.dev")
    set(GF_GNOME_EXT_DIR "$ENV{HOME}/.local/share/gnome-shell/extensions/${GF_GNOME_EXT_UUID}")

    add_custom_target(gnome_shell_extension ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GF_GNOME_EXT_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/src/platform/unix/mutter
                ${GF_GNOME_EXT_DIR}
        COMMENT "Installing GNOME Shell extension"
    )
elseif(WIN32)
    file(GLOB PLATFORM_SOURCES "src/platform/windows/*.c")
    set(PLATFORM_LIBRARIES user32 gdi32 kernel32 shell32 dwmapi)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_PACKAGE_DESCRIPTION "Cross-platform tiling window manager for Windows")
elseif(APPLE)
    pkg_check_modules(JSONC REQUIRED json-c)
    file(GLOB PLATFORM_SOURCES "src/platform/macos/*.c")
    find_library(COCOA_FRAMEWORK Cocoa)
    set(PLATFORM_LIBRARIES ${COCOA_FRAMEWORK} ${JSONC_LIBRARIES})
    set(PLATFORM_INCLUDES ${JSONC_INCLUDE_DIRS})
    set(CPACK_GENERATOR "DragNDrop")
endif()

# Main executables
add_executable(gridflux 
    ${COMMON_SOURCES}
    ${PLATFORM_SOURCES}
)

add_executable(gridflux-cli
    src/client.c
    src/ipc_command.c
    src/window_manager.c
    src/list.c
    src/memory.c
    src/logger.c
    src/config.c
    src/layout.c
    src/file.c
)

if(UNIX AND NOT APPLE)
    target_sources(gridflux-cli PRIVATE src/platform/unix/ipc.c)
elseif(WIN32)
    target_sources(gridflux-cli PRIVATE src/platform/windows/ipc.c)
endif()

# Development mode
if(GF_DEV_MODE)
    target_compile_definitions(gridflux PRIVATE GF_DEV_MODE)
    target_compile_definitions(gridflux-cli PRIVATE GF_DEV_MODE)
    if(TARGET gridflux-gui)
        target_compile_definitions(gridflux-gui PRIVATE GF_DEV_MODE)
    endif()
endif()

# Link libraries (keyword signature only)
target_link_libraries(gridflux
    PRIVATE
        ${PLATFORM_LIBRARIES}
        $<$<BOOL:WIN32>:json-c::json-c>
)
target_link_libraries(gridflux-cli
    PRIVATE
        ${PLATFORM_LIBRARIES}
        $<$<BOOL:WIN32>:json-c::json-c>
)
if(TARGET gridflux-gui)
    target_link_libraries(gridflux-gui
        PRIVATE
            $<$<BOOL:WIN32>:json-c::json-c>
            $<$<BOOL:WIN32>:${GTK4_LIBRARIES}>
    )
endif()

if(PLATFORM_INCLUDES)
    target_include_directories(gridflux PRIVATE ${PLATFORM_INCLUDES})
    target_include_directories(gridflux-cli PRIVATE ${PLATFORM_INCLUDES})
endif()

# Optional library directories
if(JSONC_LIBRARY_DIRS)
    target_link_directories(gridflux PRIVATE ${JSONC_LIBRARY_DIRS})
    target_link_directories(gridflux-cli PRIVATE ${JSONC_LIBRARY_DIRS})
endif()
if(DBUS_FOUND AND DBUS_LIBRARY_DIRS)
    target_link_directories(gridflux PRIVATE ${DBUS_LIBRARY_DIRS})
endif()

# Install
if(NOT GF_DEV_MODE)
    install(TARGETS gridflux gridflux-cli DESTINATION bin)
    
    if(EXISTS "${CMAKE_SOURCE_DIR}/config.json")
        install(FILES 
            "${CMAKE_SOURCE_DIR}/config.json"
            DESTINATION share/gridflux/examples
            RENAME config.json.example
            PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
        )
    endif()
    
    set(CPACK_PACKAGE_NAME "GridFlux")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_DESCRIPTION "Cross-platform tiling window manager with KDE/KWin support")
    set(CPACK_PACKAGE_CONTACT "GridFlux Team")
    
    if(DBUS_FOUND)
        set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/scripts/postinst")
    endif()
    
    include(CPack)
endif()

# Status output
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

if(WIN32)
    message(STATUS "  Windows support: YES")
    message(STATUS "  Compiler: MSVC")
    if(TARGET gridflux-gui)
        message(STATUS "  GUI support: YES (GTK4)")
    else()
        message(STATUS "  GUI support: NO")
    endif()
    message(STATUS "  Libraries: user32, gdi32, kernel32, shell32, dwmapi, json-c")
endif()

if(APPLE)
    message(STATUS "  macOS support: YES")
    message(STATUS "  Framework: Cocoa")
endif()

if(GF_DEV_MODE)
    message(STATUS "")
    message(STATUS "  ** DEVELOPMENT MODE ENABLED **")
    message(STATUS "  Config will be read from: ./config.json")
    message(STATUS "  Installation targets are disabled")
    message(STATUS "")
else()
    message(STATUS "")
    if(UNIX AND NOT APPLE)
        message(STATUS "  Config will be read from: ~/.config/gridflux/config.json")
    elseif(WIN32)
        message(STATUS "  Config will be read from: %%APPDATA%%\\gridflux\\config.json")
    elseif(APPLE)
        message(STATUS "  Config will be read from: ~/Library/Application Support/gridflux/config.json")
    endif()
    message(STATUS "")
endif()
message(STATUS "")
