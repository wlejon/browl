// Display against the in-process test-double compositor (fake_session.h):
// globals bound, outputs and seats turned into snapshots and queue events,
// the prepare_read protocol, and connect failures reported with a reason.
#include "browl/display.h"
#include "browl/output.h"
#include "browl/seat.h"
#include "fake_session.h"

#include <wayland-client.h>

#include <unistd.h>

using namespace browl;
using bstest::find_event;

namespace {

void test_globals_outputs_seats() {
    bstest::FakeSession s;
    REQUIRE(s.display != nullptr);
    Display& display = *s.display;
    CHECK(display.fd() >= 0);

    CHECK(display.has_compositor());
    CHECK(display.has_shm());
    CHECK(display.has_layer_shell());
    CHECK(display.has_xdg_shell());
    CHECK(display.has_foreign_toplevel_manager());
    CHECK(display.has_session_lock());
    CHECK(display.has_idle_inhibit());
    CHECK(display.has_screencopy());
    CHECK(display.has_idle_notify());
    CHECK(display.flush() >= 0);
    CHECK(display.roundtrip() >= 0);

    // The fake's output: DP-1, 1920x1080 current mode, scale 1.
    REQUIRE(display.outputs().size() == 1);
    auto out = display.default_output();
    REQUIRE(out != nullptr);
    CHECK(display.output_by_id(out->id()) == out);
    CHECK(display.output_by_id(out->id() + 1000) == nullptr);
    CHECK_EQ(out->name(), std::string("DP-1"));
    const auto snap = out->snapshot();
    CHECK_EQ(snap.name, std::string("DP-1"));
    CHECK_EQ(snap.geometry.width, 1920);
    CHECK_EQ(snap.geometry.height, 1080);
    CHECK_EQ(snap.current_mode.width, 1920);
    CHECK(snap.current_mode.current);
    CHECK_EQ(snap.scale, 1);

    REQUIRE(display.seats().size() == 1);
    auto seat = display.default_seat();
    REQUIRE(seat != nullptr);
    CHECK(display.seat_by_id(seat->id()) == seat);
    CHECK_EQ(seat->name(), std::string("seat0"));
    CHECK(seat->has_pointer());
    CHECK(seat->has_keyboard());

    const auto events = display.events().drain();
    const auto* added = find_event<OutputAddedEvent>(events);
    REQUIRE(added != nullptr);
    CHECK_EQ(added->output.id, out->id());
    CHECK(find_event<OutputChangedEvent>(events, [](const OutputChangedEvent& e) {
              return e.output.name == "DP-1" && e.output.current_mode.width == 1920;
          }) != nullptr);
    CHECK(find_event<SeatAddedEvent>(events) != nullptr);

    // Seat capability changes become snapshots and events.
    s.server.send_seat_caps(WL_SEAT_CAPABILITY_TOUCH, "seat-renamed");
    CHECK(display.roundtrip() >= 0);
    CHECK(!seat->has_pointer());
    CHECK(seat->has_touch());
    CHECK_EQ(seat->name(), std::string("seat-renamed"));
    const auto changes = display.events().drain();
    CHECK(find_event<SeatChangedEvent>(changes, [](const SeatChangedEvent& e) {
              return e.seat.has_touch && !e.seat.has_pointer;
          }) != nullptr);

    // An output 'done' with nothing changed re-announces the same snapshot.
    s.server.send_output_done();
    CHECK(display.roundtrip() >= 0);
    CHECK_EQ(out->modes().size(), size_t(1));

    // A mode change, then back: the mode list holds each mode once and
    // exactly one current mode.
    s.server.send_output_mode(WL_OUTPUT_MODE_CURRENT, 1280, 720, 60000);
    CHECK(display.roundtrip() >= 0);
    CHECK_EQ(out->current_mode().width, 1280);
    CHECK_EQ(out->geometry().width, 1280);
    s.server.send_output_mode(WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, 1920, 1080, 60000);
    CHECK(display.roundtrip() >= 0);
    CHECK_EQ(out->modes().size(), size_t(2));
    int current = 0;
    for (const auto& m : out->modes()) current += m.current ? 1 : 0;
    CHECK_EQ(current, 1);
    CHECK_EQ(out->current_mode().width, 1920);

    // The read protocol: prepare, then cancel (nothing to read).
    CHECK(display.prepare_read());
    display.cancel_read();
    CHECK(display.dispatch_pending() >= 0);
}

void test_connect_failures() {
    std::string error;
    CHECK(Display::connect("browl-test-no-such-display", &error) == nullptr);
    CHECK(error.find("browl-test-no-such-display") != std::string::npos);

    // A socket nobody speaks Wayland on: the registry roundtrip fails.
    int fds[2];
    REQUIRE(::pipe(fds) == 0);
    ::close(fds[1]);
    error.clear();
    CHECK(Display::connect_to_fd(fds[0], &error) == nullptr);
    CHECK(!error.empty());
    CHECK(unavailable_reason().empty());
}

}  // namespace

int main() {
    test_globals_outputs_seats();
    test_connect_failures();
    return bstest::finish("test_display");
}
