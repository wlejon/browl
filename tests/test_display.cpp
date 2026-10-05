#include "browl/display.h"
#include "browl/output.h"
#include "browl/seat.h"
#include "headless_compositor.h"

#include <cassert>
#include <iostream>

using namespace browl;
using namespace browl::test;

int main() {
    std::cout << "Running test_display..." << std::endl;

    HeadlessCompositor server;
    server.start();

    int client_fd = server.create_client_fd();
    assert(client_fd >= 0);

    auto display = Display::connect_to_fd(client_fd);
    assert(display != nullptr);
    assert(display->fd() >= 0);

    // Capabilities check
    assert(display->has_compositor());
    assert(display->has_shm());
    assert(display->has_layer_shell());
    assert(display->has_xdg_shell());
    assert(display->has_foreign_toplevel_manager());
    assert(display->has_session_lock());
    assert(display->has_idle_inhibit());
    assert(display->has_screencopy());
    assert(display->has_idle_notify());

    // Flush and roundtrip
    assert(display->flush() >= 0);
    assert(display->roundtrip() >= 0);

    // Outputs verification
    auto outputs = display->outputs();
    assert(!outputs.empty());
    auto out = display->default_output();
    assert(out != nullptr);
    assert(out->name() == "DP-1");
    auto out_snap = out->snapshot();
    assert(out_snap.name == "DP-1");
    assert(out_snap.geometry.width == 1920);
    assert(out_snap.geometry.height == 1080);
    assert(out_snap.scale == 1);

    // Seats verification
    auto seats = display->seats();
    assert(!seats.empty());
    auto seat = display->default_seat();
    assert(seat != nullptr);
    assert(seat->name() == "seat0");
    auto seat_snap = seat->snapshot();
    assert(seat_snap.has_pointer);
    assert(seat_snap.has_keyboard);

    // EventQueue drained
    auto events = display->events().drain();
    bool found_output_added = false;
    bool found_seat_added = false;
    for (const auto& ev : events) {
        if (std::holds_alternative<OutputAddedEvent>(ev)) {
            found_output_added = true;
        } else if (std::holds_alternative<SeatAddedEvent>(ev)) {
            found_seat_added = true;
        }
    }
    assert(found_output_added);
    assert(found_seat_added);

    // Prepare read and cancel
    assert(display->prepare_read());
    display->cancel_read();

    display.reset();
    server.stop();

    std::cout << "test_display passed!" << std::endl;
    return 0;
}
