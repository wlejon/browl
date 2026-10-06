add_library(browl_api STATIC
    api.cpp
    host_class.cpp
    native_outputs.cpp
    native_toplevel.cpp
    native_layers.cpp
    native_screencopy.cpp
    native_lock.cpp
)

target_include_directories(browl_api PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
)

target_link_libraries(browl_api PUBLIC
    browl
    bronze_runtime_shared
)

target_compile_features(browl_api PUBLIC cxx_std_20)

browl_warnings(browl_api)
