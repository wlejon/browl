// Display's application-window half: the globals in Display::AppGlobals,
// windows, surface ids, activation tokens and presentation feedback.
#include "app_globals.h"

#include "browl/output.h"
#include "browl/seat.h"
#include "browl/window.h"

#include "cursor-shape-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "presentation-time-client-protocol.h"
#include "primary-selection-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-activation-v1-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "xdg-toplevel-icon-v1-client-protocol.h"

#include <wayland-client.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace browl {

struct ActivationRequest {
    Display::AppGlobals* app = nullptr;
    xdg_activation_token_v1* token = nullptr;
    RequestId id = 0;
};

struct FeedbackRequest {
    Display::AppGlobals* app = nullptr;
    // `struct`: the generated request function wp_presentation_feedback()
    // hides the type's plain name.
    struct wp_presentation_feedback* feedback = nullptr;
    RequestId id = 0;
    SurfaceId surface_id = kNoSurface;
    OutputId output = kNoOutput;
};

struct FrameCallbackRequest {
    Display::AppGlobals* app = nullptr;
    wl_callback* callback = nullptr;
    RequestId id = 0;
    SurfaceId surface_id = kNoSurface;
};

namespace {

// --- wl_surface.frame --------------------------------------------------------

static void frame_callback_handle_done(void* data, wl_callback* /*cb*/, uint32_t time_ms) {
    auto* req = static_cast<FrameCallbackRequest*>(data);
    if (!req || !req->app) {
        return;
    }
    FrameDoneEvent ev;
    ev.request = req->id;
    ev.surface_id = req->surface_id;
    ev.time_ms = time_ms;
    Display::AppGlobals* app = req->app;
    app->display->events().push(std::move(ev));
    app->forget_frame_callback(req);
}

static const struct wl_callback_listener frame_callback_listener = {
    .done = frame_callback_handle_done,
};

// --- wp_presentation ---------------------------------------------------------

static void presentation_handle_clock_id(void* data, struct wp_presentation* /*presentation*/,
                                         uint32_t clk_id) {
    auto* app = static_cast<Display::AppGlobals*>(data);
    if (app) {
        app->presentation_clock_id.store(static_cast<int>(clk_id));
    }
}

static const struct wp_presentation_listener presentation_listener = {
    .clock_id = presentation_handle_clock_id,
};

static void feedback_handle_sync_output(void* data, struct wp_presentation_feedback* /*fb*/,
                                        struct wl_output* output) {
    auto* req = static_cast<FeedbackRequest*>(data);
    if (req && req->app) {
        req->output = req->app->output_id(output);
    }
}

static void feedback_handle_presented(void* data, struct wp_presentation_feedback* /*fb*/,
                                      uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec,
                                      uint32_t refresh, uint32_t seq_hi, uint32_t seq_lo,
                                      uint32_t flags) {
    auto* req = static_cast<FeedbackRequest*>(data);
    if (!req || !req->app) {
        return;
    }
    PresentationFeedbackEvent ev;
    ev.request = req->id;
    ev.surface_id = req->surface_id;
    ev.presented = true;
    const uint64_t sec = (static_cast<uint64_t>(tv_sec_hi) << 32) | tv_sec_lo;
    ev.time_ns = sec * 1000000000ull + tv_nsec;
    ev.refresh_ns = refresh;
    ev.sequence = (static_cast<uint64_t>(seq_hi) << 32) | seq_lo;
    // wp_presentation_feedback.kind has the same bit order as presentation_flags.
    ev.flags = flags & (presentation_flags::Vsync | presentation_flags::HwClock |
                        presentation_flags::HwCompletion | presentation_flags::ZeroCopy);
    ev.output = req->output;
    Display::AppGlobals* app = req->app;
    app->display->events().push(std::move(ev));
    app->forget_feedback(req);
}

static void feedback_handle_discarded(void* data, struct wp_presentation_feedback* /*fb*/) {
    auto* req = static_cast<FeedbackRequest*>(data);
    if (!req || !req->app) {
        return;
    }
    PresentationFeedbackEvent ev;
    ev.request = req->id;
    ev.surface_id = req->surface_id;
    ev.presented = false;
    Display::AppGlobals* app = req->app;
    app->display->events().push(std::move(ev));
    app->forget_feedback(req);
}

static const struct wp_presentation_feedback_listener feedback_listener = {
    .sync_output = feedback_handle_sync_output,
    .presented = feedback_handle_presented,
    .discarded = feedback_handle_discarded,
};

// --- xdg_activation_token_v1 -------------------------------------------------

static void activation_token_handle_done(void* data, struct xdg_activation_token_v1* /*token*/,
                                         const char* token) {
    auto* req = static_cast<ActivationRequest*>(data);
    if (!req || !req->app) {
        return;
    }
    Display::AppGlobals* app = req->app;
    app->display->events().push(ActivationTokenEvent{req->id, token ? token : ""});
    app->forget_activation(req);
}

static const struct xdg_activation_token_v1_listener activation_token_listener = {
    .done = activation_token_handle_done,
};

template <class T>
T* bind_global(wl_registry* registry, uint32_t name, const wl_interface* iface, uint32_t version,
               uint32_t max_version) {
    return static_cast<T*>(wl_registry_bind(registry, name, iface, std::min(version, max_version)));
}

}  // namespace

// ============================================================================
// AppGlobals
// ============================================================================

bool Display::AppGlobals::bind(wl_registry* registry, uint32_t name, const char* interface,
                               uint32_t version) {
    auto is = [interface](const wl_interface& iface) {
        return std::strcmp(interface, iface.name) == 0;
    };
    if (is(wl_data_device_manager_interface)) {
        data_device_manager = bind_global<wl_data_device_manager>(
            registry, name, &wl_data_device_manager_interface, version, 3);
    } else if (is(zxdg_decoration_manager_v1_interface)) {
        decoration_manager = bind_global<zxdg_decoration_manager_v1>(
            registry, name, &zxdg_decoration_manager_v1_interface, version, 1);
    } else if (is(wp_viewporter_interface)) {
        viewporter = bind_global<wp_viewporter>(registry, name, &wp_viewporter_interface, version, 1);
    } else if (is(wp_fractional_scale_manager_v1_interface)) {
        fractional_scale_manager = bind_global<wp_fractional_scale_manager_v1>(
            registry, name, &wp_fractional_scale_manager_v1_interface, version, 1);
    } else if (is(wp_presentation_interface)) {
        presentation = bind_global<wp_presentation>(registry, name, &wp_presentation_interface,
                                                    version, 2);
        if (presentation) {
            wp_presentation_add_listener(presentation, &presentation_listener, this);
        }
    } else if (is(xdg_activation_v1_interface)) {
        activation = bind_global<xdg_activation_v1>(registry, name, &xdg_activation_v1_interface,
                                                    version, 1);
    } else if (is(wp_cursor_shape_manager_v1_interface)) {
        cursor_shape_manager = bind_global<wp_cursor_shape_manager_v1>(
            registry, name, &wp_cursor_shape_manager_v1_interface, version, 1);
    } else if (is(zwp_pointer_constraints_v1_interface)) {
        pointer_constraints = bind_global<zwp_pointer_constraints_v1>(
            registry, name, &zwp_pointer_constraints_v1_interface, version, 1);
    } else if (is(zwp_relative_pointer_manager_v1_interface)) {
        relative_pointer_manager = bind_global<zwp_relative_pointer_manager_v1>(
            registry, name, &zwp_relative_pointer_manager_v1_interface, version, 1);
    } else if (is(zwp_primary_selection_device_manager_v1_interface)) {
        primary_selection_manager = bind_global<zwp_primary_selection_device_manager_v1>(
            registry, name, &zwp_primary_selection_device_manager_v1_interface, version, 1);
    } else if (is(zwp_text_input_manager_v3_interface)) {
        text_input_manager = bind_global<zwp_text_input_manager_v3>(
            registry, name, &zwp_text_input_manager_v3_interface, version, 1);
    } else if (is(xdg_toplevel_icon_manager_v1_interface)) {
        toplevel_icon_manager = bind_global<xdg_toplevel_icon_manager_v1>(
            registry, name, &xdg_toplevel_icon_manager_v1_interface, version, 1);
    } else if (is(zxdg_output_manager_v1_interface)) {
        xdg_output_manager = bind_global<zxdg_output_manager_v1>(
            registry, name, &zxdg_output_manager_v1_interface, version, 3);
        for (auto& out : display->outputs()) {
            out->attach_xdg_output(xdg_output_manager);
        }
    } else {
        return false;
    }
    return true;
}

void Display::AppGlobals::teardown() {
    std::vector<Window*> ws;
    {
        std::lock_guard<std::mutex> lock(surfaces_mutex);
        for (auto& [surface, window] : windows) {
            ws.push_back(window);
        }
    }
    for (Window* w : ws) {
        w->detach();
    }

    std::lock_guard<std::mutex> lock(requests_mutex);
    for (ActivationRequest* req : activations) {
        xdg_activation_token_v1_destroy(req->token);
        delete req;
    }
    activations.clear();
    for (FeedbackRequest* req : feedbacks) {
        wp_presentation_feedback_destroy(req->feedback);
        delete req;
    }
    feedbacks.clear();
    for (FrameCallbackRequest* req : frame_callbacks) {
        wl_callback_destroy(req->callback);
        delete req;
    }
    frame_callbacks.clear();
}

void Display::AppGlobals::destroy_globals() {
    if (xdg_output_manager) {
        zxdg_output_manager_v1_destroy(xdg_output_manager);
        xdg_output_manager = nullptr;
    }
    if (toplevel_icon_manager) {
        xdg_toplevel_icon_manager_v1_destroy(toplevel_icon_manager);
        toplevel_icon_manager = nullptr;
    }
    if (text_input_manager) {
        zwp_text_input_manager_v3_destroy(text_input_manager);
        text_input_manager = nullptr;
    }
    if (primary_selection_manager) {
        zwp_primary_selection_device_manager_v1_destroy(primary_selection_manager);
        primary_selection_manager = nullptr;
    }
    if (relative_pointer_manager) {
        zwp_relative_pointer_manager_v1_destroy(relative_pointer_manager);
        relative_pointer_manager = nullptr;
    }
    if (pointer_constraints) {
        zwp_pointer_constraints_v1_destroy(pointer_constraints);
        pointer_constraints = nullptr;
    }
    if (cursor_shape_manager) {
        wp_cursor_shape_manager_v1_destroy(cursor_shape_manager);
        cursor_shape_manager = nullptr;
    }
    if (activation) {
        xdg_activation_v1_destroy(activation);
        activation = nullptr;
    }
    if (presentation) {
        wp_presentation_destroy(presentation);
        presentation = nullptr;
    }
    if (fractional_scale_manager) {
        wp_fractional_scale_manager_v1_destroy(fractional_scale_manager);
        fractional_scale_manager = nullptr;
    }
    if (viewporter) {
        wp_viewporter_destroy(viewporter);
        viewporter = nullptr;
    }
    if (decoration_manager) {
        zxdg_decoration_manager_v1_destroy(decoration_manager);
        decoration_manager = nullptr;
    }
    if (data_device_manager) {
        // wl_data_device_manager has no destructor request: this frees the proxy.
        wl_data_device_manager_destroy(data_device_manager);
        data_device_manager = nullptr;
    }
}

void Display::AppGlobals::register_surface(wl_surface* surface, SurfaceId id) {
    if (!surface) {
        return;
    }
    std::lock_guard<std::mutex> lock(surfaces_mutex);
    surfaces[surface] = id;
}

void Display::AppGlobals::unregister_surface(wl_surface* surface) {
    std::lock_guard<std::mutex> lock(surfaces_mutex);
    surfaces.erase(surface);
}

SurfaceId Display::AppGlobals::surface_id(wl_surface* surface) const {
    if (!surface) {
        return kNoSurface;
    }
    std::lock_guard<std::mutex> lock(surfaces_mutex);
    auto it = surfaces.find(surface);
    return it == surfaces.end() ? kNoSurface : it->second;
}

void Display::AppGlobals::register_window(Window* window) {
    std::lock_guard<std::mutex> lock(surfaces_mutex);
    windows[window->wl_surface_ptr()] = window;
    surfaces[window->wl_surface_ptr()] = window->id();
}

void Display::AppGlobals::unregister_window(Window* window) {
    std::lock_guard<std::mutex> lock(surfaces_mutex);
    for (auto it = windows.begin(); it != windows.end(); ++it) {
        if (it->second == window) {
            surfaces.erase(it->first);
            windows.erase(it);
            return;
        }
    }
}

Window* Display::AppGlobals::window_for(wl_surface* surface) const {
    std::lock_guard<std::mutex> lock(surfaces_mutex);
    auto it = windows.find(surface);
    return it == windows.end() ? nullptr : it->second;
}

int32_t Display::AppGlobals::cursor_scale_for(wl_surface* surface) const {
    if (Window* w = window_for(surface)) {
        const uint32_t s120 = w->snapshot().scale120;
        return std::max<int32_t>(1, static_cast<int32_t>((s120 + 119) / 120));
    }
    // Not a window (a layer surface, a popup): the largest output scale.
    int32_t scale = 1;
    for (auto& out : display->outputs()) {
        scale = std::max(scale, out->scale());
    }
    return scale;
}

void Display::AppGlobals::output_changed(wl_output* output) {
    std::vector<Window*> ws;
    {
        std::lock_guard<std::mutex> lock(surfaces_mutex);
        for (auto& [surface, window] : windows) {
            ws.push_back(window);
        }
    }
    const OutputId id = output_id(output);
    for (Window* w : ws) {
        const auto outs = w->snapshot().outputs;
        if (std::find(outs.begin(), outs.end(), id) != outs.end()) {
            // Re-entering an output the window is already on only re-derives
            // its scale.
            w->handle_surface_enter(output);
        }
    }
}

OutputId Display::AppGlobals::output_id(wl_output* output) const {
    for (auto& out : display->outputs()) {
        if (out->wl_output_ptr() == output) {
            return out->id();
        }
    }
    return kNoOutput;
}

int32_t Display::AppGlobals::output_scale(wl_output* output) const {
    for (auto& out : display->outputs()) {
        if (out->wl_output_ptr() == output) {
            return out->scale();
        }
    }
    return 1;
}

void Display::AppGlobals::forget_activation(ActivationRequest* req) {
    {
        std::lock_guard<std::mutex> lock(requests_mutex);
        if (activations.erase(req) == 0) {
            return;
        }
    }
    xdg_activation_token_v1_destroy(req->token);
    delete req;
}

void Display::AppGlobals::forget_feedback(FeedbackRequest* req) {
    {
        std::lock_guard<std::mutex> lock(requests_mutex);
        if (feedbacks.erase(req) == 0) {
            return;
        }
    }
    // presented / discarded are destructor events: the proxy only needs freeing.
    wp_presentation_feedback_destroy(req->feedback);
    delete req;
}

void Display::AppGlobals::forget_frame_callback(FrameCallbackRequest* req) {
    {
        std::lock_guard<std::mutex> lock(requests_mutex);
        if (frame_callbacks.erase(req) == 0) {
            return;
        }
    }
    // done is a destructor event too.
    wl_callback_destroy(req->callback);
    delete req;
}

// ============================================================================
// Display
// ============================================================================

bool Display::has_decoration_manager() const { return app_->decoration_manager != nullptr; }
bool Display::has_viewporter() const { return app_->viewporter != nullptr; }
bool Display::has_fractional_scale() const { return app_->fractional_scale_manager != nullptr; }
bool Display::has_presentation() const { return app_->presentation != nullptr; }
bool Display::has_activation() const { return app_->activation != nullptr; }
bool Display::has_cursor_shape() const { return app_->cursor_shape_manager != nullptr; }
bool Display::has_pointer_constraints() const { return app_->pointer_constraints != nullptr; }
bool Display::has_relative_pointer() const { return app_->relative_pointer_manager != nullptr; }
bool Display::has_data_device() const { return app_->data_device_manager != nullptr; }
bool Display::has_primary_selection() const { return app_->primary_selection_manager != nullptr; }
bool Display::has_text_input() const { return app_->text_input_manager != nullptr; }
bool Display::has_toplevel_icon() const { return app_->toplevel_icon_manager != nullptr; }
bool Display::has_xdg_output() const { return app_->xdg_output_manager != nullptr; }

void Display::enable_input() {
    if (input_enabled_) {
        return;
    }
    input_enabled_ = true;
    for (auto& s : seats_) {
        s->enable_input();
    }
}

std::unique_ptr<Window> Display::create_window(const WindowConfig& config) {
    if (!compositor_ || !xdg_wm_base_) {
        return nullptr;
    }
    wl_surface* surface = wl_compositor_create_surface(compositor_);
    if (!surface) {
        return nullptr;
    }
    xdg_surface* xdg_surf = nullptr;
    xdg_toplevel* toplevel = nullptr;
    if (config.mapped) {
        xdg_surf = xdg_wm_base_get_xdg_surface(xdg_wm_base_, surface);
        toplevel = xdg_surf ? xdg_surface_get_toplevel(xdg_surf) : nullptr;
        if (!toplevel) {
            if (xdg_surf) {
                xdg_surface_destroy(xdg_surf);
            }
            wl_surface_destroy(surface);
            return nullptr;
        }
    }
    return std::make_unique<Window>(next_surface_id(), surface, xdg_surf, toplevel, config, this);
}

SurfaceId Display::surface_id_of(wl_surface* surface) const {
    return app_->surface_id(surface);
}

RequestId Display::request_activation_token(const std::string& app_id, wl_surface* surface,
                                            Seat* seat, uint32_t serial) {
    if (!app_->activation) {
        return 0;
    }
    xdg_activation_token_v1* token = xdg_activation_v1_get_activation_token(app_->activation);
    if (!token) {
        return 0;
    }
    auto* req = new ActivationRequest;
    req->app = app_.get();
    req->token = token;
    req->id = app_->next_request_id.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(app_->requests_mutex);
        app_->activations.insert(req);
    }
    xdg_activation_token_v1_add_listener(token, &activation_token_listener, req);
    if (seat && seat->wl_seat_ptr() && serial != 0) {
        xdg_activation_token_v1_set_serial(token, serial, seat->wl_seat_ptr());
    }
    if (!app_id.empty()) {
        xdg_activation_token_v1_set_app_id(token, app_id.c_str());
    }
    if (surface) {
        xdg_activation_token_v1_set_surface(token, surface);
    }
    xdg_activation_token_v1_commit(token);
    return req->id;
}

bool Display::activate(const std::string& token, wl_surface* surface) {
    if (!app_->activation || token.empty() || !surface) {
        return false;
    }
    xdg_activation_v1_activate(app_->activation, token.c_str(), surface);
    return true;
}

RequestId Display::request_presentation_feedback(wl_surface* surface) {
    if (!app_->presentation || !surface) {
        return 0;
    }
    // The feedback can only be answered after the surface's next commit,
    // which the caller makes after this returns, so the listener is in place
    // before any event for it can arrive, whichever thread dispatches.
    auto* req = new FeedbackRequest;
    req->app = app_.get();
    req->id = app_->next_request_id.fetch_add(1);
    req->surface_id = app_->surface_id(surface);
    std::lock_guard<std::mutex> lock(app_->requests_mutex);
    req->feedback = wp_presentation_feedback(app_->presentation, surface);
    if (!req->feedback) {
        delete req;
        return 0;
    }
    wp_presentation_feedback_add_listener(req->feedback, &feedback_listener, req);
    app_->feedbacks.insert(req);
    return req->id;
}

RequestId Display::request_frame_callback(wl_surface* surface) {
    if (!surface) {
        return 0;
    }
    auto* req = new FrameCallbackRequest;
    req->app = app_.get();
    req->id = app_->next_request_id.fetch_add(1);
    req->surface_id = app_->surface_id(surface);
    std::lock_guard<std::mutex> lock(app_->requests_mutex);
    req->callback = wl_surface_frame(surface);
    if (!req->callback) {
        delete req;
        return 0;
    }
    wl_callback_add_listener(req->callback, &frame_callback_listener, req);
    app_->frame_callbacks.insert(req);
    return req->id;
}

int Display::presentation_clock_id() const {
    return app_->presentation_clock_id.load();
}

}  // namespace browl
