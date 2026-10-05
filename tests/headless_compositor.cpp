#include "headless_compositor.h"

#include <sys/socket.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server.h>

#include <chrono>
#include <cstring>

namespace browl::test {

namespace {

// ============================================================================
// Compositor / Surface Implementation
// ============================================================================

static void surface_destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static void surface_attach_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                               struct wl_resource* /*buffer*/, int32_t /*x*/, int32_t /*y*/) {}

static void surface_damage_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                               int32_t /*x*/, int32_t /*y*/, int32_t /*width*/, int32_t /*height*/) {}

static void surface_frame_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                              uint32_t /*callback*/) {}

static void surface_set_opaque_region_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                          struct wl_resource* /*region*/) {}

static void surface_set_input_region_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                         struct wl_resource* /*region*/) {}

static void surface_commit_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    if (comp) {
        comp->layer_surface_committed_ = true;
    }
}

static void surface_set_buffer_transform_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                             int32_t /*transform*/) {}

static void surface_set_buffer_scale_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                         int32_t /*scale*/) {}

static void surface_damage_buffer_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                      int32_t /*x*/, int32_t /*y*/, int32_t /*width*/, int32_t /*height*/) {}

static const struct wl_surface_interface surface_interface = {
    .destroy = surface_destroy_req,
    .attach = surface_attach_req,
    .damage = surface_damage_req,
    .frame = surface_frame_req,
    .set_opaque_region = surface_set_opaque_region_req,
    .set_input_region = surface_set_input_region_req,
    .commit = surface_commit_req,
    .set_buffer_transform = surface_set_buffer_transform_req,
    .set_buffer_scale = surface_set_buffer_scale_req,
    .damage_buffer = surface_damage_buffer_req,
};

static void compositor_create_surface(struct wl_client* client, struct wl_resource* resource,
                                      uint32_t id) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    struct wl_resource* surf_res =
        wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(surf_res, &surface_interface, comp, nullptr);
    if (comp) {
        comp->surface_resource_ = surf_res;
    }
}

static void compositor_create_region(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                     uint32_t /*id*/) {}

static const struct wl_compositor_interface compositor_interface = {
    .create_surface = compositor_create_surface,
    .create_region = compositor_create_region,
};

static void bind_compositor(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    struct wl_resource* res = wl_resource_create(client, &wl_compositor_interface, version, id);
    wl_resource_set_implementation(res, &compositor_interface, data, nullptr);
}

// ============================================================================
// Output Implementation
// ============================================================================

static void bind_output(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    auto* comp = static_cast<HeadlessCompositor*>(data);
    struct wl_resource* res = wl_resource_create(client, &wl_output_interface, version, id);
    wl_resource_set_implementation(res, nullptr, data, nullptr);
    comp->output_resource_ = res;

    wl_output_send_geometry(res, 0, 0, 530, 300, WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB,
                           "TestMake", "TestModel", WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(res, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
                       1920, 1080, 60000);
    if (version >= 2) {
        wl_output_send_scale(res, 1);
    }
    if (version >= 4) {
        wl_output_send_name(res, "DP-1");
        wl_output_send_description(res, "Test Output DP-1");
    }
    if (version >= 2) {
        wl_output_send_done(res);
    }
}

// ============================================================================
// Seat Implementation
// ============================================================================

static void bind_seat(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    auto* comp = static_cast<HeadlessCompositor*>(data);
    struct wl_resource* res = wl_resource_create(client, &wl_seat_interface, version, id);
    wl_resource_set_implementation(res, nullptr, data, nullptr);
    comp->seat_resource_ = res;

    wl_seat_send_capabilities(res, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
    if (version >= 2) {
        wl_seat_send_name(res, "seat0");
    }
}

}  // namespace

HeadlessCompositor::HeadlessCompositor() {
    display_ = wl_display_create();
    loop_ = wl_display_get_event_loop(display_);
    wl_display_init_shm(display_);
    init_globals();
}

HeadlessCompositor::~HeadlessCompositor() {
    stop();
    if (display_) {
        wl_display_destroy(display_);
    }
}

void HeadlessCompositor::init_globals() {
    compositor_global_ = wl_global_create(display_, &wl_compositor_interface, 4, this, bind_compositor);
    output_global_ = wl_global_create(display_, &wl_output_interface, 4, this, bind_output);
    seat_global_ = wl_global_create(display_, &wl_seat_interface, 7, this, bind_seat);

    init_shell_globals();
    init_service_globals();
}

int HeadlessCompositor::create_client_fd() {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) {
        return -1;
    }

    client_ = wl_client_create(display_, sv[0]);
    if (!client_) {
        close(sv[0]);
        close(sv[1]);
        return -1;
    }

    wl_display_flush_clients(display_);
    return sv[1];
}

const char* HeadlessCompositor::add_socket_auto() {
    return wl_display_add_socket_auto(display_);
}

int HeadlessCompositor::add_socket(const char* name) {
    return wl_display_add_socket(display_, name);
}

void HeadlessCompositor::start() {
    running_ = true;
    worker_ = std::thread([this]() {
        while (running_) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                wl_display_flush_clients(display_);
                wl_event_loop_dispatch(loop_, 10);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
}

void HeadlessCompositor::stop() {
    if (running_) {
        running_ = false;
        if (worker_.joinable()) {
            worker_.join();
        }
    }
}

void HeadlessCompositor::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::step_loop(int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    wl_display_flush_clients(display_);
    wl_event_loop_dispatch(loop_, timeout_ms);
}

void HeadlessCompositor::send_output_done() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (output_resource_) {
        wl_output_send_done(output_resource_);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::send_seat_caps(uint32_t caps, const char* name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (seat_resource_) {
        wl_seat_send_capabilities(seat_resource_, caps);
        if (name) {
            wl_seat_send_name(seat_resource_, name);
        }
        wl_display_flush_clients(display_);
    }
}

}  // namespace browl::test
