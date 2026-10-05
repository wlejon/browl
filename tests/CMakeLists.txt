find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)

set(SERVER_PROTO_DIR ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${SERVER_PROTO_DIR})

set(SERVER_PROTO_HEADERS)

foreach(_proto ${BROWL_PROTOCOLS})
    set(_xml ${CMAKE_CURRENT_SOURCE_DIR}/../protocols/${_proto}.xml)
    set(_header ${SERVER_PROTO_DIR}/${_proto}-server-protocol.h)

    add_custom_command(
        OUTPUT ${_header}
        COMMAND ${WAYLAND_SCANNER} server-header ${_xml} ${_header}
        DEPENDS ${_xml}
        COMMENT "Generating Wayland server protocol: ${_proto}"
        VERBATIM
    )

    list(APPEND SERVER_PROTO_HEADERS ${_header})
endforeach()

add_library(headless_compositor STATIC
    headless_compositor.cpp
    headless_compositor_shell.cpp
    headless_compositor_services.cpp
    ${SERVER_PROTO_HEADERS}
)

target_include_directories(headless_compositor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${SERVER_PROTO_DIR}
        ${PROTO_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(headless_compositor
    PUBLIC
        browl
        PkgConfig::WAYLAND_SERVER
        Threads::Threads
)

set(BROWL_TESTS
    test_event_queue
    test_display
    test_layer_surface
    test_popup
    test_foreign_toplevel
    test_session_lock
    test_idle
    test_screencopy
    test_shm_pool
    test_live_compositor
)

foreach(_test ${BROWL_TESTS})
    add_executable(${_test} ${_test}.cpp)
    target_include_directories(${_test}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${SERVER_PROTO_DIR}
            ${PROTO_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
    target_link_libraries(${_test}
        PRIVATE
            headless_compositor
            browl
            PkgConfig::WAYLAND_SERVER
            Threads::Threads
    )
    add_test(NAME ${_test} COMMAND ${_test})
endforeach()

set_property(TEST test_live_compositor PROPERTY SKIP_RETURN_CODE 77)
