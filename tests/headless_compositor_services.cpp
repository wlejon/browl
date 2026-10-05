#include "headless_compositor.h"

#include <wayland-server-core.h>
#include <wayland-server.h>

#include "ext-idle-notify-v1-server-protocol.h"
#include "ext-session-lock-v1-server-protocol.h"
#include "idle-inhibit-unstable-v1-server-protocol.h"
#include "wlr-screencopy-unstable-v1-server-protocol.h"

namespace browl::test {

namespace {

// ============================================================================
// Session Lock Implementation
// ============================================================================

static void lock_surface_destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static void lock_surface_ack_configure_req(struct wl_client* /*client*/, struct wl_resource* resource,
                                           uint32_t serial) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    if (comp) {
        comp->last_lock_surface_ack_serial_ = serial;
    }
}

static const struct ext_session_lock_surface_v1_interface lock_surface_impl = {
    .destroy = lock_surface_destroy_req,
    .ack_configure = lock_surface_ack_configure_req,
};

static void session_lock_destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static void session_lock_get_lock_surface_req(struct wl_client* client, struct wl_resource* resource,
                                              uint32_t id, struct wl_resource* /*surface*/,
                                              struct wl_resource* /*output*/) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    struct wl_resource* res =
        wl_resource_create(client, &ext_session_lock_surface_v1_interface,
                           wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &lock_surface_impl, comp, nullptr);
    if (comp) {
        comp->lock_surface_resource_ = res;
    }
}

static void session_lock_unlock_and_destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    if (comp) {
        comp->session_unlock_received_ = true;
    }
    wl_resource_destroy(resource);
}

static const struct ext_session_lock_v1_interface session_lock_impl = {
    .destroy = session_lock_destroy_req,
    .get_lock_surface = session_lock_get_lock_surface_req,
    .unlock_and_destroy = session_lock_unlock_and_destroy_req,
};

static void session_lock_manager_destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static void session_lock_manager_lock_req(struct wl_client* client, struct wl_resource* resource,
                                          uint32_t id) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    struct wl_resource* res =
        wl_resource_create(client, &ext_session_lock_v1_interface,
                           wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &session_lock_impl, comp, nullptr);
    if (comp) {
        comp->session_lock_resource_ = res;
    }
}

static const struct ext_session_lock_manager_v1_interface session_lock_manager_impl = {
    .destroy = session_lock_manager_destroy_req,
    .lock = session_lock_manager_lock_req,
};

static void bind_session_lock_manager(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    struct wl_resource* res =
        wl_resource_create(client, &ext_session_lock_manager_v1_interface, version, id);
    wl_resource_set_implementation(res, &session_lock_manager_impl, data, nullptr);
}

// ============================================================================
// Idle Inhibit Implementation
// ============================================================================

static void idle_inhibitor_destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    if (comp) {
        comp->idle_inhibitor_destroyed_ = true;
    }
    wl_resource_destroy(resource);
}

static const struct zwp_idle_inhibitor_v1_interface idle_inhibitor_impl = {
    .destroy = idle_inhibitor_destroy_req,
};

static void idle_inhibit_manager_destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static void idle_inhibit_manager_create_inhibitor_req(struct wl_client* client, struct wl_resource* resource,
                                                      uint32_t id, struct wl_resource* /*surface*/) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    struct wl_resource* res =
        wl_resource_create(client, &zwp_idle_inhibitor_v1_interface,
                           wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &idle_inhibitor_impl, comp, nullptr);
    if (comp) {
        comp->idle_inhibitor_created_ = true;
    }
}

static const struct zwp_idle_inhibit_manager_v1_interface idle_inhibit_manager_impl = {
    .destroy = idle_inhibit_manager_destroy_req,
    .create_inhibitor = idle_inhibit_manager_create_inhibitor_req,
};

static void bind_idle_inhibit_manager(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    struct wl_resource* res =
        wl_resource_create(client, &zwp_idle_inhibit_manager_v1_interface, version, id);
    wl_resource_set_implementation(res, &idle_inhibit_manager_impl, data, nullptr);
}

// ============================================================================
// Screencopy Implementation
// ============================================================================

static void screencopy_frame_copy_req(struct wl_client* /*client*/, struct wl_resource* resource,
                                      struct wl_resource* /*buffer*/) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    if (comp) {
        comp->screencopy_copy_received_ = true;
    }
}

static void screencopy_frame_destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static void screencopy_frame_copy_with_damage_req(struct wl_client* /*client*/, struct wl_resource* resource,
                                                  struct wl_resource* /*buffer*/) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    if (comp) {
        comp->screencopy_copy_received_ = true;
    }
}

static const struct zwlr_screencopy_frame_v1_interface screencopy_frame_impl = {
    .copy = screencopy_frame_copy_req,
    .destroy = screencopy_frame_destroy_req,
    .copy_with_damage = screencopy_frame_copy_with_damage_req,
};

static void screencopy_manager_capture_output(struct wl_client* client, struct wl_resource* resource,
                                              uint32_t id, int32_t /*overlay_cursor*/,
                                              struct wl_resource* /*output*/) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    struct wl_resource* res =
        wl_resource_create(client, &zwlr_screencopy_frame_v1_interface,
                           wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &screencopy_frame_impl, comp, nullptr);
    if (comp) {
        comp->screencopy_frame_resource_ = res;
    }
}

static void screencopy_manager_capture_output_region(struct wl_client* client, struct wl_resource* resource,
                                                     uint32_t id, int32_t /*overlay_cursor*/,
                                                     struct wl_resource* /*output*/, int32_t /*x*/, int32_t /*y*/,
                                                     int32_t /*width*/, int32_t /*height*/) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    struct wl_resource* res =
        wl_resource_create(client, &zwlr_screencopy_frame_v1_interface,
                           wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &screencopy_frame_impl, comp, nullptr);
    if (comp) {
        comp->screencopy_frame_resource_ = res;
    }
}

static void screencopy_manager_destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static const struct zwlr_screencopy_manager_v1_interface screencopy_manager_impl = {
    .capture_output = screencopy_manager_capture_output,
    .capture_output_region = screencopy_manager_capture_output_region,
    .destroy = screencopy_manager_destroy_req,
};

static void bind_screencopy_manager(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    struct wl_resource* res =
        wl_resource_create(client, &zwlr_screencopy_manager_v1_interface, version, id);
    wl_resource_set_implementation(res, &screencopy_manager_impl, data, nullptr);
}

// ============================================================================
// Idle Notify Implementation
// ============================================================================

static void idle_notification_destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static const struct ext_idle_notification_v1_interface idle_notification_impl = {
    .destroy = idle_notification_destroy_req,
};

static void idle_notifier_destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static void idle_notifier_get_idle_notification_req(struct wl_client* client, struct wl_resource* resource,
                                                    uint32_t id, uint32_t /*timeout*/,
                                                    struct wl_resource* /*seat*/) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    struct wl_resource* res =
        wl_resource_create(client, &ext_idle_notification_v1_interface,
                           wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &idle_notification_impl, comp, nullptr);
    if (comp) {
        comp->idle_notification_resource_ = res;
    }
}

static void idle_notifier_get_input_idle_notification_req(struct wl_client* client, struct wl_resource* resource,
                                                          uint32_t id, uint32_t timeout,
                                                          struct wl_resource* seat) {
    idle_notifier_get_idle_notification_req(client, resource, id, timeout, seat);
}

static const struct ext_idle_notifier_v1_interface idle_notifier_impl = {
    .destroy = idle_notifier_destroy_req,
    .get_idle_notification = idle_notifier_get_idle_notification_req,
    .get_input_idle_notification = idle_notifier_get_input_idle_notification_req,
};

static void bind_idle_notifier(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    struct wl_resource* res =
        wl_resource_create(client, &ext_idle_notifier_v1_interface, version, id);
    wl_resource_set_implementation(res, &idle_notifier_impl, data, nullptr);
}

}  // namespace

void HeadlessCompositor::init_service_globals() {
    session_lock_global_ = wl_global_create(
        display_, &ext_session_lock_manager_v1_interface, 1, this, bind_session_lock_manager);
    idle_inhibit_global_ = wl_global_create(
        display_, &zwp_idle_inhibit_manager_v1_interface, 1, this, bind_idle_inhibit_manager);
    screencopy_global_ = wl_global_create(
        display_, &zwlr_screencopy_manager_v1_interface, 3, this, bind_screencopy_manager);
    idle_notify_global_ = wl_global_create(
        display_, &ext_idle_notifier_v1_interface, 1, this, bind_idle_notifier);
}

void HeadlessCompositor::send_session_locked() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (session_lock_resource_) {
        ext_session_lock_v1_send_locked(session_lock_resource_);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::send_session_lock_finished() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (session_lock_resource_) {
        ext_session_lock_v1_send_finished(session_lock_resource_);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::configure_lock_surface(uint32_t width, uint32_t height) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (lock_surface_resource_) {
        uint32_t serial = next_serial_++;
        ext_session_lock_surface_v1_send_configure(lock_surface_resource_, serial, width, height);
        wl_display_flush_clients(display_);
    }
}

bool HeadlessCompositor::session_lock_created() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return session_lock_resource_ != nullptr;
}

bool HeadlessCompositor::session_unlock_received() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return session_unlock_received_;
}

bool HeadlessCompositor::lock_surface_created() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lock_surface_resource_ != nullptr;
}

uint32_t HeadlessCompositor::last_lock_surface_ack_serial() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_lock_surface_ack_serial_;
}

bool HeadlessCompositor::idle_inhibitor_created() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return idle_inhibitor_created_;
}

bool HeadlessCompositor::idle_inhibitor_destroyed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return idle_inhibitor_destroyed_;
}

void HeadlessCompositor::fire_idle() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (idle_notification_resource_) {
        ext_idle_notification_v1_send_idled(idle_notification_resource_);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::fire_resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (idle_notification_resource_) {
        ext_idle_notification_v1_send_resumed(idle_notification_resource_);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::send_screencopy_buffer(uint32_t format, uint32_t width, uint32_t height,
                                               uint32_t stride) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (screencopy_frame_resource_) {
        zwlr_screencopy_frame_v1_send_buffer(
            screencopy_frame_resource_, format, width, height, stride);
        zwlr_screencopy_frame_v1_send_buffer_done(screencopy_frame_resource_);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::send_screencopy_ready(uint32_t tv_sec, uint32_t tv_nsec) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (screencopy_frame_resource_) {
        zwlr_screencopy_frame_v1_send_ready(screencopy_frame_resource_, 0, tv_sec, tv_nsec);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::send_screencopy_failed() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (screencopy_frame_resource_) {
        zwlr_screencopy_frame_v1_send_failed(screencopy_frame_resource_);
        wl_display_flush_clients(display_);
    }
}

bool HeadlessCompositor::screencopy_frame_created() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return screencopy_frame_resource_ != nullptr;
}

bool HeadlessCompositor::screencopy_copy_received() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return screencopy_copy_received_;
}

}  // namespace browl::test
