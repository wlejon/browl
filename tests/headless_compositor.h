#pragma once

#include <wayland-server.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace browl::test {

struct CompositorToplevelState {
    struct wl_resource* resource = nullptr;
    std::string title;
    std::string app_id;
    uint32_t state = 0;
    bool activated = false;
    bool closed = false;
    bool minimized = false;
    bool maximized = false;
    bool fullscreen = false;
};

class HeadlessCompositor {
public:
    HeadlessCompositor();
    ~HeadlessCompositor();

    HeadlessCompositor(const HeadlessCompositor&) = delete;
    HeadlessCompositor& operator=(const HeadlessCompositor&) = delete;

    int create_client_fd();
    const char* add_socket_auto();
    int add_socket(const char* name);
    void start();
    void stop();
    void flush();
    void step_loop(int timeout_ms = 10);

    // Compositor & display actions
    void send_output_done();
    void send_seat_caps(uint32_t caps, const char* name = "seat0");

    // Layer shell actions
    void configure_layer_surface(uint32_t width, uint32_t height);
    void close_layer_surface();
    bool layer_surface_created() const;
    uint32_t last_layer_ack_serial() const;
    bool layer_surface_committed() const;

    // XDG popup actions
    void configure_popup(int32_t x, int32_t y, int32_t width, int32_t height);
    void send_popup_done();
    void send_popup_repositioned(uint32_t token);
    bool popup_created() const;
    bool popup_repositioned_received() const;

    // Foreign toplevel actions
    CompositorToplevelState* create_foreign_toplevel(const std::string& title,
                                                   const std::string& app_id,
                                                   uint32_t state);
    void update_toplevel_title(CompositorToplevelState* top, const std::string& title);
    void update_toplevel_app_id(CompositorToplevelState* top, const std::string& app_id);
    void update_toplevel_state(CompositorToplevelState* top, uint32_t state);
    void close_toplevel(CompositorToplevelState* top);

    // Session lock actions
    void send_session_locked();
    void send_session_lock_finished();
    void configure_lock_surface(uint32_t width, uint32_t height);
    bool session_lock_created() const;
    bool session_unlock_received() const;
    bool lock_surface_created() const;
    uint32_t last_lock_surface_ack_serial() const;

    // Idle actions
    bool idle_inhibitor_created() const;
    bool idle_inhibitor_destroyed() const;
    void fire_idle();
    void fire_resume();

    // Screencopy actions
    void send_screencopy_buffer(uint32_t format, uint32_t width, uint32_t height,
                               uint32_t stride);
    void send_screencopy_ready(uint32_t tv_sec, uint32_t tv_nsec);
    void send_screencopy_failed();
    bool screencopy_frame_created() const;
    bool screencopy_copy_received() const;

    // Inspection getters
    struct wl_display* server_display() const { return display_; }

public:
    void init_globals();
    void init_shell_globals();
    void init_service_globals();

    struct wl_display* display_ = nullptr;
    struct wl_client* client_ = nullptr;
    struct wl_event_loop* loop_ = nullptr;

    std::thread worker_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;

    // Globals
    struct wl_global* compositor_global_ = nullptr;
    struct wl_global* output_global_ = nullptr;
    struct wl_global* seat_global_ = nullptr;
    struct wl_global* layer_shell_global_ = nullptr;
    struct wl_global* xdg_wm_base_global_ = nullptr;
    struct wl_global* foreign_toplevel_global_ = nullptr;
    struct wl_global* session_lock_global_ = nullptr;
    struct wl_global* idle_inhibit_global_ = nullptr;
    struct wl_global* screencopy_global_ = nullptr;
    struct wl_global* idle_notify_global_ = nullptr;

    // Active resources
    struct wl_resource* output_resource_ = nullptr;
    struct wl_resource* seat_resource_ = nullptr;
    struct wl_resource* layer_surface_resource_ = nullptr;
    struct wl_resource* surface_resource_ = nullptr;
    struct wl_resource* popup_resource_ = nullptr;
    struct wl_resource* positioner_resource_ = nullptr;
    struct wl_resource* toplevel_manager_resource_ = nullptr;
    struct wl_resource* session_lock_resource_ = nullptr;
    struct wl_resource* lock_surface_resource_ = nullptr;
    struct wl_resource* idle_notification_resource_ = nullptr;
    struct wl_resource* screencopy_frame_resource_ = nullptr;

    // State flags
    uint32_t next_serial_ = 1;
    uint32_t last_layer_ack_serial_ = 0;
    bool layer_surface_committed_ = false;
    bool popup_repositioned_received_ = false;
    bool session_unlock_received_ = false;
    uint32_t last_lock_surface_ack_serial_ = 0;
    bool idle_inhibitor_created_ = false;
    bool idle_inhibitor_destroyed_ = false;
    bool screencopy_copy_received_ = false;

    std::vector<std::unique_ptr<CompositorToplevelState>> toplevels_;
};

}  // namespace browl::test
