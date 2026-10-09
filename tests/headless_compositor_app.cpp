// The test double's application-window globals: xdg-decoration, viewporter,
// fractional scale, presentation time, xdg-activation, toplevel icon,
// cursor shape, xdg-output. Requests are recorded in app_ (read through
// app_state()); events are sent by the configure_window / send_* methods.
#include "headless_compositor.h"

#include <wayland-server-core.h>
#include <wayland-server.h>

#include "cursor-shape-v1-server-protocol.h"
#include "fractional-scale-v1-server-protocol.h"
#include "presentation-time-server-protocol.h"
#include "viewporter-server-protocol.h"
#include "xdg-activation-v1-server-protocol.h"
#include "xdg-decoration-unstable-v1-server-protocol.h"
#include "xdg-output-unstable-v1-server-protocol.h"
#include "xdg-shell-server-protocol.h"
#include "xdg-toplevel-icon-v1-server-protocol.h"

#include <algorithm>
#include <ctime>
#include <string>

namespace browl::test {

namespace {

HeadlessCompositor* comp_of(struct wl_resource* resource) {
    return static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
}

void destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

// --- zxdg_decoration_manager_v1 ----------------------------------------------------

void decoration_set_mode_req(struct wl_client*, struct wl_resource* resource, uint32_t mode) {
    comp_of(resource)->app_.decoration_mode_requested = mode;
}

void decoration_unset_mode_req(struct wl_client*, struct wl_resource* resource) {
    comp_of(resource)->app_.decoration_mode_requested = 0;
}

const struct zxdg_toplevel_decoration_v1_interface decoration_impl = {
    .destroy = destroy_req,
    .set_mode = decoration_set_mode_req,
    .unset_mode = decoration_unset_mode_req,
};

void decoration_destroyed(struct wl_resource* resource) {
    auto* comp = comp_of(resource);
    if (comp->decoration_resource_ == resource) comp->decoration_resource_ = nullptr;
}

void decoration_manager_get_req(struct wl_client* client, struct wl_resource* resource, uint32_t id,
                                struct wl_resource* /*toplevel*/) {
    auto* comp = comp_of(resource);
    auto* res = wl_resource_create(client, &zxdg_toplevel_decoration_v1_interface,
                                   wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &decoration_impl, comp, decoration_destroyed);
    comp->decoration_resource_ = res;
    ++comp->app_.decorations_created;
}

const struct zxdg_decoration_manager_v1_interface decoration_manager_impl = {
    .destroy = destroy_req,
    .get_toplevel_decoration = decoration_manager_get_req,
};

// --- wp_viewporter ---------------------------------------------------------------------

void viewport_set_source_req(struct wl_client*, struct wl_resource*, wl_fixed_t, wl_fixed_t, wl_fixed_t,
                             wl_fixed_t) {}

void viewport_set_destination_req(struct wl_client*, struct wl_resource* resource, int32_t width,
                                  int32_t height) {
    comp_of(resource)->app_.viewport_width = width;
    comp_of(resource)->app_.viewport_height = height;
}

const struct wp_viewport_interface viewport_impl = {
    .destroy = destroy_req,
    .set_source = viewport_set_source_req,
    .set_destination = viewport_set_destination_req,
};

void viewporter_get_viewport_req(struct wl_client* client, struct wl_resource* resource, uint32_t id,
                                 struct wl_resource* /*surface*/) {
    auto* res = wl_resource_create(client, &wp_viewport_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &viewport_impl, comp_of(resource), nullptr);
    comp_of(resource)->app_.viewport_created = true;
}

const struct wp_viewporter_interface viewporter_impl = {
    .destroy = destroy_req,
    .get_viewport = viewporter_get_viewport_req,
};

// --- wp_fractional_scale_manager_v1 -------------------------------------------------

const struct wp_fractional_scale_v1_interface fractional_impl = {
    .destroy = destroy_req,
};

void fractional_destroyed(struct wl_resource* resource) {
    auto* comp = comp_of(resource);
    if (comp->fractional_resource_ == resource) comp->fractional_resource_ = nullptr;
}

void fractional_manager_get_req(struct wl_client* client, struct wl_resource* resource, uint32_t id,
                                struct wl_resource* /*surface*/) {
    auto* comp = comp_of(resource);
    auto* res = wl_resource_create(client, &wp_fractional_scale_v1_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &fractional_impl, comp, fractional_destroyed);
    comp->fractional_resource_ = res;
    comp->app_.fractional_created = true;
}

const struct wp_fractional_scale_manager_v1_interface fractional_manager_impl = {
    .destroy = destroy_req,
    .get_fractional_scale = fractional_manager_get_req,
};

// --- wp_presentation -----------------------------------------------------------------

void feedback_destroyed(struct wl_resource* resource) {
    auto* comp = comp_of(resource);
    auto& f = comp->feedbacks_;
    f.erase(std::remove(f.begin(), f.end(), resource), f.end());
}

void presentation_feedback_req(struct wl_client* client, struct wl_resource* resource,
                               struct wl_resource* /*surface*/, uint32_t id) {
    auto* comp = comp_of(resource);
    auto* res = wl_resource_create(client, &wp_presentation_feedback_interface,
                                   wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, nullptr, comp, feedback_destroyed);
    comp->feedbacks_.push_back(res);
    ++comp->app_.feedbacks_requested;
}

const struct wp_presentation_interface presentation_impl = {
    .destroy = destroy_req,
    .feedback = presentation_feedback_req,
};

// --- xdg_activation_v1 ---------------------------------------------------------------

void token_set_serial_req(struct wl_client*, struct wl_resource* resource, uint32_t serial,
                          struct wl_resource* /*seat*/) {
    comp_of(resource)->app_.token_serial = serial;
}

void token_set_app_id_req(struct wl_client*, struct wl_resource* resource, const char* app_id) {
    comp_of(resource)->app_.token_app_id = app_id;
}

void token_set_surface_req(struct wl_client*, struct wl_resource* resource, struct wl_resource* surface) {
    comp_of(resource)->app_.token_has_surface = surface != nullptr;
}

void token_commit_req(struct wl_client*, struct wl_resource* resource) {
    auto* comp = comp_of(resource);
    const std::string token = "token-" + std::to_string(comp->next_token_++);
    xdg_activation_token_v1_send_done(resource, token.c_str());
}

const struct xdg_activation_token_v1_interface token_impl = {
    .set_serial = token_set_serial_req,
    .set_app_id = token_set_app_id_req,
    .set_surface = token_set_surface_req,
    .commit = token_commit_req,
    .destroy = destroy_req,
};

void activation_get_token_req(struct wl_client* client, struct wl_resource* resource, uint32_t id) {
    auto* comp = comp_of(resource);
    auto* res = wl_resource_create(client, &xdg_activation_token_v1_interface,
                                   wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &token_impl, comp, nullptr);
    ++comp->app_.tokens_requested;
}

void activation_activate_req(struct wl_client*, struct wl_resource* resource, const char* token,
                             struct wl_resource* /*surface*/) {
    comp_of(resource)->app_.activated_token = token;
}

const struct xdg_activation_v1_interface activation_impl = {
    .destroy = destroy_req,
    .get_activation_token = activation_get_token_req,
    .activate = activation_activate_req,
};

// --- xdg_toplevel_icon_manager_v1 ----------------------------------------------------

void icon_set_name_req(struct wl_client*, struct wl_resource*, const char*) {}

void icon_add_buffer_req(struct wl_client*, struct wl_resource* resource, struct wl_resource* buffer,
                         int32_t /*scale*/) {
    auto* comp = comp_of(resource);
    ++comp->app_.icon_buffers_added;
    if (wl_shm_buffer* shm = wl_shm_buffer_get(buffer)) {
        comp->app_.icon_buffer_size = wl_shm_buffer_get_width(shm);
        wl_shm_buffer_begin_access(shm);
        comp->app_.icon_first_pixel = *static_cast<const uint32_t*>(wl_shm_buffer_get_data(shm));
        wl_shm_buffer_end_access(shm);
    }
}

const struct xdg_toplevel_icon_v1_interface icon_impl = {
    .destroy = destroy_req,
    .set_name = icon_set_name_req,
    .add_buffer = icon_add_buffer_req,
};

void icon_manager_create_icon_req(struct wl_client* client, struct wl_resource* resource, uint32_t id) {
    auto* res = wl_resource_create(client, &xdg_toplevel_icon_v1_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &icon_impl, comp_of(resource), nullptr);
}

void icon_manager_set_icon_req(struct wl_client*, struct wl_resource* resource, struct wl_resource* /*toplevel*/,
                               struct wl_resource* icon) {
    auto* comp = comp_of(resource);
    if (icon) {
        ++comp->app_.icons_set;
    } else {
        ++comp->app_.icons_cleared;
    }
}

const struct xdg_toplevel_icon_manager_v1_interface icon_manager_impl = {
    .destroy = destroy_req,
    .create_icon = icon_manager_create_icon_req,
    .set_icon = icon_manager_set_icon_req,
};

// --- wp_cursor_shape_manager_v1 ------------------------------------------------------

void cursor_device_set_shape_req(struct wl_client*, struct wl_resource* resource, uint32_t serial,
                                 uint32_t shape) {
    comp_of(resource)->app_.cursor_shape = shape;
    comp_of(resource)->app_.cursor_shape_serial = serial;
}

const struct wp_cursor_shape_device_v1_interface cursor_device_impl = {
    .destroy = destroy_req,
    .set_shape = cursor_device_set_shape_req,
};

void cursor_manager_get_pointer_req(struct wl_client* client, struct wl_resource* resource, uint32_t id,
                                    struct wl_resource* /*pointer*/) {
    auto* res = wl_resource_create(client, &wp_cursor_shape_device_v1_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &cursor_device_impl, comp_of(resource), nullptr);
}

void cursor_manager_get_tablet_req(struct wl_client*, struct wl_resource*, uint32_t, struct wl_resource*) {}

const struct wp_cursor_shape_manager_v1_interface cursor_manager_impl = {
    .destroy = destroy_req,
    .get_pointer = cursor_manager_get_pointer_req,
    .get_tablet_tool_v2 = cursor_manager_get_tablet_req,
};

// --- zxdg_output_manager_v1 ----------------------------------------------------------

const struct zxdg_output_v1_interface xdg_output_impl = {
    .destroy = destroy_req,
};

void xdg_output_destroyed(struct wl_resource* resource) {
    auto* comp = comp_of(resource);
    if (comp->xdg_output_resource_ == resource) comp->xdg_output_resource_ = nullptr;
}

void xdg_output_manager_get_req(struct wl_client* client, struct wl_resource* resource, uint32_t id,
                                struct wl_resource* /*output*/) {
    auto* comp = comp_of(resource);
    auto* res = wl_resource_create(client, &zxdg_output_v1_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &xdg_output_impl, comp, xdg_output_destroyed);
    comp->xdg_output_resource_ = res;
}

const struct zxdg_output_manager_v1_interface xdg_output_manager_impl = {
    .destroy = destroy_req,
    .get_xdg_output = xdg_output_manager_get_req,
};

// --- binds -----------------------------------------------------------------------------

template <const wl_interface* Iface, auto Impl>
void bind_with(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    auto* res = wl_resource_create(client, Iface, static_cast<int>(version), id);
    wl_resource_set_implementation(res, Impl, data, nullptr);
}

void bind_presentation(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    auto* res = wl_resource_create(client, &wp_presentation_interface, static_cast<int>(version), id);
    wl_resource_set_implementation(res, &presentation_impl, data, nullptr);
    wp_presentation_send_clock_id(res, CLOCK_MONOTONIC);
}

void add_state(struct wl_array* arr, uint32_t value) {
    *static_cast<uint32_t*>(wl_array_add(arr, sizeof(uint32_t))) = value;
}

}  // namespace

void HeadlessCompositor::init_app_globals() {
    wl_global_create(display_, &zxdg_decoration_manager_v1_interface, 1, this,
                     bind_with<&zxdg_decoration_manager_v1_interface, &decoration_manager_impl>);
    wl_global_create(display_, &wp_viewporter_interface, 1, this,
                     bind_with<&wp_viewporter_interface, &viewporter_impl>);
    wl_global_create(display_, &wp_fractional_scale_manager_v1_interface, 1, this,
                     bind_with<&wp_fractional_scale_manager_v1_interface, &fractional_manager_impl>);
    wl_global_create(display_, &wp_presentation_interface, 1, this, bind_presentation);
    wl_global_create(display_, &xdg_activation_v1_interface, 1, this,
                     bind_with<&xdg_activation_v1_interface, &activation_impl>);
    wl_global_create(display_, &xdg_toplevel_icon_manager_v1_interface, 1, this,
                     bind_with<&xdg_toplevel_icon_manager_v1_interface, &icon_manager_impl>);
    cursor_shape_global_ = wl_global_create(display_, &wp_cursor_shape_manager_v1_interface, 1, this,
                                            bind_with<&wp_cursor_shape_manager_v1_interface, &cursor_manager_impl>);
    wl_global_create(display_, &zxdg_output_manager_v1_interface, 3, this,
                     bind_with<&zxdg_output_manager_v1_interface, &xdg_output_manager_impl>);
}

uint32_t HeadlessCompositor::configure_window(int32_t width, int32_t height, const std::vector<uint32_t>& states,
                                              int32_t bounds_width, int32_t bounds_height,
                                              const std::vector<uint32_t>& wm_caps, uint32_t decoration_mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!toplevel_resource_ || !xdg_surface_resource_) {
        return 0;
    }
    const int version = wl_resource_get_version(toplevel_resource_);
    if (version >= XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION && (bounds_width || bounds_height)) {
        xdg_toplevel_send_configure_bounds(toplevel_resource_, bounds_width, bounds_height);
    }
    if (version >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION && !wm_caps.empty()) {
        wl_array caps;
        wl_array_init(&caps);
        for (uint32_t c : wm_caps) add_state(&caps, c);
        xdg_toplevel_send_wm_capabilities(toplevel_resource_, &caps);
        wl_array_release(&caps);
    }
    if (decoration_mode && decoration_resource_) {
        zxdg_toplevel_decoration_v1_send_configure(decoration_resource_, decoration_mode);
    }
    wl_array arr;
    wl_array_init(&arr);
    for (uint32_t s : states) add_state(&arr, s);
    xdg_toplevel_send_configure(toplevel_resource_, width, height, &arr);
    wl_array_release(&arr);
    const uint32_t serial = next_serial_++;
    xdg_surface_send_configure(xdg_surface_resource_, serial);
    wl_display_flush_clients(display_);
    return serial;
}

void HeadlessCompositor::close_window() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (toplevel_resource_) {
        xdg_toplevel_send_close(toplevel_resource_);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::send_surface_enter() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (window_surface_ && output_resource_) {
        wl_surface_send_enter(window_surface_, output_resource_);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::send_surface_leave() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (window_surface_ && output_resource_) {
        wl_surface_send_leave(window_surface_, output_resource_);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::send_preferred_buffer_scale(int32_t scale) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (window_surface_ && wl_resource_get_version(window_surface_) >= 6) {
        wl_surface_send_preferred_buffer_scale(window_surface_, scale);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::send_fractional_scale(uint32_t scale120) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fractional_resource_) {
        wp_fractional_scale_v1_send_preferred_scale(fractional_resource_, scale120);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::send_presented(uint64_t time_ns, uint32_t refresh_ns, uint64_t seq, uint32_t flags) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (feedbacks_.empty()) {
        return;
    }
    wl_resource* fb = feedbacks_.front();
    if (output_resource_ && wl_resource_get_client(output_resource_) == wl_resource_get_client(fb)) {
        wp_presentation_feedback_send_sync_output(fb, output_resource_);
    }
    const uint64_t sec = time_ns / 1000000000ull;
    wp_presentation_feedback_send_presented(fb, static_cast<uint32_t>(sec >> 32), static_cast<uint32_t>(sec),
                                            static_cast<uint32_t>(time_ns % 1000000000ull), refresh_ns,
                                            static_cast<uint32_t>(seq >> 32), static_cast<uint32_t>(seq), flags);
    wl_resource_destroy(fb);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::send_discarded() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (feedbacks_.empty()) {
        return;
    }
    wl_resource* fb = feedbacks_.front();
    wp_presentation_feedback_send_discarded(fb);
    wl_resource_destroy(fb);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::send_xdg_output_logical(int32_t x, int32_t y, int32_t width, int32_t height) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (xdg_output_resource_ && output_resource_) {
        zxdg_output_v1_send_logical_position(xdg_output_resource_, x, y);
        zxdg_output_v1_send_logical_size(xdg_output_resource_, width, height);
        wl_output_send_done(output_resource_);
        wl_display_flush_clients(display_);
    }
}

CompositorAppState HeadlessCompositor::app_state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return app_;
}

}  // namespace browl::test
