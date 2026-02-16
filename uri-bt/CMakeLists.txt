cmake_minimum_required(VERSION 3.10)

project(uri-bt C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED dbus-1)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(DBUS_GLIB REQUIRED dbus-glib-1)

include_directories(
    ${DBUS_INCLUDE_DIRS}
    ${DBUS_GLIB_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    src
)

add_executable(uri-bt
    src/main.c
    src/adapter.c
    src/agent.c
)

target_link_libraries(uri-bt
    ${DBUS_LIBRARIES}
    ${DBUS_GLIB_LIBRARIES}
    ${GLIB_LIBRARIES}
)

