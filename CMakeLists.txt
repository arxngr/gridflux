cmake_minimum_required(VERSION 3.12)
project(gridflux VERSION 0.1.2 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(GF_DEV_MODE "Enable development mode (read config from current dir)" OFF)

# Packaging configuration
set(CPACK_PACKAGE_NAME "gridflux")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION "Virtual workspace & Window management tool for arranging and managing windows")
set(CPACK_PACKAGE_VENDOR "GridFlux Project")
set(CPACK_PACKAGE_CONTACT "0x4rd1@gmail.com")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/arxngr/gridflux")
set(CPACK_SET_DESTDIR TRUE)

include_directories(src)

file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS "src/*.c")
list(REMOVE_ITEM COMMON_SOURCES "${CMAKE_SOURCE_DIR}/src/client.c")
list(REMOVE_ITEM COMMON_SOURCES "${CMAKE_SOURCE_DIR}/src/gui.c")


if(WIN32)
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE
    )

    find_package(PkgConfig REQUIRED)

    pkg_check_modules(JSONC REQUIRED json-c)
    pkg_check_modules(GTK4 gtk4)

    # Windows packaging
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "GridFlux Window Manager")
    set(CPACK_NSIS_PACKAGE_NAME "GridFlux")
    set(CPACK_NSIS_CONTACT "0x4rd1@gmail.com")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/arxngr/gridflux")

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
        m
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

    set(PLATFORM_LIBRARIES
        user32
        gdi32
        kernel32
        shell32
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
        ${PLATFORM_SOURCES}
    )

    target_include_directories(gridflux-gui PRIVATE
        ${GTK4_INCLUDE_DIRS}
        ${PLATFORM_INCLUDES}
    )

    target_link_libraries(gridflux-gui PRIVATE
        ${GTK4_LIBRARIES}
        ${PLATFORM_LIBRARIES}
    )
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


if(GF_DEV_MODE)
    target_compile_definitions(gridflux PRIVATE GF_DEV_MODE)
    target_compile_definitions(gridflux-cli PRIVATE GF_DEV_MODE)
    if(TARGET gridflux-gui)
        target_compile_definitions(gridflux-gui PRIVATE GF_DEV_MODE)
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
        
        # Install icons
        install(DIRECTORY icons/hicolor/ DESTINATION share/icons/hicolor)
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
include(CPack)
