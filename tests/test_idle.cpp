#include "browl/display.h"
#include "browl/idle_inhibit.h"
#include "browl/idle_notify.h"
#include "browl/seat.h"
#include "headless_compositor.h"

#include <wayland-client.h>

#include <cassert>
#include <iostream>

using namespace browl;
using namespace browl::test;

int main() {
    std::cout << "Running test_idle..." << std::endl;

    HeadlessCompositor server;
    server.start();

    int client_fd = server.create_client_fd();
    assert(client_fd >= 0);

    auto display = Display::connect_to_fd(client_fd);
    assert(display != nullptr);

    // 1. Idle Inhibit
    wl_surface* surface = display->create_surface();
    assert(surface != nullptr);

    auto inhibitor = display->create_idle_inhibitor(surface);
    assert(inhibitor != nullptr);
    assert(inhibitor->zwp_inhibitor_ptr() != nullptr);

    display->roundtrip();
    assert(server.idle_inhibitor_created());

    // Destroy inhibitor RAII
    inhibitor.reset();
    display->roundtrip();
    assert(server.idle_inhibitor_destroyed());

    wl_surface_destroy(surface);

    // 2. Idle Notification
    auto seat = display->default_seat();
    assert(seat != nullptr);

    auto notif = display->create_idle_notification(3000, seat.get());
    assert(notif != nullptr);
    assert(notif->ext_notification_ptr() != nullptr);
    assert(!notif->is_idled());

    display->roundtrip();

    // Compositor reports idle
    server.fire_idle();
    display->roundtrip();

    assert(notif->is_idled());
    auto snap = notif->snapshot();
    assert(snap.idled);

    auto events = display->events().drain();
    bool found_idled = false;
    for (const auto& ev : events) {
        if (std::holds_alternative<IdleNotificationIdledEvent>(ev)) {
            if (std::get<IdleNotificationIdledEvent>(ev).id == notif->id()) {
                found_idled = true;
            }
        }
    }
    assert(found_idled);

    // Compositor reports resume
    server.fire_resume();
    display->roundtrip();

    assert(!notif->is_idled());
    snap = notif->snapshot();
    assert(!snap.idled);

    events = display->events().drain();
    bool found_resumed = false;
    for (const auto& ev : events) {
        if (std::holds_alternative<IdleNotificationResumedEvent>(ev)) {
            if (std::get<IdleNotificationResumedEvent>(ev).id == notif->id()) {
                found_resumed = true;
            }
        }
    }
    assert(found_resumed);

    notif.reset();
    display.reset();
    server.stop();

    std::cout << "test_idle passed!" << std::endl;
    return 0;
}
