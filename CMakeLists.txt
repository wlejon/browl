cmake_minimum_required(VERSION 3.24)

project(browl VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Respect memory constraints: limit compile and link jobs by default
if(NOT DEFINED BRO_COMPILE_JOBS)
    set(BRO_COMPILE_JOBS 4)
endif()
if(NOT DEFINED BRO_LINK_JOBS)
    set(BRO_LINK_JOBS 2)
endif()
set_property(GLOBAL PROPERTY JOB_POOLS compile_jobs=${BRO_COMPILE_JOBS} link_jobs=${BRO_LINK_JOBS})
set(CMAKE_JOB_POOL_COMPILE compile_jobs)
set(CMAKE_JOB_POOL_LINK link_jobs)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(PROTO_DIR ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${PROTO_DIR})

set(BROWL_PROTOCOLS
    ext-idle-notify-v1
    ext-session-lock-v1
    idle-inhibit-unstable-v1
    wlr-foreign-toplevel-management-unstable-v1
    wlr-layer-shell-unstable-v1
    wlr-screencopy-unstable-v1
    xdg-shell
)

set(BROWL_PROTO_SOURCES)
set(BROWL_PROTO_HEADERS)

foreach(_proto ${BROWL_PROTOCOLS})
    set(_xml ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${_proto}.xml)
    set(_header ${PROTO_DIR}/${_proto}-client-protocol.h)
    set(_code ${PROTO_DIR}/${_proto}-protocol.c)

    add_custom_command(
        OUTPUT ${_header} ${_code}
        COMMAND ${WAYLAND_SCANNER} client-header ${_xml} ${_header}
        COMMAND ${WAYLAND_SCANNER} private-code ${_xml} ${_code}
        DEPENDS ${_xml}
        COMMENT "Generating Wayland client protocol: ${_proto}"
        VERBATIM
    )

    list(APPEND BROWL_PROTO_HEADERS ${_header})
    list(APPEND BROWL_PROTO_SOURCES ${_code})
endforeach()

set(BROWL_SOURCES
    src/display.cpp
    src/output.cpp
    src/seat.cpp
    src/shm_pool.cpp
    src/layer_surface.cpp
    src/popup.cpp
    src/foreign_toplevel.cpp
    src/session_lock.cpp
    src/idle.cpp
    src/screencopy.cpp
    ${BROWL_PROTO_SOURCES}
    ${BROWL_PROTO_HEADERS}
)

add_library(browl STATIC ${BROWL_SOURCES})
add_library(browl::browl ALIAS browl)

target_include_directories(browl
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${PROTO_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(browl
    PUBLIC
        PkgConfig::WAYLAND_CLIENT
        Threads::Threads
)

target_compile_features(browl PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(browl PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(browl PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Tests
option(BROWL_BUILD_TESTS "Build browl tests" ON)
if(BROWL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
