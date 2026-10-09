#pragma once

#include "browl/event_queue.h"
#include "browl/events.h"
#include "browl/types.h"

#include <memory>
#include <string>
#include <vector>

struct wl_display;
struct wl_registry;
struct wl_compositor;
struct wl_subcompositor;
struct wl_shm;
struct zwlr_layer_shell_v1;
struct xdg_wm_base;
struct zwlr_foreign_toplevel_manager_v1;
struct ext_session_lock_manager_v1;
struct zwp_idle_inhibit_manager_v1;
struct zwlr_screencopy_manager_v1;
struct ext_idle_notifier_v1;
struct wl_surface;

namespace browl {

class Output;
class Seat;
class Window;
struct WindowConfig;
class LayerSurface;
struct LayerSurfaceConfig;
class Positioner;
class Popup;
class ForeignToplevelManager;
class SessionLock;
class IdleInhibitor;
class IdleNotification;
class ScreenCopyManager;
class ShmPool;

/// Why Wayland cannot be used on this platform: empty on Linux, the reason
/// elsewhere (where Display::connect() always fails with it).
std::string unavailable_reason();

/// Whether Wayland is available on this platform.
inline bool available(std::string* reason = nullptr) {
    std::string r = unavailable_reason();
    if (reason) *reason = r;
    return r.empty();
}

class Display {
public:
    /// Connects to the compositor `name` (or $WAYLAND_DISPLAY when empty) and
    /// binds the globals. On failure returns nullptr and, if `error` is given,
    /// says why.
    static std::unique_ptr<Display> connect(const std::string& name = "",
                                            std::string* error = nullptr);
    /// Same, over an already connected socket; takes ownership of `fd`.
    static std::unique_ptr<Display> connect_to_fd(int fd, std::string* error = nullptr);
    ~Display();

    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    int fd() const;
    int dispatch();
    int dispatch_pending();
    int flush();
    int roundtrip();
    bool prepare_read();
    void cancel_read();
    int read_events();

    EventQueue& events() { return event_queue_; }

    // Capability queries
    bool has_compositor() const;
    bool has_shm() const;
    bool has_layer_shell() const;
    bool has_xdg_shell() const;
    bool has_foreign_toplevel_manager() const;
    bool has_session_lock() const;
    bool has_idle_inhibit() const;
    bool has_screencopy() const;
    bool has_idle_notify() const;

    // Application-window protocols
    bool has_decoration_manager() const;   // zxdg_decoration_manager_v1
    bool has_viewporter() const;           // wp_viewporter
    bool has_fractional_scale() const;     // wp_fractional_scale_manager_v1
    bool has_presentation() const;         // wp_presentation
    bool has_activation() const;           // xdg_activation_v1
    bool has_cursor_shape() const;         // wp_cursor_shape_manager_v1
    bool has_pointer_constraints() const;  // zwp_pointer_constraints_v1
    bool has_relative_pointer() const;     // zwp_relative_pointer_manager_v1
    bool has_data_device() const;          // wl_data_device_manager
    bool has_primary_selection() const;    // zwp_primary_selection_device_manager_v1
    bool has_text_input() const;           // zwp_text_input_manager_v3
    bool has_toplevel_icon() const;        // xdg_toplevel_icon_manager_v1
    bool has_xdg_output() const;           // zxdg_output_manager_v1

    /// Bind every seat's pointer, keyboard and touch (and those of seats that
    /// appear later) and start delivering input events. Idempotent.
    void enable_input();
    bool input_enabled() const { return input_enabled_; }

    /// An application window (xdg_toplevel). Null without wl_compositor and
    /// xdg_wm_base. The first configure arrives as a WindowConfigureEvent
    /// after the initial commit, which this makes.
    std::unique_ptr<Window> create_window(const WindowConfig& config);

    /// The browl surface id of a wl_surface this Display created (windows,
    /// layer surfaces, popups, lock surfaces); kNoSurface for any other.
    SurfaceId surface_id_of(wl_surface* surface) const;

    /// Ask xdg_activation_v1 for a token to activate a surface with (here or
    /// in another process). `surface`, `seat` and `serial` say what user
    /// action the request answers; each may be null / 0. The token arrives as
    /// an ActivationTokenEvent with the returned id; 0 without the protocol.
    RequestId request_activation_token(const std::string& app_id = "",
                                       wl_surface* surface = nullptr,
                                       Seat* seat = nullptr, uint32_t serial = 0);
    /// Activate (raise and focus) `surface` with a token. False without the
    /// protocol or with an empty token.
    bool activate(const std::string& token, wl_surface* surface);

    /// Ask wp_presentation when the surface's NEXT commit turns to light. Call
    /// right before the commit (for a Vulkan swapchain: before
    /// vkQueuePresentKHR). The answer is a PresentationFeedbackEvent with the
    /// returned id; 0 without the protocol. Callable from any thread.
    RequestId request_presentation_feedback(wl_surface* surface);
    /// The clock presentation times are on (wp_presentation.clock_id), e.g.
    /// CLOCK_MONOTONIC (1); -1 before the compositor said.
    int presentation_clock_id() const;

    // Shell Role Factories
    std::unique_ptr<LayerSurface> create_layer_surface(const LayerSurfaceConfig& config);
    std::shared_ptr<ForeignToplevelManager> foreign_toplevel_manager();
    std::unique_ptr<SessionLock> create_session_lock();
    std::unique_ptr<IdleInhibitor> create_idle_inhibitor(wl_surface* surface);
    std::shared_ptr<ScreenCopyManager> screencopy_manager();
    std::unique_ptr<IdleNotification> create_idle_notification(uint32_t timeout_ms,
                                                              Seat* seat = nullptr);
    std::shared_ptr<ShmPool> create_shm_pool(size_t size);
    std::unique_ptr<Positioner> create_positioner();
    wl_surface* create_surface();

    // Outputs and Seats
    std::vector<std::shared_ptr<Output>> outputs() const;
    std::shared_ptr<Output> output_by_id(OutputId id) const;
    std::shared_ptr<Output> default_output() const;

    std::vector<std::shared_ptr<Seat>> seats() const;
    std::shared_ptr<Seat> seat_by_id(SeatId id) const;
    std::shared_ptr<Seat> default_seat() const;

    // Raw protocol pointers
    wl_display* wl_display_ptr() const { return display_; }
    wl_compositor* wl_compositor_ptr() const { return compositor_; }
    wl_shm* wl_shm_ptr() const { return shm_; }
    zwlr_layer_shell_v1* layer_shell_ptr() const { return layer_shell_; }
    xdg_wm_base* xdg_wm_base_ptr() const { return xdg_wm_base_; }
    zwlr_foreign_toplevel_manager_v1* foreign_toplevel_manager_ptr() const {
        return foreign_toplevel_manager_raw_;
    }
    ext_session_lock_manager_v1* session_lock_manager_ptr() const {
        return session_lock_manager_;
    }
    zwp_idle_inhibit_manager_v1* idle_inhibit_manager_ptr() const {
        return idle_inhibit_manager_;
    }
    zwlr_screencopy_manager_v1* screencopy_manager_ptr() const {
        return screencopy_manager_raw_;
    }
    ext_idle_notifier_v1* idle_notifier_ptr() const { return idle_notifier_; }

    // Internal ID generator
    SurfaceId next_surface_id();
    uint64_t next_notification_id();

    // Internal registry event handlers
    void handle_global(uint32_t name, const char* interface, uint32_t version);
    void handle_global_remove(uint32_t name);

private:
    explicit Display(wl_display* display);
    bool init_registry(std::string* error);

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    EventQueue event_queue_;

    wl_compositor* compositor_ = nullptr;
    wl_subcompositor* subcompositor_ = nullptr;
    wl_shm* shm_ = nullptr;
    zwlr_layer_shell_v1* layer_shell_ = nullptr;
    xdg_wm_base* xdg_wm_base_ = nullptr;
    zwlr_foreign_toplevel_manager_v1* foreign_toplevel_manager_raw_ = nullptr;
    ext_session_lock_manager_v1* session_lock_manager_ = nullptr;
    zwp_idle_inhibit_manager_v1* idle_inhibit_manager_ = nullptr;
    zwlr_screencopy_manager_v1* screencopy_manager_raw_ = nullptr;
    ext_idle_notifier_v1* idle_notifier_ = nullptr;

    std::vector<std::shared_ptr<Output>> outputs_;
    std::vector<std::shared_ptr<Seat>> seats_;
    std::shared_ptr<ForeignToplevelManager> toplevel_manager_instance_;
    std::shared_ptr<ScreenCopyManager> screencopy_manager_instance_;

    SurfaceId next_surface_id_ = 1;
    uint64_t next_notification_id_ = 1;
    bool input_enabled_ = false;

public:
    // The application-window globals and bookkeeping (src/app_globals.h).
    struct AppGlobals;
    AppGlobals& app_globals() const { return *app_; }

private:
    std::unique_ptr<AppGlobals> app_;
};

}  // namespace browl
