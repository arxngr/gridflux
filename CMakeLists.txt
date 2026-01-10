cmake_minimum_required(VERSION 3.12)
project(gridflux VERSION 2.0.0 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(GF_DEV_MODE "Enable development mode (read config from current dir)" OFF)

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
        "${CMAKE_SOURCE_DIR}/src/platform/platform_env.c"
    )

    set(PLATFORM_LIBRARIES
        ${X11_LIBRARIES}
        m
        ${JSONC_LIBRARIES}
    )

    set(PLATFORM_INCLUDES
        ${X11_INCLUDE_DIR}
        ${JSONC_INCLUDE_DIRS}
    )

    if(DBUS_FOUND)
        file(GLOB_RECURSE KWIN_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/src/platform/unix/kwin/*.c"
        )
        list(APPEND PLATFORM_SOURCES ${KWIN_SOURCES})
        add_definitions(-DGF_KWIN_SUPPORT)

        list(APPEND PLATFORM_LIBRARIES ${DBUS_LIBRARIES})
        list(APPEND PLATFORM_INCLUDES ${DBUS_INCLUDE_DIRS})
    endif()

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
    
    # Install desktop file
    if(UNIX AND NOT APPLE)
        install(FILES gridflux.desktop DESTINATION share/applications)
        
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
