#pragma once

#include "browl/events.h"
#include "browl/types.h"

#include <memory>
#include <vector>

struct ext_session_lock_v1;
struct ext_session_lock_surface_v1;
struct ext_session_lock_manager_v1;
struct wl_surface;
struct wl_buffer;

namespace browl {

class Display;
class Output;
class SessionLockSurface;

class SessionLock {
public:
    SessionLock(ext_session_lock_v1* lock, Display* display);
    ~SessionLock();

    SessionLock(const SessionLock&) = delete;
    SessionLock& operator=(const SessionLock&) = delete;

    ext_session_lock_v1* ext_lock_ptr() const { return lock_; }

    SessionLockSnapshot snapshot() const;
    bool is_locked() const { return locked_; }
    bool is_finished() const { return finished_; }

    std::unique_ptr<SessionLockSurface> create_surface(Output& output);
    void unlock_and_destroy();

    // Internal listener callbacks
    void handle_locked();
    void handle_finished();

private:
    ext_session_lock_v1* lock_ = nullptr;
    Display* display_ = nullptr;
    bool locked_ = false;
    bool finished_ = false;
};

class SessionLockSurface {
public:
    SessionLockSurface(SurfaceId id, OutputId output_id, wl_surface* surface,
                       ext_session_lock_surface_v1* lock_surface, Display* display);
    ~SessionLockSurface();

    SessionLockSurface(const SessionLockSurface&) = delete;
    SessionLockSurface& operator=(const SessionLockSurface&) = delete;

    SurfaceId id() const { return id_; }
    OutputId output_id() const { return output_id_; }
    SessionLockSurfaceSnapshot snapshot() const;

    Size configured_size() const { return configured_size_; }
    uint32_t configured_serial() const { return configured_serial_; }

    void ack_configure(uint32_t serial);
    void commit();
    void attach_buffer(wl_buffer* buffer, int32_t x = 0, int32_t y = 0);
    void damage(int32_t x, int32_t y, int32_t width, int32_t height);

    wl_surface* wl_surface_ptr() const { return surface_; }
    ext_session_lock_surface_v1* ext_lock_surface_ptr() const { return lock_surface_; }

    // Internal listener callbacks
    void handle_configure(uint32_t serial, uint32_t width, uint32_t height);

private:
    SurfaceId id_ = kNoSurface;
    OutputId output_id_ = kNoOutput;
    wl_surface* surface_ = nullptr;
    ext_session_lock_surface_v1* lock_surface_ = nullptr;
    Display* display_ = nullptr;

    Size configured_size_;
    uint32_t configured_serial_ = 0;
};

}  // namespace browl
