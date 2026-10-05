#include "browl/display.h"
#include "browl/output.h"
#include "browl/session_lock.h"
#include "headless_compositor.h"

#include <cassert>
#include <iostream>

using namespace browl;
using namespace browl::test;

int main() {
    std::cout << "Running test_session_lock..." << std::endl;

    HeadlessCompositor server;
    server.start();

    int client_fd = server.create_client_fd();
    assert(client_fd >= 0);

    auto display = Display::connect_to_fd(client_fd);
    assert(display != nullptr);

    auto lock = display->create_session_lock();
    assert(lock != nullptr);
    assert(lock->ext_lock_ptr() != nullptr);

    display->roundtrip();
    assert(server.session_lock_created());

    // Compositor emits locked event
    server.send_session_locked();
    display->roundtrip();

    assert(lock->is_locked());
    assert(!lock->is_finished());

    auto snap = lock->snapshot();
    assert(snap.locked);
    assert(!snap.finished);

    auto events = display->events().drain();
    bool found_locked = false;
    for (const auto& ev : events) {
        if (std::holds_alternative<SessionLockedEvent>(ev)) {
            found_locked = true;
        }
    }
    assert(found_locked);

    // Create lock surface for output
    auto output = display->default_output();
    assert(output != nullptr);

    auto lock_surface = lock->create_surface(*output);
    assert(lock_surface != nullptr);
    assert(lock_surface->wl_surface_ptr() != nullptr);
    assert(lock_surface->ext_lock_surface_ptr() != nullptr);

    display->roundtrip();
    assert(server.lock_surface_created());

    // Compositor configures lock surface
    server.configure_lock_surface(1920, 1080);
    display->roundtrip();

    assert(lock_surface->configured_size().width == 1920);
    assert(lock_surface->configured_size().height == 1080);

    events = display->events().drain();
    bool found_surface_configure = false;
    for (const auto& ev : events) {
        if (std::holds_alternative<SessionLockSurfaceConfigureEvent>(ev)) {
            const auto& conf = std::get<SessionLockSurfaceConfigureEvent>(ev);
            if (conf.surface_id == lock_surface->id()) {
                found_surface_configure = true;
                assert(conf.width == 1920);
                assert(conf.height == 1080);
            }
        }
    }
    assert(found_surface_configure);

    // Ack configure and commit
    lock_surface->ack_configure(lock_surface->configured_serial());
    lock_surface->commit();
    display->roundtrip();

    assert(server.last_lock_surface_ack_serial() == lock_surface->configured_serial());

    // Destroy lock surfaces before unlocking according to protocol specification
    lock_surface.reset();

    // Unlock and destroy session lock
    lock->unlock_and_destroy();
    display->roundtrip();

    assert(server.session_unlock_received());

    lock.reset();
    display.reset();
    server.stop();

    std::cout << "test_session_lock passed!" << std::endl;
    return 0;
}
