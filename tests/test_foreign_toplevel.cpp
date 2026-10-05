#include "browl/display.h"
#include "browl/foreign_toplevel.h"
#include "browl/seat.h"
#include "headless_compositor.h"

#include <cassert>
#include <iostream>

using namespace browl;
using namespace browl::test;

int main() {
    std::cout << "Running test_foreign_toplevel..." << std::endl;

    HeadlessCompositor server;
    server.start();

    int client_fd = server.create_client_fd();
    assert(client_fd >= 0);

    auto display = Display::connect_to_fd(client_fd);
    assert(display != nullptr);

    auto mgr = display->foreign_toplevel_manager();
    assert(mgr != nullptr);
    assert(mgr->zwlr_manager_ptr() != nullptr);
    assert(mgr->toplevels().empty());

    // Compositor announces a new toplevel window
    // State 4 = Activated
    auto* comp_top = server.create_foreign_toplevel("Terminal", "org.bro.terminal", 4);
    assert(comp_top != nullptr);
    display->roundtrip();

    auto tops = mgr->toplevels();
    assert(tops.size() == 1);
    auto top = tops.front();
    assert(top != nullptr);
    assert(top->title() == "Terminal");
    assert(top->app_id() == "org.bro.terminal");
    assert(top->is_activated());
    assert(!top->is_maximized());
    assert(!top->is_minimized());
    assert(!top->is_fullscreen());

    auto snaps = mgr->snapshots();
    assert(snaps.size() == 1);
    assert(snaps.front().title == "Terminal");
    assert(snaps.front().app_id == "org.bro.terminal");

    // Verify ToplevelCreatedEvent in event queue
    auto events = display->events().drain();
    bool found_created = false;
    for (const auto& ev : events) {
        if (std::holds_alternative<ToplevelCreatedEvent>(ev)) {
            if (std::get<ToplevelCreatedEvent>(ev).toplevel.id == top->id()) {
                found_created = true;
            }
        }
    }
    assert(found_created);

    // Title and state changes
    server.update_toplevel_title(comp_top, "Terminal - vim");
    // State 1 | 4 = Maximized | Activated
    server.update_toplevel_state(comp_top, 1 | 4);
    display->roundtrip();

    assert(top->title() == "Terminal - vim");
    assert(top->is_maximized());
    assert(top->is_activated());

    // Client requests: activate and minimize
    auto seat = display->default_seat();
    assert(seat != nullptr);
    top->activate(*seat);
    top->set_minimized();
    top->close();
    display->roundtrip();

    assert(comp_top->activated);
    assert(comp_top->minimized);
    assert(comp_top->closed);

    // Close toplevel from compositor
    server.close_toplevel(comp_top);
    display->roundtrip();

    assert(mgr->toplevels().empty());
    assert(mgr->snapshots().empty());

    events = display->events().drain();
    bool found_closed = false;
    for (const auto& ev : events) {
        if (std::holds_alternative<ToplevelClosedEvent>(ev)) {
            if (std::get<ToplevelClosedEvent>(ev).id == top->id()) {
                found_closed = true;
            }
        }
    }
    assert(found_closed);

    display.reset();
    server.stop();

    std::cout << "test_foreign_toplevel passed!" << std::endl;
    return 0;
}
