cmake_minimum_required(VERSION 3.24)

project(browl VERSION 0.1.0 LANGUAGES C CXX)

# Dependencies (cmake/bro_deps.cmake): an outer project's target wins, else the
# ../<name> working tree beside the top-level project, else the pinned commit,
# fetched at configure.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bro_deps.cmake)

# Targets
#   browl::browl   client side of the Wayland shell protocols (layer shell,
#                  popups, foreign toplevels, session lock, idle, screencopy)
#
# Wayland is a Linux (and BSD) display protocol. On Linux the library builds
# against libwayland-client with protocol code from wayland-scanner. Elsewhere
# the same headers compile, Display::connect() fails with the reason, and
# nothing Wayland is needed to configure or build (src/unavailable/).

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(_browl_top_level OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_browl_top_level ON)
endif()

option(BROWL_BUILD_TESTS "Build the browl tests" ${_browl_top_level})

# Coverage. Scoped to a top-level build: a consumer that add_subdirectory()s
# browl has no business having its own build instrumented from in here.
option(BROWL_COVERAGE "Instrument the build for gcov coverage (GCC/Clang)" OFF)
if(BROWL_COVERAGE AND _browl_top_level)
    if(MSVC)
        message(FATAL_ERROR "BROWL_COVERAGE requires GCC or Clang (gcov instrumentation)")
    endif()
    add_compile_options(--coverage -fprofile-update=atomic -O0 -g)
    add_link_options(--coverage)
    message(STATUS "browl: coverage instrumentation ON (-O0)")
endif()

if(MSVC AND _browl_top_level)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Memory-bounded parallelism on machines that build the whole bro stack at
# once. Job pools exist only in the Ninja generators; elsewhere (Visual Studio,
# Makefiles) the properties would be ignored, so they are not set at all.
if(_browl_top_level AND CMAKE_GENERATOR MATCHES "Ninja")
    if(NOT DEFINED BRO_COMPILE_JOBS)
        set(BRO_COMPILE_JOBS 4)
    endif()
    if(NOT DEFINED BRO_LINK_JOBS)
        set(BRO_LINK_JOBS 2)
    endif()
    set_property(GLOBAL PROPERTY JOB_POOLS compile_jobs=${BRO_COMPILE_JOBS} link_jobs=${BRO_LINK_JOBS})
    set(CMAKE_JOB_POOL_COMPILE compile_jobs)
    set(CMAKE_JOB_POOL_LINK link_jobs)
endif()

function(browl_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /MP)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

set(BROWL_PROTOCOLS
    ext-idle-notify-v1
    ext-session-lock-v1
    idle-inhibit-unstable-v1
    wlr-foreign-toplevel-management-unstable-v1
    wlr-layer-shell-unstable-v1
    wlr-screencopy-unstable-v1
    xdg-shell
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(BROWL_LINUX ON)
    find_package(PkgConfig REQUIRED)
    find_package(Threads REQUIRED)
    pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
    find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

    set(PROTO_DIR ${CMAKE_CURRENT_BINARY_DIR}/protocols)
    file(MAKE_DIRECTORY ${PROTO_DIR})
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
            VERBATIM)
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
        src/screencopy.cpp)
    # The generated protocol code is a separate object library so the
    # library's warning flags apply to its own sources only.
    add_library(browl_protocols OBJECT ${BROWL_PROTO_SOURCES} ${BROWL_PROTO_HEADERS})
    target_link_libraries(browl_protocols PUBLIC PkgConfig::WAYLAND_CLIENT)
    set_target_properties(browl_protocols PROPERTIES POSITION_INDEPENDENT_CODE ON)

    add_library(browl STATIC ${BROWL_SOURCES} $<TARGET_OBJECTS:browl_protocols>)
    add_dependencies(browl browl_protocols)
    target_include_directories(browl PRIVATE ${PROTO_DIR})
    target_link_libraries(browl PUBLIC PkgConfig::WAYLAND_CLIENT Threads::Threads)
else()
    set(BROWL_LINUX OFF)
    add_library(browl STATIC src/unavailable/browl.cpp)
    message(STATUS "browl: not Linux, Wayland is unavailable; Display::connect() reports why")
endif()

add_library(browl::browl ALIAS browl)

target_include_directories(browl
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROTO_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(browl PUBLIC cxx_std_20)
browl_warnings(browl)

# --- Standalone Bronze JavaScript API ---
# On by default only when browl is the top-level project. A library that
# consumes browl then never needs bronze for it; bro forces BROWL_ENABLE_API
# ON itself when it wants the binding.
#
# The binding drives the Wayland client directly, so it exists on Linux only;
# elsewhere the option is accepted and ignored (bro links browl on Linux only).
option(BROWL_ENABLE_API "Build Bronze JavaScript API" ${PROJECT_IS_TOP_LEVEL})
if(BROWL_ENABLE_API AND NOT BROWL_LINUX)
    message(STATUS "browl: not Linux, no Wayland client to bind; browl_api not built")
    set(BROWL_ENABLE_API OFF)
endif()
if(BROWL_ENABLE_API)
    # bronze (with brass, its code generator) through cmake/bro_deps.cmake: an
    # outer project's target, else ../bronze beside the top-level project, else
    # the pinned commit, fetched at configure.
    bro_dependency(bronze GITHUB wlejon/bronze REF 15ce4cf5fc25d8afe03882985ae95adcba46a6a9 TARGET bronze_runtime_shared
                   OPTIONS BRONZE_BUILD_SHARED_RUNTIME=ON BRONZE_BUILD_TESTS=OFF)

    add_subdirectory(src/api)
endif()

if(BROWL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
