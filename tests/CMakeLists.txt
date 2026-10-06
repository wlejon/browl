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
    ${SERVER_PROTO_HEADERS})
target_include_directories(headless_compositor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${SERVER_PROTO_DIR} ${PROTO_DIR})
target_link_libraries(headless_compositor PUBLIC browl::browl PkgConfig::WAYLAND_SERVER Threads::Threads)

foreach(_test test_display test_layer_surface test_popup test_foreign_toplevel test_session_lock
              test_idle test_screencopy test_shm_pool)
    browl_test(${_test} SOURCES ${_test}.cpp LIBS headless_compositor)
endforeach()

# Against a real compositor. Each test gets its own sway, so a lock or a
# fullscreen window never leaks from one test into the next.
add_library(sway_harness STATIC sway_harness.cpp)
target_include_directories(sway_harness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROTO_DIR})
target_link_libraries(sway_harness PUBLIC browl::browl)
browl_warnings(sway_harness)
foreach(_test test_sway_shell test_sway_toplevel test_sway_lock_idle)
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
