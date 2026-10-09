#include "browl/display.h"

#include "browl/foreign_toplevel.h"
#include "browl/idle_inhibit.h"
#include "browl/idle_notify.h"
#include "browl/layer_surface.h"
#include "browl/output.h"
#include "browl/popup.h"
#include "browl/screencopy.h"
#include "browl/seat.h"
#include "browl/session_lock.h"
#include "browl/shm_pool.h"
#include "browl/window.h"

#include "app_globals.h"

#include "ext-idle-notify-v1-client-protocol.h"
#include "ext-session-lock-v1-client-protocol.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <wayland-client.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace browl {

namespace {

static void registry_handle_global(void* data, struct wl_registry* /*registry*/,
                                   uint32_t name, const char* interface, uint32_t version) {
    auto* display = static_cast<Display*>(data);
    if (display) {
        display->handle_global(name, interface, version);
    }
}

static void registry_handle_global_remove(void* data, struct wl_registry* /*registry*/,
                                          uint32_t name) {
    auto* display = static_cast<Display*>(data);
    if (display) {
        display->handle_global_remove(name);
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static void xdg_wm_base_handle_ping(void* /*data*/, struct xdg_wm_base* base, uint32_t serial) {
    xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener xdg_base_listener = {
    .ping = xdg_wm_base_handle_ping,
};

}  // namespace

Display::Display(wl_display* display)
    : display_(display), app_(std::make_unique<AppGlobals>()) {
    app_->display = this;
}

Display::~Display() {
    // Windows and pending requests first (they use the seats, outputs and
    // globals), then seats and outputs, then the globals.
    app_->teardown();

    if (toplevel_manager_instance_) {
        toplevel_manager_instance_->detach();
        toplevel_manager_instance_.reset();
        foreign_toplevel_manager_raw_ = nullptr;
    }
    if (screencopy_manager_instance_) {
        screencopy_manager_instance_->detach();
        screencopy_manager_instance_.reset();
        screencopy_manager_raw_ = nullptr;
    }

    for (auto& out : outputs_) {
        out->detach();
    }
    outputs_.clear();

    for (auto& s : seats_) {
        s->detach();
    }
    seats_.clear();

    app_->destroy_globals();

    if (idle_notifier_) {
        ext_idle_notifier_v1_destroy(idle_notifier_);
        idle_notifier_ = nullptr;
    }
    if (screencopy_manager_raw_) {
        zwlr_screencopy_manager_v1_destroy(screencopy_manager_raw_);
        screencopy_manager_raw_ = nullptr;
    }
    if (idle_inhibit_manager_) {
        zwp_idle_inhibit_manager_v1_destroy(idle_inhibit_manager_);
        idle_inhibit_manager_ = nullptr;
    }
    if (session_lock_manager_) {
        ext_session_lock_manager_v1_destroy(session_lock_manager_);
        session_lock_manager_ = nullptr;
    }
    if (foreign_toplevel_manager_raw_) {
        zwlr_foreign_toplevel_manager_v1_destroy(foreign_toplevel_manager_raw_);
        foreign_toplevel_manager_raw_ = nullptr;
    }
    if (xdg_wm_base_) {
        xdg_wm_base_destroy(xdg_wm_base_);
    }
    if (layer_shell_) {
        zwlr_layer_shell_v1_destroy(layer_shell_);
    }
    if (shm_) {
        wl_shm_destroy(shm_);
    }
    if (subcompositor_) {
        wl_subcompositor_destroy(subcompositor_);
    }
    if (compositor_) {
        wl_compositor_destroy(compositor_);
    }
    if (registry_) {
        wl_registry_destroy(registry_);
    }
    if (display_) {
        wl_display_disconnect(display_);
    }
}

std::string unavailable_reason() {
    return {};
}

std::unique_ptr<Display> Display::connect(const std::string& name, std::string* error) {
    wl_display* disp = wl_display_connect(name.empty() ? nullptr : name.c_str());
    if (!disp) {
        if (error) {
            const char* env = std::getenv("WAYLAND_DISPLAY");
            *error = "cannot connect to Wayland display '" +
                     (name.empty() ? std::string(env ? env : "wayland-0") : name) +
                     "': " + std::strerror(errno);
        }
        return nullptr;
    }

    auto d = std::unique_ptr<Display>(new Display(disp));
    if (!d->init_registry(error)) {
        return nullptr;
    }
    return d;
}

std::unique_ptr<Display> Display::connect_to_fd(int fd, std::string* error) {
    wl_display* disp = wl_display_connect_to_fd(fd);
    if (!disp) {
        if (error) {
            *error = std::string("cannot use fd as a Wayland connection: ") + std::strerror(errno);
        }
        return nullptr;
    }

    auto d = std::unique_ptr<Display>(new Display(disp));
    if (!d->init_registry(error)) {
        return nullptr;
    }
    return d;
}

bool Display::init_registry(std::string* error) {
    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &registry_listener, this);
    // The second roundtrip collects the events the newly bound globals send
    // on bind (output modes, seat capabilities).
    if (roundtrip() < 0 || roundtrip() < 0) {
        if (error) {
            *error = std::string("Wayland roundtrip failed: ") +
                     std::strerror(wl_display_get_error(display_));
        }
        return false;
    }
    return true;
}

int Display::fd() const {
    return wl_display_get_fd(display_);
}

int Display::dispatch() {
    return wl_display_dispatch(display_);
}

int Display::dispatch_pending() {
    return wl_display_dispatch_pending(display_);
}

int Display::flush() {
    return wl_display_flush(display_);
}

int Display::roundtrip() {
    return wl_display_roundtrip(display_);
}

bool Display::prepare_read() {
    return wl_display_prepare_read(display_) == 0;
}

void Display::cancel_read() {
    wl_display_cancel_read(display_);
}

int Display::read_events() {
    return wl_display_read_events(display_);
}

bool Display::has_compositor() const {
    return compositor_ != nullptr;
}

bool Display::has_shm() const {
    return shm_ != nullptr;
}

bool Display::has_layer_shell() const {
    return layer_shell_ != nullptr;
}

bool Display::has_xdg_shell() const {
    return xdg_wm_base_ != nullptr;
}

bool Display::has_foreign_toplevel_manager() const {
    return foreign_toplevel_manager_raw_ != nullptr;
}

bool Display::has_session_lock() const {
    return session_lock_manager_ != nullptr;
}

bool Display::has_idle_inhibit() const {
    return idle_inhibit_manager_ != nullptr;
}

bool Display::has_screencopy() const {
    return screencopy_manager_raw_ != nullptr;
}

bool Display::has_idle_notify() const {
    return idle_notifier_ != nullptr;
}

std::unique_ptr<LayerSurface> Display::create_layer_surface(const LayerSurfaceConfig& config) {
    if (!compositor_ || !layer_shell_) {
        return nullptr;
    }

    wl_surface* surface = wl_compositor_create_surface(compositor_);
    if (!surface) {
        return nullptr;
    }

    struct wl_output* out = config.output ? config.output->wl_output_ptr() : nullptr;
    struct zwlr_layer_surface_v1* layer_surf = zwlr_layer_shell_v1_get_layer_surface(
        layer_shell_, surface, out, static_cast<uint32_t>(config.layer),
        config.name_space.c_str());
    if (!layer_surf) {
        wl_surface_destroy(surface);
        return nullptr;
    }

    SurfaceId id = next_surface_id();
    return std::make_unique<LayerSurface>(id, surface, layer_surf, config, this);
}

std::shared_ptr<ForeignToplevelManager> Display::foreign_toplevel_manager() {
    if (!toplevel_manager_instance_ && foreign_toplevel_manager_raw_) {
        toplevel_manager_instance_ =
            std::make_shared<ForeignToplevelManager>(foreign_toplevel_manager_raw_, this);
    }
    return toplevel_manager_instance_;
}

std::unique_ptr<SessionLock> Display::create_session_lock() {
    if (!session_lock_manager_) {
        return nullptr;
    }

    struct ext_session_lock_v1* lock = ext_session_lock_manager_v1_lock(session_lock_manager_);
    if (!lock) {
        return nullptr;
    }

    return std::make_unique<SessionLock>(lock, this);
}

std::unique_ptr<IdleInhibitor> Display::create_idle_inhibitor(wl_surface* surface) {
    if (!idle_inhibit_manager_ || !surface) {
        return nullptr;
    }

    struct zwp_idle_inhibitor_v1* inh =
        zwp_idle_inhibit_manager_v1_create_inhibitor(idle_inhibit_manager_, surface);
    if (!inh) {
        return nullptr;
    }

    return std::make_unique<IdleInhibitor>(inh, this);
}

std::shared_ptr<ScreenCopyManager> Display::screencopy_manager() {
    if (!screencopy_manager_instance_ && screencopy_manager_raw_) {
        screencopy_manager_instance_ =
            std::make_shared<ScreenCopyManager>(screencopy_manager_raw_, this);
    }
    return screencopy_manager_instance_;
}

std::unique_ptr<IdleNotification> Display::create_idle_notification(uint32_t timeout_ms,
                                                                   Seat* seat) {
    if (!idle_notifier_) {
        return nullptr;
    }

    struct wl_seat* seat_ptr = seat ? seat->wl_seat_ptr() : nullptr;
    struct ext_idle_notification_v1* notif =
        ext_idle_notifier_v1_get_idle_notification(idle_notifier_, timeout_ms, seat_ptr);
    if (!notif) {
        return nullptr;
    }

    uint64_t id = next_notification_id();
    return std::make_unique<IdleNotification>(id, notif, this);
}

std::shared_ptr<ShmPool> Display::create_shm_pool(size_t size) {
    if (!shm_) {
        return nullptr;
    }
    return ShmPool::create(shm_, size);
}

std::unique_ptr<Positioner> Display::create_positioner() {
    if (!xdg_wm_base_) {
        return nullptr;
    }

    struct xdg_positioner* pos = xdg_wm_base_create_positioner(xdg_wm_base_);
    if (!pos) {
        return nullptr;
    }

    return std::make_unique<Positioner>(pos, this);
}

wl_surface* Display::create_surface() {
    if (!compositor_) {
        return nullptr;
    }
    return wl_compositor_create_surface(compositor_);
}

std::vector<std::shared_ptr<Output>> Display::outputs() const {
    return outputs_;
}

std::shared_ptr<Output> Display::output_by_id(OutputId id) const {
    for (const auto& out : outputs_) {
        if (out->id() == id) {
            return out;
        }
    }
    return nullptr;
}

std::shared_ptr<Output> Display::default_output() const {
    return outputs_.empty() ? nullptr : outputs_.front();
}

std::vector<std::shared_ptr<Seat>> Display::seats() const {
    return seats_;
}

std::shared_ptr<Seat> Display::seat_by_id(SeatId id) const {
    for (const auto& s : seats_) {
        if (s->id() == id) {
            return s;
        }
    }
    return nullptr;
}

std::shared_ptr<Seat> Display::default_seat() const {
    return seats_.empty() ? nullptr : seats_.front();
}

SurfaceId Display::next_surface_id() {
    return next_surface_id_++;
}

uint64_t Display::next_notification_id() {
    return next_notification_id_++;
}

void Display::handle_global(uint32_t name, const char* interface, uint32_t version) {
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        // v6: wl_surface.preferred_buffer_scale (a window's integer scale).
        uint32_t bind_ver = std::min(version, 6u);
        compositor_ = static_cast<wl_compositor*>(
            wl_registry_bind(registry_, name, &wl_compositor_interface, bind_ver));
    } else if (std::strcmp(interface, wl_subcompositor_interface.name) == 0) {
        uint32_t bind_ver = std::min(version, 1u);
        subcompositor_ = static_cast<wl_subcompositor*>(
            wl_registry_bind(registry_, name, &wl_subcompositor_interface, bind_ver));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        uint32_t bind_ver = std::min(version, 1u);
        shm_ = static_cast<wl_shm*>(
            wl_registry_bind(registry_, name, &wl_shm_interface, bind_ver));
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
        uint32_t bind_ver = std::min(version, 4u);
        auto* out = static_cast<wl_output*>(
            wl_registry_bind(registry_, name, &wl_output_interface, bind_ver));
        auto output = std::make_shared<Output>(name, out, this);
        output->attach_xdg_output(app_->xdg_output_manager);
        outputs_.push_back(output);
        event_queue_.push(OutputAddedEvent{output->snapshot()});
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        // v8: axis_value120; v9: axis_relative_direction.
        uint32_t bind_ver = std::min(version, 9u);
        auto* st = static_cast<wl_seat*>(
            wl_registry_bind(registry_, name, &wl_seat_interface, bind_ver));
        auto seat = std::make_shared<Seat>(name, st, this);
        seats_.push_back(seat);
        if (input_enabled_) {
            seat->enable_input();
        }
        event_queue_.push(SeatAddedEvent{seat->snapshot()});
    } else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        uint32_t bind_ver = std::min(version, 4u);
        layer_shell_ = static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(registry_, name, &zwlr_layer_shell_v1_interface, bind_ver));
    } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
        // v4: configure_bounds; v5: wm_capabilities; v6: the suspended state.
        uint32_t bind_ver = std::min(version, 6u);
        xdg_wm_base_ =static_cast<xdg_wm_base*>(
            wl_registry_bind(registry_, name, &xdg_wm_base_interface, bind_ver));
        if (xdg_wm_base_) {
            xdg_wm_base_add_listener(xdg_wm_base_, &xdg_base_listener, this);
        }
    } else if (std::strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        uint32_t bind_ver = std::min(version, 3u);
        foreign_toplevel_manager_raw_ = static_cast<zwlr_foreign_toplevel_manager_v1*>(
            wl_registry_bind(registry_, name, &zwlr_foreign_toplevel_manager_v1_interface, bind_ver));
    } else if (std::strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
        uint32_t bind_ver = std::min(version, 1u);
        session_lock_manager_ = static_cast<ext_session_lock_manager_v1*>(
            wl_registry_bind(registry_, name, &ext_session_lock_manager_v1_interface, bind_ver));
    } else if (std::strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) == 0) {
        uint32_t bind_ver = std::min(version, 1u);
        idle_inhibit_manager_ = static_cast<zwp_idle_inhibit_manager_v1*>(
            wl_registry_bind(registry_, name, &zwp_idle_inhibit_manager_v1_interface, bind_ver));
    } else if (std::strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        uint32_t bind_ver = std::min(version, 3u);
        screencopy_manager_raw_ = static_cast<zwlr_screencopy_manager_v1*>(
            wl_registry_bind(registry_, name, &zwlr_screencopy_manager_v1_interface, bind_ver));
    } else if (std::strcmp(interface, ext_idle_notifier_v1_interface.name) == 0) {
        uint32_t bind_ver = std::min(version, 1u);
        idle_notifier_ = static_cast<ext_idle_notifier_v1*>(
            wl_registry_bind(registry_, name, &ext_idle_notifier_v1_interface, bind_ver));
    } else {
        app_->bind(registry_, name, interface, version);
    }
}

void Display::handle_global_remove(uint32_t name) {
    auto out_it = std::remove_if(outputs_.begin(), outputs_.end(),
                                 [name](const std::shared_ptr<Output>& out) {
                                     return out->id() == name;
                                 });
    if (out_it != outputs_.end()) {
        outputs_.erase(out_it, outputs_.end());
        event_queue_.push(OutputRemovedEvent{name});
        return;
    }

    auto seat_it = std::remove_if(seats_.begin(), seats_.end(),
                                  [name](const std::shared_ptr<Seat>& s) {
                                      return s->id() == name;
                                  });
    if (seat_it != seats_.end()) {
        seats_.erase(seat_it, seats_.end());
        event_queue_.push(SeatRemovedEvent{name});
        return;
    }
}

}  // namespace browl
