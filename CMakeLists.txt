cmake_minimum_required(VERSION 3.12)
project(gridflux VERSION 1.0.0 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

option(GF_DEV_MODE "Enable development mode (read config from current dir)" OFF)
option(GF_ENABLE_SANITIZERS "Enable address/undefined-behavior checks for validation builds" OFF)

if(MSVC)
    add_compile_options(/GS /guard:cf)
    add_link_options(/guard:cf /DYNAMICBASE /NXCOMPAT)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_link_options(/HIGHENTROPYVA)
    endif()
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fstack-protector-strong)
    if(MINGW)
        # Resolve stack-check support statically; installers need no new CRT DLL.
        link_libraries(-Wl,-Bstatic ssp -Wl,-Bdynamic)
        add_link_options(-Wl,--dynamicbase,--nxcompat)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            add_link_options(-Wl,--high-entropy-va)
        endif()
    elseif(UNIX AND NOT APPLE)
        add_compile_definitions($<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:_FORTIFY_SOURCE=2>)
    endif()
endif()
if(GF_ENABLE_SANITIZERS)
    if(MSVC)
        add_compile_options(/fsanitize=address /Zi)
        add_link_options(/INCREMENTAL:NO /DEBUG)
    elseif(UNIX AND NOT APPLE)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "Sanitizer validation requires MSVC or a Linux GNU/Clang toolchain")
    endif()
endif()

# Packaging configuration
set(CPACK_PACKAGE_NAME "gridflux")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION "Virtual workspace & Window management tool for arranging and managing windows")
set(CPACK_PACKAGE_VENDOR "GridFlux Project")
set(CPACK_PACKAGE_CONTACT "0x4rd1@gmail.com")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/arxngr/gridflux")

if(UNIX AND NOT WIN32)
    set(CPACK_SET_DESTDIR TRUE)
endif()

include_directories(src)

file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS
    "src/core/*.c"
    "src/utils/*.c"
    "src/config/*.c"
    "src/ipc/*.c"
)
list(APPEND COMMON_SOURCES "src/server/main.c")


if(WIN32)
    add_compile_definitions(
        # Match the Windows 10 DPI APIs already used by the window module.
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE
    )

    # This linker option applies only to the MinGW GCC toolchain.
    if(MINGW)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc")
    endif()

    if(MSVC AND DEFINED VCPKG_INSTALLED_DIR)
        find_program(PKG_CONFIG_EXECUTABLE NAMES pkgconf pkg-config
            PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/pkgconf"
                  "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/pkgconf")
    endif()

    find_package(PkgConfig REQUIRED)

    pkg_check_modules(JSONC REQUIRED json-c)
    pkg_check_modules(GTK4 gtk4)

    # Windows packaging
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "GridFlux Window Manager")
    set(CPACK_NSIS_PACKAGE_NAME "GridFlux")
    set(CPACK_NSIS_CONTACT "0x4rd1@gmail.com")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/arxngr/gridflux")
    
    # NSIS Branding & Icons (The Setup.exe icon itself)
    string(REPLACE "/" "\\\\" CPACK_NSIS_ICON_PATH "${CMAKE_SOURCE_DIR}/icons/gridflux.ico")
    string(REPLACE "/" "\\\\" CPACK_NSIS_BANNER_PATH "${CMAKE_SOURCE_DIR}/icons/banner.bmp")
    string(REPLACE "/" "\\\\" CPACK_NSIS_DIALOG_PATH "${CMAKE_SOURCE_DIR}/icons/dialog.bmp")
    
    set(CPACK_NSIS_MUI_ICON ${CPACK_NSIS_ICON_PATH})
    set(CPACK_NSIS_MUI_UNIICON ${CPACK_NSIS_ICON_PATH})
    
    # NSIS Internal UI Graphics
    set(CPACK_PACKAGE_ICON ${CPACK_NSIS_BANNER_PATH})
    set(CPACK_NSIS_MUI_HEADERIMAGE ${CPACK_NSIS_BANNER_PATH})

endif()


if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    find_package(X11 REQUIRED)

    pkg_check_modules(GTK4 gtk4)
    pkg_check_modules(DBUS dbus-1)
    pkg_check_modules(JSONC REQUIRED json-c)

    add_compile_definitions(_GNU_SOURCE)

    file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/platform/unix/*.c"
    )


    set(PLATFORM_LIBRARIES
        ${X11_LIBRARIES}
        Xext
        Xi
        Xinerama
        m
        pthread
        ${JSONC_LIBRARIES}
    )

    set(PLATFORM_INCLUDES
        ${X11_INCLUDE_DIR}
        ${JSONC_INCLUDE_DIRS}
    )

    # KWin and DBus support removed as per user request
    # if(DBUS_FOUND)
    #     file(GLOB_RECURSE KWIN_SOURCES CONFIGURE_DEPENDS
    #         "${CMAKE_SOURCE_DIR}/src/platform/unix/kwin/*.c"
    #     )
    #     list(APPEND PLATFORM_SOURCES ${KWIN_SOURCES})
    #     add_definitions(-DGF_KWIN_SUPPORT)
    #
    #     list(APPEND PLATFORM_LIBRARIES ${DBUS_LIBRARIES})
    #     list(APPEND PLATFORM_INCLUDES ${DBUS_INCLUDE_DIRS})
    # endif()


    # DEB/RPM packaging
    set(CPACK_GENERATOR "DEB;RPM")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "GridFlux Project <0x4rd1@gmail.com>")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libx11-6, libjson-c5, libdbus-1-3, libgtk-4-1")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
    set(CPACK_RPM_PACKAGE_REQUIRES "libX11, json-c, dbus-1, gtk4")
    set(CPACK_RPM_PACKAGE_URL "https://github.com/arxngr/gridflux")

elseif(WIN32)
    file(GLOB PLATFORM_SOURCES "src/platform/windows/*.c")

    set(WINDOWS_WINDOW_SUPPORT_SOURCES
        src/platform/windows/internal.c
        src/utils/memory.c
        src/utils/logger.c
    )

    set(PLATFORM_LIBRARIES
        user32
        gdi32
        kernel32
        shell32
        advapi32
        ole32
        uuid
        dwmapi
        ${JSONC_LIBRARIES}
    )

    set(PLATFORM_INCLUDES
        ${JSONC_INCLUDE_DIRS}
    )

elseif(APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(JSONC REQUIRED json-c)

    file(GLOB PLATFORM_SOURCES "src/platform/macos/*.c")
    find_library(COCOA_FRAMEWORK Cocoa)

    set(PLATFORM_LIBRARIES
        ${COCOA_FRAMEWORK}
        ${JSONC_LIBRARIES}
    )

    set(PLATFORM_INCLUDES
        ${JSONC_INCLUDE_DIRS}
    )
    
    # macOS packaging
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "GridFlux")
    set(CPACK_DMG_FORMAT "UDZO")
endif()


add_executable(gridflux
    ${COMMON_SOURCES}
    ${PLATFORM_SOURCES}
)

if(WIN32)
    if(GF_DEV_MODE)
        # Dev mode: use asInvoker manifest so debugger can launch without elevation
        target_sources(gridflux PRIVATE ${CMAKE_SOURCE_DIR}/gridflux.dev.rc)
    else()
        target_sources(gridflux PRIVATE ${CMAKE_SOURCE_DIR}/gridflux.rc)
    endif()
    add_executable(gridflux-launcher WIN32
        src/launcher/win32.c
        src/config/path.c
        src/platform/windows/taskbar.c
        src/platform/windows/window.c
        ${WINDOWS_WINDOW_SUPPORT_SOURCES}
        src/launcher/gridflux-launcher.rc
    )
    target_include_directories(gridflux-launcher PRIVATE ${PLATFORM_INCLUDES})
    target_link_libraries(gridflux-launcher PRIVATE kernel32 user32 gdi32 shell32 advapi32 dwmapi)
    if(NOT GF_DEV_MODE)
        install(TARGETS gridflux-launcher DESTINATION bin)
    endif()
endif()

add_executable(gridflux-cli
    src/cli/main.c
    src/ipc/ipc_command.c
    src/ipc/protocol.c
    src/core/wm.c
    src/core/arrange.c
    src/core/workspace.c
    src/core/window.c
    src/core/events.c
    src/core/monitor.c
    src/core/debug.c
    src/core/border.c
    src/utils/list.c
    src/utils/memory.c
    src/utils/logger.c
    src/config/config.c
    src/config/path.c
    src/core/layout.c
    src/core/resize.c
    src/utils/file.c
    src/config/rules.c
    src/config/excludes.c
)

if(UNIX AND NOT APPLE)
    target_sources(gridflux-cli PRIVATE src/platform/unix/ipc.c)
elseif(WIN32)
    target_sources(gridflux-cli PRIVATE src/platform/windows/ipc.c)
endif()


if(GTK4_FOUND)
    add_executable(gridflux-gui WIN32
        src/gui/gui.c
        src/gui/window/main_window.c
        src/gui/window/toolbar.c
        src/gui/window/statusbar.c
        src/gui/panels/workspace_panel.c
        src/gui/panels/settings_panel.c
        src/gui/panels/rules_panel.c
        src/gui/panels/exclude_panel.c
        src/gui/panels/panel_common.c
        src/gui/widgets/workspace_card.c
        src/gui/bridge/ipc_client.c
        src/gui/bridge/refresh.c
        src/gui/platform/async.c
        src/gui/window/tray.c
        src/ipc/ipc_command.c
        src/ipc/protocol.c
        src/core/wm.c
        src/core/arrange.c
        src/core/workspace.c
        src/core/window.c
        src/core/events.c
        src/core/monitor.c
        src/core/debug.c
        src/core/border.c
        src/utils/list.c
        src/utils/memory.c
        src/config/config.c
        src/config/path.c
        src/utils/logger.c
        src/core/layout.c
        src/core/resize.c
        src/utils/file.c
        src/config/rules.c
        src/config/excludes.c
        ${PLATFORM_SOURCES}
    )

    if(WIN32)
        target_sources(gridflux-gui PRIVATE
            src/gui/platform/windows/gui_platform.c
            src/gui/bridge/windows/process_manager.c
        )
    elseif(UNIX AND NOT APPLE)
        target_sources(gridflux-gui PRIVATE
            src/gui/platform/unix/gui_platform.c
            src/gui/bridge/unix/process_manager.c
        )
    endif()

    target_include_directories(gridflux-gui PRIVATE
        ${GTK4_INCLUDE_DIRS}
        ${PLATFORM_INCLUDES}
    )

    target_link_libraries(gridflux-gui PRIVATE
        ${GTK4_LIBRARIES}
        ${PLATFORM_LIBRARIES}
    )
    if(GTK4_LIBRARY_DIRS)
        target_link_directories(gridflux-gui PRIVATE ${GTK4_LIBRARY_DIRS})
    endif()

    # Embed icon resource on Windows (taskbar, alt-tab, explorer)
    if(WIN32)
        target_sources(gridflux-gui PRIVATE ${CMAKE_SOURCE_DIR}/gridflux-gui.rc)
        if(MSVC)
            target_link_options(gridflux-gui PRIVATE "/ENTRY:mainCRTStartup")
        endif()
    endif()
endif()


target_link_libraries(gridflux PRIVATE ${PLATFORM_LIBRARIES})
target_link_libraries(gridflux-cli PRIVATE ${PLATFORM_LIBRARIES})

target_include_directories(gridflux PRIVATE ${PLATFORM_INCLUDES})
target_include_directories(gridflux-cli PRIVATE ${PLATFORM_INCLUDES})

if(JSONC_LIBRARY_DIRS)
    target_link_directories(gridflux PRIVATE ${JSONC_LIBRARY_DIRS})
    target_link_directories(gridflux-cli PRIVATE ${JSONC_LIBRARY_DIRS})
    if(TARGET gridflux-gui)
        target_link_directories(gridflux-gui PRIVATE ${JSONC_LIBRARY_DIRS})
    endif()
endif()

if(MSVC)
    # Prevent duplicate manifest resource errors on MSVC since they are already embedded via RC files
    set_target_properties(gridflux PROPERTIES LINK_FLAGS "/MANIFEST:NO")
    set_target_properties(gridflux-launcher PROPERTIES LINK_FLAGS "/MANIFEST:NO")
    if(TARGET gridflux-gui)
        set_target_properties(gridflux-gui PROPERTIES LINK_FLAGS "/MANIFEST:NO")
    endif()
endif()

if(GF_DEV_MODE)
    target_compile_definitions(gridflux PRIVATE GF_DEV_MODE)
    target_compile_definitions(gridflux-cli PRIVATE GF_DEV_MODE)
    if(TARGET gridflux-launcher)
        target_compile_definitions(gridflux-launcher PRIVATE GF_DEV_MODE)
    endif()
    if(TARGET gridflux-gui)
        target_compile_definitions(gridflux-gui PRIVATE GF_DEV_MODE)
    endif()
else()
    # Pass install paths as compile definitions for production builds
    if(TARGET gridflux-gui)
        target_compile_definitions(gridflux-gui PRIVATE
            GF_DATADIR="${CMAKE_INSTALL_PREFIX}/share/gridflux"
            GF_ICONDIR="${CMAKE_INSTALL_PREFIX}/share/icons"
        )
    endif()
endif()


if(NOT GF_DEV_MODE)
    install(TARGETS gridflux gridflux-cli DESTINATION bin)
    if(TARGET gridflux-gui)
        install(TARGETS gridflux-gui DESTINATION bin)
    endif()
    
    # Install desktop file and icons
    if(UNIX AND NOT APPLE)
        # Generate desktop file dynamically
        configure_file(
            "${CMAKE_SOURCE_DIR}/gridflux.desktop.in"
            "${CMAKE_BINARY_DIR}/gridflux-gui.desktop"
            @ONLY
        )
        install(FILES "${CMAKE_BINARY_DIR}/gridflux-gui.desktop" DESTINATION share/applications)
        if(TARGET gridflux-gui)
            configure_file(gridflux-tray.desktop.in gridflux-tray.desktop @ONLY)
            install(FILES "${CMAKE_BINARY_DIR}/gridflux-tray.desktop" DESTINATION /etc/xdg/autostart)
        endif()
        
        # Install icons to system icon theme for window manager integration
        install(DIRECTORY icons/hicolor/ DESTINATION share/icons/hicolor)

        # Install application data (logo used by GUI at runtime)
        install(FILES icons/gf_logo.png DESTINATION share/gridflux/icons)
    endif()
endif()


message(STATUS "")
message(STATUS "GridFlux Build Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Development mode: ${GF_DEV_MODE}")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "")

# Include CPack for packaging
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

include(CPack)
