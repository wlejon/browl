#include "browl/window.h"

#include "browl/display.h"
#include "browl/output.h"
#include "browl/seat.h"
#include "browl/shm_pool.h"

#include "app_globals.h"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "xdg-toplevel-icon-v1-client-protocol.h"

#include <wayland-client.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace browl {

struct Window::Impl {
    // What the client asked for, re-applied whenever the role is (re)created.
    WindowConfig config;
    wl_output* fullscreen_output = nullptr;
    std::vector<uint8_t> icon_rgba;  // straight-alpha RGBA8, empty: no icon
    int32_t icon_size = 0;

    zxdg_toplevel_decoration_v1* decoration = nullptr;
    wp_viewport* viewport = nullptr;
    wp_fractional_scale_v1* fractional = nullptr;
    std::shared_ptr<ShmPool> icon_pool;
    std::shared_ptr<ShmBuffer> icon_buffer;

    // One configure sequence, applied at xdg_surface.configure.
    Size pending_size;
    Size pending_bounds;
    uint32_t pending_states = 0;
    uint32_t pending_caps = wm_capability::All;
    DecorationMode pending_decoration = DecorationMode::Unknown;

    // What the scale is derived from.
    uint32_t fractional120 = 0;
    int32_t preferred_buffer_scale = 0;
    std::vector<wl_output*> entered;

    mutable std::mutex mutex;
    WindowSnapshot snap;
};

namespace {

// --- xdg_surface / xdg_toplevel ----------------------------------------------

static void xdg_surface_handle_configure(void* data, struct xdg_surface* /*surface*/,
                                         uint32_t serial) {
    auto* window = static_cast<Window*>(data);
    if (window) {
        window->handle_xdg_surface_configure(serial);
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_handle_configure,
};

static void toplevel_handle_configure(void* data, struct xdg_toplevel* /*toplevel*/,
                                      int32_t width, int32_t height, struct wl_array* states) {
    auto* window = static_cast<Window*>(data);
    if (window) {
        window->handle_toplevel_configure(width, height, static_cast<const uint32_t*>(states->data),
                                          states->size / sizeof(uint32_t));
    }
}

static void toplevel_handle_close(void* data, struct xdg_toplevel* /*toplevel*/) {
    auto* window = static_cast<Window*>(data);
    if (window) {
        window->handle_toplevel_close();
    }
}

static void toplevel_handle_configure_bounds(void* data, struct xdg_toplevel* /*toplevel*/,
                                             int32_t width, int32_t height) {
    auto* window = static_cast<Window*>(data);
    if (window) {
        window->handle_configure_bounds(width, height);
    }
}

static void toplevel_handle_wm_capabilities(void* data, struct xdg_toplevel* /*toplevel*/,
                                            struct wl_array* caps) {
    auto* window = static_cast<Window*>(data);
    if (window) {
        window->handle_wm_capabilities(static_cast<const uint32_t*>(caps->data),
                                       caps->size / sizeof(uint32_t));
    }
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_handle_configure,
    .close = toplevel_handle_close,
    .configure_bounds = toplevel_handle_configure_bounds,
    .wm_capabilities = toplevel_handle_wm_capabilities,
};

// --- zxdg_toplevel_decoration_v1 ---------------------------------------------

static void decoration_handle_configure(void* data, struct zxdg_toplevel_decoration_v1* /*deco*/,
                                        uint32_t mode) {
    auto* window = static_cast<Window*>(data);
    if (window) {
        window->handle_decoration_mode(mode);
    }
}

static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {
    .configure = decoration_handle_configure,
};

// --- wp_fractional_scale_v1 --------------------------------------------------

static void fractional_handle_preferred_scale(void* data, struct wp_fractional_scale_v1* /*fs*/,
                                              uint32_t scale) {
    auto* window = static_cast<Window*>(data);
    if (window) {
        window->handle_preferred_scale(scale);
    }
}

static const struct wp_fractional_scale_v1_listener fractional_listener = {
    .preferred_scale = fractional_handle_preferred_scale,
};

// --- wl_surface --------------------------------------------------------------

static void surface_handle_enter(void* data, struct wl_surface* /*surface*/,
                                 struct wl_output* output) {
    auto* window = static_cast<Window*>(data);
    if (window) {
        window->handle_surface_enter(output);
    }
}

static void surface_handle_leave(void* data, struct wl_surface* /*surface*/,
                                 struct wl_output* output) {
    auto* window = static_cast<Window*>(data);
    if (window) {
        window->handle_surface_leave(output);
    }
}

static void surface_handle_preferred_buffer_scale(void* data, struct wl_surface* /*surface*/,
                                                  int32_t factor) {
    auto* window = static_cast<Window*>(data);
    if (window) {
        window->handle_preferred_buffer_scale(factor);
    }
}

static void surface_handle_preferred_buffer_transform(void* /*data*/, struct wl_surface* /*surface*/,
                                                      uint32_t /*transform*/) {}

static const struct wl_surface_listener surface_listener = {
    .enter = surface_handle_enter,
    .leave = surface_handle_leave,
    .preferred_buffer_scale = surface_handle_preferred_buffer_scale,
    .preferred_buffer_transform = surface_handle_preferred_buffer_transform,
};

uint32_t state_bit(uint32_t xdg_state) {
    // xdg_toplevel.state: maximized = 1 ... suspended = 9, in window_state's
    // bit order.
    return (xdg_state >= 1 && xdg_state <= 9) ? (1u << (xdg_state - 1)) : 0;
}

uint32_t capability_bit(uint32_t xdg_cap) {
    switch (xdg_cap) {
        case XDG_TOPLEVEL_WM_CAPABILITIES_WINDOW_MENU: return wm_capability::WindowMenu;
        case XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE: return wm_capability::Maximize;
        case XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN: return wm_capability::Fullscreen;
        case XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE: return wm_capability::Minimize;
        default: return 0;
    }
}

}  // namespace

Window::Window(SurfaceId id, wl_surface* surface, xdg_surface* xdg_surf, xdg_toplevel* toplevel,
               const WindowConfig& config, Display* display)
    : id_(id),
      surface_(surface),
      xdg_surf_(xdg_surf),
      toplevel_(toplevel),
      display_(display),
      impl_(std::make_unique<Impl>()) {
    impl_->config = config;
    impl_->snap.id = id_;
    if (!display_ || !surface_) {
        return;
    }
    auto& app = display_->app_globals();
    wl_surface_add_listener(surface_, &surface_listener, this);
    if (app.viewporter) {
        impl_->viewport = wp_viewporter_get_viewport(app.viewporter, surface_);
    }
    if (app.fractional_scale_manager) {
        impl_->fractional =
            wp_fractional_scale_manager_v1_get_fractional_scale(app.fractional_scale_manager, surface_);
        wp_fractional_scale_v1_add_listener(impl_->fractional, &fractional_listener, this);
    }
    app.register_window(this);
    if (xdg_surf_ && toplevel_) {
        attach_role();
    }
}

Window::~Window() {
    detach();
}

void Window::detach() {
    if (!display_) {
        return;
    }
    display_->app_globals().unregister_window(this);
    destroy_role();
    if (impl_->fractional) {
        wp_fractional_scale_v1_destroy(impl_->fractional);
        impl_->fractional = nullptr;
    }
    if (impl_->viewport) {
        wp_viewport_destroy(impl_->viewport);
        impl_->viewport = nullptr;
    }
    impl_->icon_buffer.reset();
    impl_->icon_pool.reset();
    if (surface_) {
        wl_surface_destroy(surface_);
        surface_ = nullptr;
    }
    display_ = nullptr;
}

void Window::attach_role() {
    auto& app = display_->app_globals();
    const WindowConfig& c = impl_->config;
    xdg_surface_add_listener(xdg_surf_, &xdg_surface_listener, this);
    xdg_toplevel_add_listener(toplevel_, &toplevel_listener, this);
    if (!c.title.empty()) {
        xdg_toplevel_set_title(toplevel_, c.title.c_str());
    }
    if (!c.app_id.empty()) {
        xdg_toplevel_set_app_id(toplevel_, c.app_id.c_str());
    }
    if (c.min_size.width > 0 || c.min_size.height > 0) {
        xdg_toplevel_set_min_size(toplevel_, c.min_size.width, c.min_size.height);
    }
    if (c.max_size.width > 0 || c.max_size.height > 0) {
        xdg_toplevel_set_max_size(toplevel_, c.max_size.width, c.max_size.height);
    }
    if (c.maximized) {
        xdg_toplevel_set_maximized(toplevel_);
    }
    if (c.fullscreen) {
        xdg_toplevel_set_fullscreen(toplevel_, impl_->fullscreen_output);
    }
    if (app.decoration_manager) {
        impl_->decoration =
            zxdg_decoration_manager_v1_get_toplevel_decoration(app.decoration_manager, toplevel_);
        zxdg_toplevel_decoration_v1_add_listener(impl_->decoration, &decoration_listener, this);
        zxdg_toplevel_decoration_v1_set_mode(
            impl_->decoration, c.server_side_decorations
                                   ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
                                   : ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
    }
    if (!impl_->icon_rgba.empty()) {
        apply_icon();
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->pending_caps = wm_capability::All;
    }
    // The initial commit: no buffer, the compositor answers with a configure.
    wl_surface_commit(surface_);
}

void Window::destroy_role() {
    if (impl_->decoration) {
        zxdg_toplevel_decoration_v1_destroy(impl_->decoration);
        impl_->decoration = nullptr;
    }
    if (toplevel_) {
        xdg_toplevel_destroy(toplevel_);
        toplevel_ = nullptr;
    }
    if (xdg_surf_) {
        xdg_surface_destroy(xdg_surf_);
        xdg_surf_ = nullptr;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->snap.configured = false;
}

WindowSnapshot Window::snapshot() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->snap;
}

bool Window::mapped() const {
    return toplevel_ != nullptr;
}

void Window::map() {
    if (!display_ || !surface_ || toplevel_ || !display_->xdg_wm_base_ptr()) {
        return;
    }
    // A buffer attached while the surface had no role (a swapchain's) must
    // not reach the new role before its first configure.
    wl_surface_attach(surface_, nullptr, 0, 0);
    wl_surface_commit(surface_);
    xdg_surf_ = xdg_wm_base_get_xdg_surface(display_->xdg_wm_base_ptr(), surface_);
    toplevel_ = xdg_surface_get_toplevel(xdg_surf_);
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->snap.close_requested = false;
    }
    attach_role();
}

void Window::unmap() {
    if (!display_ || !surface_ || !toplevel_) {
        return;
    }
    wl_surface_attach(surface_, nullptr, 0, 0);
    wl_surface_commit(surface_);
    destroy_role();
}

void Window::set_title(const std::string& title) {
    impl_->config.title = title;
    if (toplevel_) {
        xdg_toplevel_set_title(toplevel_, title.c_str());
    }
}

void Window::set_app_id(const std::string& app_id) {
    impl_->config.app_id = app_id;
    if (toplevel_) {
        xdg_toplevel_set_app_id(toplevel_, app_id.c_str());
    }
}

void Window::set_min_size(int32_t width, int32_t height) {
    impl_->config.min_size = {width, height};
    if (toplevel_) {
        xdg_toplevel_set_min_size(toplevel_, width, height);
    }
}

void Window::set_max_size(int32_t width, int32_t height) {
    impl_->config.max_size = {width, height};
    if (toplevel_) {
        xdg_toplevel_set_max_size(toplevel_, width, height);
    }
}

void Window::set_maximized(bool maximized) {
    impl_->config.maximized = maximized;
    if (toplevel_) {
        if (maximized) {
            xdg_toplevel_set_maximized(toplevel_);
        } else {
            xdg_toplevel_unset_maximized(toplevel_);
        }
    }
}

void Window::set_fullscreen(bool fullscreen, Output* output) {
    impl_->config.fullscreen = fullscreen;
    impl_->fullscreen_output = (fullscreen && output) ? output->wl_output_ptr() : nullptr;
    if (toplevel_) {
        if (fullscreen) {
            xdg_toplevel_set_fullscreen(toplevel_, impl_->fullscreen_output);
        } else {
            xdg_toplevel_unset_fullscreen(toplevel_);
        }
    }
}

void Window::set_minimized() {
    if (toplevel_) {
        xdg_toplevel_set_minimized(toplevel_);
    }
}

void Window::set_server_side_decorations(bool server_side) {
    impl_->config.server_side_decorations = server_side;
    if (impl_->decoration) {
        zxdg_toplevel_decoration_v1_set_mode(impl_->decoration,
                                             server_side ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
                                                         : ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
    }
}

bool Window::has_icon_protocol() const {
    return display_ && display_->app_globals().toplevel_icon_manager && display_->wl_shm_ptr();
}

bool Window::set_icon(int32_t width, int32_t height, const uint8_t* rgba) {
    if (!has_icon_protocol()) {
        return false;
    }
    if (width == 0 && height == 0) {
        impl_->icon_rgba.clear();
        impl_->icon_size = 0;
        if (toplevel_) {
            xdg_toplevel_icon_manager_v1_set_icon(display_->app_globals().toplevel_icon_manager,
                                                  toplevel_, nullptr);
        }
        return true;
    }
    if (width <= 0 || width != height || !rgba || width > 4096) {
        return false;
    }
    impl_->icon_size = width;
    impl_->icon_rgba.assign(rgba, rgba + size_t(width) * size_t(height) * 4);
    return toplevel_ ? apply_icon() : true;
}

bool Window::apply_icon() {
    auto& app = display_->app_globals();
    const int32_t n = impl_->icon_size;
    auto pool = display_->create_shm_pool(size_t(n) * size_t(n) * 4);
    if (!pool) {
        return false;
    }
    auto buffer = pool->allocate_buffer(n, n, n * 4, WL_SHM_FORMAT_ARGB8888);
    if (!buffer) {
        return false;
    }
    // Straight RGBA bytes to premultiplied ARGB8888 words (B, G, R, A in memory).
    const uint8_t* src = impl_->icon_rgba.data();
    auto* dst = static_cast<uint8_t*>(buffer->data());
    for (size_t i = 0; i < size_t(n) * size_t(n); ++i) {
        const uint32_t a = src[i * 4 + 3];
        dst[i * 4 + 0] = static_cast<uint8_t>((src[i * 4 + 2] * a + 127) / 255);
        dst[i * 4 + 1] = static_cast<uint8_t>((src[i * 4 + 1] * a + 127) / 255);
        dst[i * 4 + 2] = static_cast<uint8_t>((src[i * 4 + 0] * a + 127) / 255);
        dst[i * 4 + 3] = static_cast<uint8_t>(a);
    }
    xdg_toplevel_icon_v1* icon = xdg_toplevel_icon_manager_v1_create_icon(app.toplevel_icon_manager);
    xdg_toplevel_icon_v1_add_buffer(icon, buffer->wl_buffer_ptr(), 1);
    xdg_toplevel_icon_manager_v1_set_icon(app.toplevel_icon_manager, toplevel_, icon);
    // The toplevel keeps the icon after its object is destroyed; the buffer
    // is kept until the next icon replaces it.
    xdg_toplevel_icon_v1_destroy(icon);
    impl_->icon_pool = std::move(pool);
    impl_->icon_buffer = std::move(buffer);
    return true;
}

bool Window::set_logical_size(int32_t width, int32_t height) {
    if (!impl_->viewport) {
        return false;
    }
    if (width > 0 && height > 0) {
        wp_viewport_set_destination(impl_->viewport, width, height);
    } else {
        wp_viewport_set_destination(impl_->viewport, -1, -1);
    }
    return true;
}

void Window::set_buffer_scale(int32_t scale) {
    if (surface_ && scale > 0 && wl_surface_get_version(surface_) >= WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION) {
        wl_surface_set_buffer_scale(surface_, scale);
    }
}

void Window::set_window_geometry(const Rect& rect) {
    if (xdg_surf_ && rect.width > 0 && rect.height > 0) {
        xdg_surface_set_window_geometry(xdg_surf_, rect.x, rect.y, rect.width, rect.height);
    }
}

void Window::start_move(Seat& seat, uint32_t serial) {
    if (toplevel_ && seat.wl_seat_ptr()) {
        xdg_toplevel_move(toplevel_, seat.wl_seat_ptr(), serial);
    }
}

void Window::start_resize(Seat& seat, uint32_t serial, ResizeEdge edges) {
    if (toplevel_ && seat.wl_seat_ptr()) {
        xdg_toplevel_resize(toplevel_, seat.wl_seat_ptr(), serial, static_cast<uint32_t>(edges));
    }
}

void Window::show_window_menu(Seat& seat, uint32_t serial, int32_t x, int32_t y) {
    if (toplevel_ && seat.wl_seat_ptr()) {
        xdg_toplevel_show_window_menu(toplevel_, seat.wl_seat_ptr(), serial, x, y);
    }
}

void Window::attach_buffer(wl_buffer* buffer, int32_t x, int32_t y) {
    if (surface_) {
        wl_surface_attach(surface_, buffer, x, y);
    }
}

void Window::damage(int32_t x, int32_t y, int32_t width, int32_t height) {
    if (surface_) {
        if (wl_surface_get_version(surface_) >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION) {
            wl_surface_damage_buffer(surface_, x, y, width, height);
        } else {
            wl_surface_damage(surface_, x, y, width, height);
        }
    }
}

void Window::commit() {
    if (surface_) {
        wl_surface_commit(surface_);
    }
}

// --- listeners ---------------------------------------------------------------

void Window::handle_toplevel_configure(int32_t width, int32_t height, const uint32_t* states,
                                       size_t count) {
    uint32_t bits = 0;
    for (size_t i = 0; i < count; ++i) {
        bits |= state_bit(states[i]);
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->pending_size = {width, height};
    impl_->pending_states = bits;
}

void Window::handle_toplevel_close() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->snap.close_requested = true;
    }
    if (display_) {
        display_->events().push(WindowCloseEvent{id_});
    }
}

void Window::handle_configure_bounds(int32_t width, int32_t height) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->pending_bounds = {width, height};
}

void Window::handle_wm_capabilities(const uint32_t* caps, size_t count) {
    uint32_t bits = 0;
    for (size_t i = 0; i < count; ++i) {
        bits |= capability_bit(caps[i]);
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->pending_caps = bits;
}

void Window::handle_decoration_mode(uint32_t mode) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->pending_decoration = mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
                                    ? DecorationMode::ServerSide
                                    : DecorationMode::ClientSide;
}

void Window::handle_xdg_surface_configure(uint32_t serial) {
    WindowSnapshot snap;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->snap.configured_size = impl_->pending_size;
        impl_->snap.bounds = impl_->pending_bounds;
        impl_->snap.states = impl_->pending_states;
        impl_->snap.wm_capabilities = impl_->pending_caps;
        impl_->snap.decoration = impl_->decoration ? impl_->pending_decoration : DecorationMode::Unknown;
        impl_->snap.configured = true;
        snap = impl_->snap;
    }
    if (xdg_surf_) {
        xdg_surface_ack_configure(xdg_surf_, serial);
    }
    if (display_) {
        display_->events().push(WindowConfigureEvent{id_, serial, std::move(snap)});
    }
}

void Window::handle_preferred_scale(uint32_t scale120) {
    impl_->fractional120 = scale120;
    update_scale();
}

void Window::handle_preferred_buffer_scale(int32_t factor) {
    impl_->preferred_buffer_scale = factor;
    update_scale();
}

void Window::handle_surface_enter(void* wl_output_ptr) {
    auto* out = static_cast<wl_output*>(wl_output_ptr);
    auto& entered = impl_->entered;
    if (std::find(entered.begin(), entered.end(), out) == entered.end()) {
        entered.push_back(out);
        publish_outputs();
    }
    update_scale();
}

void Window::handle_surface_leave(void* wl_output_ptr) {
    auto* out = static_cast<wl_output*>(wl_output_ptr);
    auto& entered = impl_->entered;
    auto it = std::find(entered.begin(), entered.end(), out);
    if (it != entered.end()) {
        entered.erase(it);
        publish_outputs();
    }
    update_scale();
}

void Window::publish_outputs() {
    if (!display_) {
        return;
    }
    std::vector<OutputId> ids;
    for (wl_output* o : impl_->entered) {
        ids.push_back(display_->app_globals().output_id(o));
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->snap.outputs = ids;
    }
    display_->events().push(WindowOutputsEvent{id_, std::move(ids)});
}

void Window::update_scale() {
    if (!display_) {
        return;
    }
    uint32_t scale120 = 120;
    if (impl_->fractional120 > 0) {
        scale120 = impl_->fractional120;
    } else if (impl_->preferred_buffer_scale > 0) {
        scale120 = static_cast<uint32_t>(impl_->preferred_buffer_scale) * 120;
    } else if (!impl_->entered.empty()) {
        int32_t s = 1;
        for (wl_output* o : impl_->entered) {
            s = std::max(s, display_->app_globals().output_scale(o));
        }
        scale120 = static_cast<uint32_t>(s) * 120;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->snap.scale120 == scale120) {
            return;
        }
        impl_->snap.scale120 = scale120;
    }
    display_->events().push(WindowScaleEvent{id_, scale120});
}

}  // namespace browl
