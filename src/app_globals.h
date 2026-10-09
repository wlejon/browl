#pragma once

// Display::AppGlobals: the globals an application window and its input need
// (decorations, viewporter, fractional scale, presentation time, activation,
// cursor shape, pointer constraints, relative pointer, data device, primary
// selection, text input, toplevel icon, xdg-output), plus the bookkeeping
// that is shared across threads: which wl_surfaces browl made (surface ids
// for input events), the windows (for output scale changes), and the
// activation / presentation requests still waiting for their answer.

#include "browl/display.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

struct wl_output;
struct wl_registry;
struct wl_data_device_manager;
struct zxdg_decoration_manager_v1;
struct wp_viewporter;
struct wp_fractional_scale_manager_v1;
struct wp_presentation;
struct xdg_activation_v1;
struct wp_cursor_shape_manager_v1;
struct zwp_pointer_constraints_v1;
struct zwp_relative_pointer_manager_v1;
struct zwp_primary_selection_device_manager_v1;
struct zwp_text_input_manager_v3;
struct xdg_toplevel_icon_manager_v1;
struct zxdg_output_manager_v1;

namespace browl {

class Window;
struct ActivationRequest;
struct FeedbackRequest;

struct Display::AppGlobals {
    Display* display = nullptr;

    wl_data_device_manager* data_device_manager = nullptr;
    zxdg_decoration_manager_v1* decoration_manager = nullptr;
    wp_viewporter* viewporter = nullptr;
    wp_fractional_scale_manager_v1* fractional_scale_manager = nullptr;
    wp_presentation* presentation = nullptr;
    xdg_activation_v1* activation = nullptr;
    wp_cursor_shape_manager_v1* cursor_shape_manager = nullptr;
    zwp_pointer_constraints_v1* pointer_constraints = nullptr;
    zwp_relative_pointer_manager_v1* relative_pointer_manager = nullptr;
    zwp_primary_selection_device_manager_v1* primary_selection_manager = nullptr;
    zwp_text_input_manager_v3* text_input_manager = nullptr;
    xdg_toplevel_icon_manager_v1* toplevel_icon_manager = nullptr;
    zxdg_output_manager_v1* xdg_output_manager = nullptr;

    std::atomic<int> presentation_clock_id{-1};
    std::atomic<RequestId> next_request_id{1};

    /// Binds `interface` when it is one of the globals above; false otherwise.
    bool bind(wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
    /// Detaches every window and drops the requests still waiting (before
    /// the seats and outputs go).
    void teardown();
    /// Destroys the globals (last, before the connection closes).
    void destroy_globals();

    // Surfaces browl created, by wl_surface (any thread).
    void register_surface(wl_surface* surface, SurfaceId id);
    void unregister_surface(wl_surface* surface);
    SurfaceId surface_id(wl_surface* surface) const;

    // Windows, by their wl_surface.
    void register_window(Window* window);
    void unregister_window(Window* window);
    Window* window_for(wl_surface* surface) const;
    /// The integer buffer scale a cursor over `surface` wants (1 if unknown).
    int32_t cursor_scale_for(wl_surface* surface) const;

    /// An output's scale (or other state) changed: windows on it re-derive
    /// their scale.
    void output_changed(wl_output* output);
    OutputId output_id(wl_output* output) const;
    int32_t output_scale(wl_output* output) const;

    // Requests answered later (Display::request_activation_token,
    // Display::request_presentation_feedback).
    void forget_activation(ActivationRequest* req);
    void forget_feedback(FeedbackRequest* req);

    mutable std::mutex surfaces_mutex;
    std::unordered_map<wl_surface*, SurfaceId> surfaces;
    std::unordered_map<wl_surface*, Window*> windows;

    std::mutex requests_mutex;
    std::unordered_set<ActivationRequest*> activations;
    std::unordered_set<FeedbackRequest*> feedbacks;
};

}  // namespace browl
