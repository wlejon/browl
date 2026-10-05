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

class Display {
public:
    static std::unique_ptr<Display> connect(const std::string& name = "");
    static std::unique_ptr<Display> connect_to_fd(int fd);
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
    void init_registry();

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
};

}  // namespace browl
