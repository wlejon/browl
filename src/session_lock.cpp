#include "browl/session_lock.h"

#include "browl/display.h"
#include "browl/output.h"
#include "ext-session-lock-v1-client-protocol.h"

#include <wayland-client.h>

namespace browl {

namespace {

static void lock_handle_locked(void* data, struct ext_session_lock_v1* /*lock*/) {
    auto* lock = static_cast<SessionLock*>(data);
    if (lock) {
        lock->handle_locked();
    }
}

static void lock_handle_finished(void* data, struct ext_session_lock_v1* /*lock*/) {
    auto* lock = static_cast<SessionLock*>(data);
    if (lock) {
        lock->handle_finished();
    }
}

static const struct ext_session_lock_v1_listener lock_listener = {
    .locked = lock_handle_locked,
    .finished = lock_handle_finished,
};

static void lock_surface_handle_configure(void* data,
                                          struct ext_session_lock_surface_v1* /*surface*/,
                                          uint32_t serial, uint32_t width, uint32_t height) {
    auto* surface = static_cast<SessionLockSurface*>(data);
    if (surface) {
        surface->handle_configure(serial, width, height);
    }
}

static const struct ext_session_lock_surface_v1_listener surface_listener = {
    .configure = lock_surface_handle_configure,
};

}  // namespace

SessionLock::SessionLock(ext_session_lock_v1* lock, Display* display)
    : lock_(lock), display_(display) {
    if (lock_) {
        ext_session_lock_v1_add_listener(lock_, &lock_listener, this);
    }
}

SessionLock::~SessionLock() {
    if (lock_) {
        ext_session_lock_v1_destroy(lock_);
    }
}

SessionLockSnapshot SessionLock::snapshot() const {
    SessionLockSnapshot snap;
    snap.locked = locked_;
    snap.finished = finished_;
    return snap;
}

std::unique_ptr<SessionLockSurface> SessionLock::create_surface(Output& output) {
    if (!lock_ || !display_) {
        return nullptr;
    }

    wl_surface* surface = display_->create_surface();
    if (!surface) {
        return nullptr;
    }

    struct ext_session_lock_surface_v1* lock_surf =
        ext_session_lock_v1_get_lock_surface(lock_, surface, output.wl_output_ptr());
    if (!lock_surf) {
        wl_surface_destroy(surface);
        return nullptr;
    }

    SurfaceId id = display_->next_surface_id();
    return std::make_unique<SessionLockSurface>(id, output.id(), surface, lock_surf, display_);
}

void SessionLock::unlock_and_destroy() {
    if (lock_) {
        ext_session_lock_v1_unlock_and_destroy(lock_);
        lock_ = nullptr;
    }
}

void SessionLock::handle_locked() {
    locked_ = true;
    if (display_) {
        display_->events().push(SessionLockedEvent{});
    }
}

void SessionLock::handle_finished() {
    finished_ = true;
    if (display_) {
        display_->events().push(SessionLockFinishedEvent{});
    }
}

SessionLockSurface::SessionLockSurface(SurfaceId id, OutputId output_id, wl_surface* surface,
                                       ext_session_lock_surface_v1* lock_surface,
                                       Display* display)
    : id_(id),
      output_id_(output_id),
      surface_(surface),
      lock_surface_(lock_surface),
      display_(display) {
    if (lock_surface_) {
        ext_session_lock_surface_v1_add_listener(lock_surface_, &surface_listener, this);
    }
}

SessionLockSurface::~SessionLockSurface() {
    if (lock_surface_) {
        ext_session_lock_surface_v1_destroy(lock_surface_);
    }
    if (surface_) {
        wl_surface_destroy(surface_);
    }
}

SessionLockSurfaceSnapshot SessionLockSurface::snapshot() const {
    SessionLockSurfaceSnapshot snap;
    snap.id = id_;
    snap.output_id = output_id_;
    snap.configured_size = configured_size_;
    snap.configured_serial = configured_serial_;
    return snap;
}

void SessionLockSurface::ack_configure(uint32_t serial) {
    if (lock_surface_) {
        ext_session_lock_surface_v1_ack_configure(lock_surface_, serial);
    }
}

void SessionLockSurface::commit() {
    if (surface_) {
        wl_surface_commit(surface_);
    }
}

void SessionLockSurface::attach_buffer(wl_buffer* buffer, int32_t x, int32_t y) {
    if (surface_) {
        wl_surface_attach(surface_, buffer, x, y);
    }
}

void SessionLockSurface::damage(int32_t x, int32_t y, int32_t width, int32_t height) {
    if (surface_) {
        if (wl_surface_get_version(surface_) >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION) {
            wl_surface_damage_buffer(surface_, x, y, width, height);
        } else {
            wl_surface_damage(surface_, x, y, width, height);
        }
    }
}

void SessionLockSurface::handle_configure(uint32_t serial, uint32_t width, uint32_t height) {
    configured_serial_ = serial;
    configured_size_ = {static_cast<int32_t>(width), static_cast<int32_t>(height)};

    if (display_) {
        display_->events().push(
            SessionLockSurfaceConfigureEvent{id_, output_id_, serial, width, height});
    }
}

}  // namespace browl
