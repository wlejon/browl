# browl tests. Exit code 77 is a skip (something the machine genuinely
# lacks, with the reason printed); any other nonzero code is a failure.
# Checks are real in every configuration (tests/check.h, no assert()).
#
#   test_event_queue        portable, every platform
#   test_unavailable        Windows / macOS: connect() fails with the reason
#   test_<feature>          Linux: against HeadlessCompositor, an in-process
#                           test double (hand-written wayland-server; see
#                           fake_session.h for what it does and does not prove)
#   test_sway_<area>        Linux: against a real compositor, a private
#                           headless sway started per test by
#                           run-headless-sway.sh (skips when sway is absent)
function(browl_test name)
    cmake_parse_arguments(T "" "TIMEOUT" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${T_SOURCES})
    target_link_libraries(${name} PRIVATE browl::browl ${T_LIBS})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    browl_warnings(${name})
    if(NOT T_TIMEOUT)
        set(T_TIMEOUT 60)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT ${T_TIMEOUT})
endfunction()

browl_test(test_event_queue SOURCES test_event_queue.cpp)

if(NOT BROWL_LINUX)
    browl_test(test_unavailable SOURCES test_unavailable.cpp)

    # Every test executable links no_dialogs.cpp (see the file). Windows never
    # gets past the return() below, so this is the end of the file there.
    if(WIN32)
        get_property(_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
        foreach(_t IN LISTS _test_targets)
            get_target_property(_type ${_t} TYPE)
            if(_type STREQUAL "EXECUTABLE")
                target_sources(${_t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/no_dialogs.cpp)
            endif()
        endforeach()
    endif()
    return()
endif()

pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)

set(SERVER_PROTO_DIR ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${SERVER_PROTO_DIR})
set(SERVER_PROTO_HEADERS)
foreach(_proto ${BROWL_PROTOCOLS})
    set(_xml ${PROJECT_SOURCE_DIR}/protocols/${_proto}.xml)
    set(_header ${SERVER_PROTO_DIR}/${_proto}-server-protocol.h)
    add_custom_command(
        OUTPUT ${_header}
        COMMAND ${WAYLAND_SCANNER} server-header ${_xml} ${_header}
        DEPENDS ${_xml}
        COMMENT "Generating Wayland server protocol: ${_proto}"
        VERBATIM)
    list(APPEND SERVER_PROTO_HEADERS ${_header})
endforeach()

# The test double. It defines the protocol interfaces' server side from the
# same generated code the library links, so it shares browl's objects.
add_library(headless_compositor STATIC
    headless_compositor.cpp
    headless_compositor_shell.cpp
    headless_compositor_services.cpp
    headless_compositor_app.cpp
    headless_compositor_input.cpp
    ${SERVER_PROTO_HEADERS})
target_include_directories(headless_compositor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${SERVER_PROTO_DIR} ${PROTO_DIR})
target_link_libraries(headless_compositor PUBLIC browl::browl PkgConfig::WAYLAND_SERVER Threads::Threads
                                          PkgConfig::XKBCOMMON)

foreach(_test test_display test_layer_surface test_popup test_foreign_toplevel test_session_lock
              test_idle test_screencopy test_shm_pool test_window test_input test_selection)
    browl_test(${_test} SOURCES ${_test}.cpp LIBS headless_compositor)
endforeach()

# A manual test client, not a ctest: drags files out of a window, the way a
# file manager does, for checking a drop target (see the file).
add_executable(browl_drag_source tool_drag_source.cpp)
target_link_libraries(browl_drag_source PRIVATE browl::browl)
browl_warnings(browl_drag_source)

# Keymap needs no compositor, only xkbcommon and its data.
browl_test(test_keymap SOURCES test_keymap.cpp LIBS PkgConfig::XKBCOMMON Threads::Threads)

# Against a real compositor. Each test gets its own sway, so a lock or a
# fullscreen window never leaks from one test into the next. The harness
# also speaks zwp_virtual_keyboard_v1 (test-only, tests/protocols/) to give
# the input-less headless seat a keyboard, hence keyboard focus and serials.
set(_vk_xml ${CMAKE_CURRENT_SOURCE_DIR}/protocols/virtual-keyboard-unstable-v1.xml)
set(_vk_header ${SERVER_PROTO_DIR}/virtual-keyboard-unstable-v1-client-protocol.h)
set(_vk_code ${SERVER_PROTO_DIR}/virtual-keyboard-unstable-v1-protocol.c)
add_custom_command(
    OUTPUT ${_vk_header} ${_vk_code}
    COMMAND ${WAYLAND_SCANNER} client-header ${_vk_xml} ${_vk_header}
    COMMAND ${WAYLAND_SCANNER} private-code ${_vk_xml} ${_vk_code}
    DEPENDS ${_vk_xml}
    COMMENT "Generating Wayland client protocol: virtual-keyboard-unstable-v1 (tests)"
    VERBATIM)
add_library(sway_harness STATIC sway_harness.cpp sway_harness_input.cpp ${_vk_code} ${_vk_header})
target_include_directories(sway_harness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROTO_DIR} ${SERVER_PROTO_DIR})
target_link_libraries(sway_harness PUBLIC browl::browl PkgConfig::XKBCOMMON)
browl_warnings(sway_harness)
foreach(_test test_sway_shell test_sway_toplevel test_sway_lock_idle test_sway_window)
    add_executable(${_test} ${_test}.cpp)
    target_link_libraries(${_test} PRIVATE sway_harness)
    browl_warnings(${_test})
    add_test(NAME ${_test}
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-headless-sway.sh $<TARGET_FILE:${_test}>)
    set_tests_properties(${_test} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endforeach()

if(BROWL_ENABLE_API)
    add_executable(browl_test_api
        test_wl_api.cpp
    )
    target_include_directories(browl_test_api PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/api
        ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(browl_test_api PRIVATE
        browl_api
        browl::browl
        headless_compositor
        bronze_runtime_shared
    )
    browl_warnings(browl_test_api)

    if(NOT BRASS_HOST_BACKEND)
        message(STATUS "browl: browl_test_api not registered (brass has no code generator for this target)")
    else()
        add_test(NAME browl_test_api COMMAND browl_test_api)
        set_tests_properties(browl_test_api PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

        add_test(NAME browl_test_api_gc_stress COMMAND browl_test_api)
        set_tests_properties(browl_test_api_gc_stress PROPERTIES
            TIMEOUT 60
            SKIP_RETURN_CODE 77
            ENVIRONMENT "BRONZE_GC_STRESS=1;BRONZE_GC_POISON=1"
        )
    endif()
endif()
