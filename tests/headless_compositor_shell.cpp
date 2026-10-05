#include "headless_compositor.h"

#include <wayland-server-core.h>
#include <wayland-server.h>

#include "wlr-foreign-toplevel-management-unstable-v1-server-protocol.h"
#include "wlr-layer-shell-unstable-v1-server-protocol.h"
#include "xdg-shell-server-protocol.h"

namespace browl::test {

namespace {

// ============================================================================
// Layer Shell Implementation
// ============================================================================

static void layer_surface_set_size_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                       uint32_t /*width*/, uint32_t /*height*/) {}

static void layer_surface_set_anchor_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                         uint32_t /*anchor*/) {}

static void layer_surface_set_exclusive_zone_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                                 int32_t /*zone*/) {}

static void layer_surface_set_margin_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                         int32_t /*top*/, int32_t /*right*/, int32_t /*bottom*/, int32_t /*left*/) {}

static void layer_surface_set_keyboard_interactivity_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                                         uint32_t /*interactivity*/) {}

static void layer_surface_get_popup_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                        struct wl_resource* /*popup*/) {}

static void layer_surface_ack_configure_req(struct wl_client* /*client*/, struct wl_resource* resource,
                                            uint32_t serial) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    if (comp) {
        comp->last_layer_ack_serial_ = serial;
    }
}

static void layer_surface_destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static void layer_surface_set_layer_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                        uint32_t /*layer*/) {}

static const struct zwlr_layer_surface_v1_interface layer_surface_impl = {
    .set_size = layer_surface_set_size_req,
    .set_anchor = layer_surface_set_anchor_req,
    .set_exclusive_zone = layer_surface_set_exclusive_zone_req,
    .set_margin = layer_surface_set_margin_req,
    .set_keyboard_interactivity = layer_surface_set_keyboard_interactivity_req,
    .get_popup = layer_surface_get_popup_req,
    .ack_configure = layer_surface_ack_configure_req,
    .destroy = layer_surface_destroy_req,
    .set_layer = layer_surface_set_layer_req,
};

static void layer_shell_get_layer_surface(struct wl_client* client, struct wl_resource* resource,
                                          uint32_t id, struct wl_resource* /*surface*/,
                                          struct wl_resource* /*output*/, uint32_t /*layer*/,
                                          const char* /*scope*/) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    struct wl_resource* res =
        wl_resource_create(client, &zwlr_layer_surface_v1_interface,
                           wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &layer_surface_impl, comp, nullptr);
    if (comp) {
        comp->layer_surface_resource_ = res;
    }
}

static void layer_shell_destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static const struct zwlr_layer_shell_v1_interface layer_shell_impl = {
    .get_layer_surface = layer_shell_get_layer_surface,
    .destroy = layer_shell_destroy_req,
};

static void bind_layer_shell(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    struct wl_resource* res =
        wl_resource_create(client, &zwlr_layer_shell_v1_interface, version, id);
    wl_resource_set_implementation(res, &layer_shell_impl, data, nullptr);
}

// ============================================================================
// XDG Shell Implementation
// ============================================================================

static void xdg_positioner_destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static void xdg_positioner_set_size_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                        int32_t /*width*/, int32_t /*height*/) {}

static void xdg_positioner_set_anchor_rect_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                               int32_t /*x*/, int32_t /*y*/, int32_t /*width*/, int32_t /*height*/) {}

static void xdg_positioner_set_anchor_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                          uint32_t /*anchor*/) {}

static void xdg_positioner_set_gravity_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                           uint32_t /*gravity*/) {}

static void xdg_positioner_set_constraint_adjustment_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                                         uint32_t /*adjustment*/) {}

static void xdg_positioner_set_offset_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                          int32_t /*x*/, int32_t /*y*/) {}

static void xdg_positioner_set_reactive_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/) {}

static void xdg_positioner_set_parent_size_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                               int32_t /*width*/, int32_t /*height*/) {}

static void xdg_positioner_set_parent_configure_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                                    uint32_t /*serial*/) {}

static const struct xdg_positioner_interface positioner_impl = {
    .destroy = xdg_positioner_destroy_req,
    .set_size = xdg_positioner_set_size_req,
    .set_anchor_rect = xdg_positioner_set_anchor_rect_req,
    .set_anchor = xdg_positioner_set_anchor_req,
    .set_gravity = xdg_positioner_set_gravity_req,
    .set_constraint_adjustment = xdg_positioner_set_constraint_adjustment_req,
    .set_offset = xdg_positioner_set_offset_req,
    .set_reactive = xdg_positioner_set_reactive_req,
    .set_parent_size = xdg_positioner_set_parent_size_req,
    .set_parent_configure = xdg_positioner_set_parent_configure_req,
};

static void xdg_popup_destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static void xdg_popup_grab_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                               struct wl_resource* /*seat*/, uint32_t /*serial*/) {}

static void xdg_popup_reposition_req(struct wl_client* /*client*/, struct wl_resource* resource,
                                     struct wl_resource* /*positioner*/, uint32_t token) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    if (comp) {
        comp->popup_repositioned_received_ = true;
        xdg_popup_send_repositioned(resource, token);
    }
}

static const struct xdg_popup_interface popup_impl = {
    .destroy = xdg_popup_destroy_req,
    .grab = xdg_popup_grab_req,
    .reposition = xdg_popup_reposition_req,
};

static void xdg_surface_destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static void xdg_surface_get_toplevel_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                         uint32_t /*id*/) {}

static void xdg_surface_get_popup_req(struct wl_client* client, struct wl_resource* resource,
                                      uint32_t id, struct wl_resource* /*parent*/,
                                      struct wl_resource* /*positioner*/) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    struct wl_resource* res =
        wl_resource_create(client, &xdg_popup_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &popup_impl, comp, nullptr);
    if (comp) {
        comp->popup_resource_ = res;
    }
}

static void xdg_surface_set_window_geometry_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                                int32_t /*x*/, int32_t /*y*/, int32_t /*width*/, int32_t /*height*/) {}

static void xdg_surface_ack_configure_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                          uint32_t /*serial*/) {}

static const struct xdg_surface_interface xdg_surface_impl = {
    .destroy = xdg_surface_destroy_req,
    .get_toplevel = xdg_surface_get_toplevel_req,
    .get_popup = xdg_surface_get_popup_req,
    .set_window_geometry = xdg_surface_set_window_geometry_req,
    .ack_configure = xdg_surface_ack_configure_req,
};

static void xdg_wm_base_destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static void xdg_wm_base_create_positioner_req(struct wl_client* client, struct wl_resource* resource,
                                              uint32_t id) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    struct wl_resource* res =
        wl_resource_create(client, &xdg_positioner_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &positioner_impl, comp, nullptr);
    if (comp) {
        comp->positioner_resource_ = res;
    }
}

static void xdg_wm_base_get_xdg_surface_req(struct wl_client* client, struct wl_resource* resource,
                                            uint32_t id, struct wl_resource* /*surface*/) {
    auto* comp = static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
    struct wl_resource* res =
        wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &xdg_surface_impl, comp, nullptr);
}

static void xdg_wm_base_pong_req(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                 uint32_t /*serial*/) {}

static const struct xdg_wm_base_interface xdg_wm_base_impl = {
    .destroy = xdg_wm_base_destroy_req,
    .create_positioner = xdg_wm_base_create_positioner_req,
    .get_xdg_surface = xdg_wm_base_get_xdg_surface_req,
    .pong = xdg_wm_base_pong_req,
};

static void bind_xdg_wm_base(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    struct wl_resource* res = wl_resource_create(client, &xdg_wm_base_interface, version, id);
    wl_resource_set_implementation(res, &xdg_wm_base_impl, data, nullptr);
}

// ============================================================================
// Foreign Toplevel Implementation
// ============================================================================

static void foreign_toplevel_handle_set_maximized(struct wl_client* /*client*/, struct wl_resource* resource) {
    auto* top = static_cast<CompositorToplevelState*>(wl_resource_get_user_data(resource));
    if (top) top->maximized = true;
}

static void foreign_toplevel_handle_unset_maximized(struct wl_client* /*client*/, struct wl_resource* resource) {
    auto* top = static_cast<CompositorToplevelState*>(wl_resource_get_user_data(resource));
    if (top) top->maximized = false;
}

static void foreign_toplevel_handle_set_minimized(struct wl_client* /*client*/, struct wl_resource* resource) {
    auto* top = static_cast<CompositorToplevelState*>(wl_resource_get_user_data(resource));
    if (top) top->minimized = true;
}

static void foreign_toplevel_handle_unset_minimized(struct wl_client* /*client*/, struct wl_resource* resource) {
    auto* top = static_cast<CompositorToplevelState*>(wl_resource_get_user_data(resource));
    if (top) top->minimized = false;
}

static void foreign_toplevel_handle_activate(struct wl_client* /*client*/, struct wl_resource* resource,
                                             struct wl_resource* /*seat*/) {
    auto* top = static_cast<CompositorToplevelState*>(wl_resource_get_user_data(resource));
    if (top) top->activated = true;
}

static void foreign_toplevel_handle_close(struct wl_client* /*client*/, struct wl_resource* resource) {
    auto* top = static_cast<CompositorToplevelState*>(wl_resource_get_user_data(resource));
    if (top) top->closed = true;
}

static void foreign_toplevel_handle_set_rectangle(struct wl_client* /*client*/, struct wl_resource* /*resource*/,
                                                  struct wl_resource* /*surface*/, int32_t /*x*/, int32_t /*y*/,
                                                  int32_t /*width*/, int32_t /*height*/) {}

static void foreign_toplevel_handle_destroy(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static void foreign_toplevel_handle_set_fullscreen(struct wl_client* /*client*/, struct wl_resource* resource,
                                                   struct wl_resource* /*output*/) {
    auto* top = static_cast<CompositorToplevelState*>(wl_resource_get_user_data(resource));
    if (top) top->fullscreen = true;
}

static void foreign_toplevel_handle_unset_fullscreen(struct wl_client* /*client*/, struct wl_resource* resource) {
    auto* top = static_cast<CompositorToplevelState*>(wl_resource_get_user_data(resource));
    if (top) top->fullscreen = false;
}

static const struct zwlr_foreign_toplevel_handle_v1_interface toplevel_handle_impl = {
    .set_maximized = foreign_toplevel_handle_set_maximized,
    .unset_maximized = foreign_toplevel_handle_unset_maximized,
    .set_minimized = foreign_toplevel_handle_set_minimized,
    .unset_minimized = foreign_toplevel_handle_unset_minimized,
    .activate = foreign_toplevel_handle_activate,
    .close = foreign_toplevel_handle_close,
    .set_rectangle = foreign_toplevel_handle_set_rectangle,
    .destroy = foreign_toplevel_handle_destroy,
    .set_fullscreen = foreign_toplevel_handle_set_fullscreen,
    .unset_fullscreen = foreign_toplevel_handle_unset_fullscreen,
};

static void foreign_toplevel_manager_stop(struct wl_client* /*client*/, struct wl_resource* /*resource*/) {}

static const struct zwlr_foreign_toplevel_manager_v1_interface toplevel_manager_impl = {
    .stop = foreign_toplevel_manager_stop,
};

static void bind_foreign_toplevel_manager(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    auto* comp = static_cast<HeadlessCompositor*>(data);
    struct wl_resource* res =
        wl_resource_create(client, &zwlr_foreign_toplevel_manager_v1_interface, version, id);
    wl_resource_set_implementation(res, &toplevel_manager_impl, data, nullptr);
    comp->toplevel_manager_resource_ = res;
    if (!comp->client_) {
        comp->client_ = client;
    }
}

}  // namespace

void HeadlessCompositor::init_shell_globals() {
    layer_shell_global_ = wl_global_create(
        display_, &zwlr_layer_shell_v1_interface, 4, this, bind_layer_shell);
    xdg_wm_base_global_ = wl_global_create(
        display_, &xdg_wm_base_interface, 5, this, bind_xdg_wm_base);
    foreign_toplevel_global_ = wl_global_create(
        display_, &zwlr_foreign_toplevel_manager_v1_interface, 3, this, bind_foreign_toplevel_manager);
}

void HeadlessCompositor::configure_layer_surface(uint32_t width, uint32_t height) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (layer_surface_resource_) {
        uint32_t serial = next_serial_++;
        zwlr_layer_surface_v1_send_configure(layer_surface_resource_, serial, width, height);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::close_layer_surface() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (layer_surface_resource_) {
        zwlr_layer_surface_v1_send_closed(layer_surface_resource_);
        wl_display_flush_clients(display_);
    }
}

bool HeadlessCompositor::layer_surface_created() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return layer_surface_resource_ != nullptr;
}

uint32_t HeadlessCompositor::last_layer_ack_serial() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_layer_ack_serial_;
}

bool HeadlessCompositor::layer_surface_committed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return layer_surface_committed_;
}

void HeadlessCompositor::configure_popup(int32_t x, int32_t y, int32_t width, int32_t height) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (popup_resource_) {
        xdg_popup_send_configure(popup_resource_, x, y, width, height);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::send_popup_done() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (popup_resource_) {
        xdg_popup_send_popup_done(popup_resource_);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::send_popup_repositioned(uint32_t token) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (popup_resource_) {
        xdg_popup_send_repositioned(popup_resource_, token);
        wl_display_flush_clients(display_);
    }
}

bool HeadlessCompositor::popup_created() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return popup_resource_ != nullptr;
}

bool HeadlessCompositor::popup_repositioned_received() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return popup_repositioned_received_;
}

CompositorToplevelState* HeadlessCompositor::create_foreign_toplevel(const std::string& title,
                                                                   const std::string& app_id,
                                                                   uint32_t state) {
    std::lock_guard<std::mutex> lock(mutex_);
    struct wl_client* target_client = client_;
    if (!target_client && toplevel_manager_resource_) {
        target_client = wl_resource_get_client(toplevel_manager_resource_);
    }
    if (!toplevel_manager_resource_ || !target_client) {
        return nullptr;
    }

    auto top = std::make_unique<CompositorToplevelState>();
    top->title = title;
    top->app_id = app_id;
    top->state = state;

    struct wl_resource* handle_res =
        wl_resource_create(target_client, &zwlr_foreign_toplevel_handle_v1_interface,
                           wl_resource_get_version(toplevel_manager_resource_), 0);
    wl_resource_set_implementation(handle_res, &toplevel_handle_impl, top.get(), nullptr);
    top->resource = handle_res;

    zwlr_foreign_toplevel_manager_v1_send_toplevel(toplevel_manager_resource_, handle_res);
    zwlr_foreign_toplevel_handle_v1_send_title(handle_res, title.c_str());
    zwlr_foreign_toplevel_handle_v1_send_app_id(handle_res, app_id.c_str());

    struct wl_array state_arr;
    wl_array_init(&state_arr);
    if (state & 1) { // Maximized
        uint32_t* p = static_cast<uint32_t*>(wl_array_add(&state_arr, sizeof(uint32_t)));
        *p = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED;
    }
    if (state & 2) { // Minimized
        uint32_t* p = static_cast<uint32_t*>(wl_array_add(&state_arr, sizeof(uint32_t)));
        *p = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
    }
    if (state & 4) { // Activated
        uint32_t* p = static_cast<uint32_t*>(wl_array_add(&state_arr, sizeof(uint32_t)));
        *p = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
    }
    if (state & 8) { // Fullscreen
        uint32_t* p = static_cast<uint32_t*>(wl_array_add(&state_arr, sizeof(uint32_t)));
        *p = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN;
    }
    zwlr_foreign_toplevel_handle_v1_send_state(handle_res, &state_arr);
    wl_array_release(&state_arr);

    zwlr_foreign_toplevel_handle_v1_send_done(handle_res);
    wl_display_flush_clients(display_);

    CompositorToplevelState* ptr = top.get();
    toplevels_.push_back(std::move(top));
    return ptr;
}

void HeadlessCompositor::update_toplevel_title(CompositorToplevelState* top, const std::string& title) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (top && top->resource) {
        top->title = title;
        zwlr_foreign_toplevel_handle_v1_send_title(top->resource, title.c_str());
        zwlr_foreign_toplevel_handle_v1_send_done(top->resource);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::update_toplevel_app_id(CompositorToplevelState* top, const std::string& app_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (top && top->resource) {
        top->app_id = app_id;
        zwlr_foreign_toplevel_handle_v1_send_app_id(top->resource, app_id.c_str());
        zwlr_foreign_toplevel_handle_v1_send_done(top->resource);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::update_toplevel_state(CompositorToplevelState* top, uint32_t state) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (top && top->resource) {
        top->state = state;
        struct wl_array state_arr;
        wl_array_init(&state_arr);
        if (state & 1) {
            uint32_t* p = static_cast<uint32_t*>(wl_array_add(&state_arr, sizeof(uint32_t)));
            *p = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED;
        }
        if (state & 2) {
            uint32_t* p = static_cast<uint32_t*>(wl_array_add(&state_arr, sizeof(uint32_t)));
            *p = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
        }
        if (state & 4) {
            uint32_t* p = static_cast<uint32_t*>(wl_array_add(&state_arr, sizeof(uint32_t)));
            *p = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
        }
        if (state & 8) {
            uint32_t* p = static_cast<uint32_t*>(wl_array_add(&state_arr, sizeof(uint32_t)));
            *p = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN;
        }
        zwlr_foreign_toplevel_handle_v1_send_state(top->resource, &state_arr);
        wl_array_release(&state_arr);
        zwlr_foreign_toplevel_handle_v1_send_done(top->resource);
        wl_display_flush_clients(display_);
    }
}

void HeadlessCompositor::close_toplevel(CompositorToplevelState* top) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (top && top->resource) {
        zwlr_foreign_toplevel_handle_v1_send_closed(top->resource);
        wl_display_flush_clients(display_);
    }
}

}  // namespace browl::test
