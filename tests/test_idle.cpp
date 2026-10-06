// IdleInhibitor and IdleNotification against the in-process test-double
// compositor (fake_session.h): object lifetimes and idled/resumed delivery.
// Real idle timing and inhibition are checked in test_sway.
#include "browl/display.h"
#include "browl/idle_inhibit.h"
#include "browl/idle_notify.h"
#include "browl/seat.h"
#include "fake_session.h"

#include <wayland-client.h>

using namespace browl;
using bstest::find_event;

namespace {

void test_inhibitor() {
    bstest::FakeSession s;
    REQUIRE(s.display != nullptr);
    Display& display = *s.display;

    CHECK(display.create_idle_inhibitor(nullptr) == nullptr);
    wl_surface* surface = display.create_surface();
    REQUIRE(surface != nullptr);
    auto inhibitor = display.create_idle_inhibitor(surface);
    REQUIRE(inhibitor != nullptr);
    CHECK(inhibitor->zwp_inhibitor_ptr() != nullptr);
    CHECK(display.roundtrip() >= 0);
    CHECK(s.server.idle_inhibitor_created());
    CHECK(!s.server.idle_inhibitor_destroyed());

    inhibitor.reset();
    CHECK(display.roundtrip() >= 0);
    CHECK(s.server.idle_inhibitor_destroyed());
    wl_surface_destroy(surface);
}

void test_notification() {
    bstest::FakeSession s;
    REQUIRE(s.display != nullptr);
    Display& display = *s.display;
    auto seat = display.default_seat();
    REQUIRE(seat != nullptr);

    // (The fake tracks one notification: the latest created.)
    auto other = display.create_idle_notification(5000, seat.get());
    REQUIRE(other != nullptr);
    const uint64_t other_id = other->id();
    other.reset();
    auto notif = display.create_idle_notification(3000, seat.get());
    REQUIRE(notif != nullptr);
    CHECK(notif->id() != other_id);
    CHECK(notif->ext_notification_ptr() != nullptr);
    CHECK(!notif->is_idled());
    CHECK(display.roundtrip() >= 0);

    s.server.fire_idle();
    CHECK(display.roundtrip() >= 0);
    CHECK(notif->is_idled());
    CHECK(notif->snapshot().idled);
    auto events = display.events().drain();
    CHECK(find_event<IdleNotificationIdledEvent>(events, [&](const IdleNotificationIdledEvent& e) {
              return e.id == notif->id();
          }) != nullptr);

    s.server.fire_resume();
    CHECK(display.roundtrip() >= 0);
    CHECK(!notif->is_idled());
    events = display.events().drain();
    CHECK(find_event<IdleNotificationResumedEvent>(events, [&](const IdleNotificationResumedEvent& e) {
              return e.id == notif->id();
          }) != nullptr);
}

}  // namespace

int main() {
    test_inhibitor();
    test_notification();
    return bstest::finish("test_idle");
}
