// SessionLock against the in-process test-double compositor
// (fake_session.h): the lock handshake, lock surfaces, and which request ends
// a lock in each state (unlock only after locked; dropping a locked lock
// never unlocks). Locking a real compositor is checked in test_sway.
#include "browl/display.h"
#include "browl/output.h"
#include "browl/session_lock.h"
#include "fake_session.h"

using namespace browl;
using bstest::find_event;

namespace {

void test_lock_unlock() {
    bstest::FakeSession s;
    REQUIRE(s.display != nullptr);
    Display& display = *s.display;

    auto lock = display.create_session_lock();
    REQUIRE(lock != nullptr);
    CHECK(lock->ext_lock_ptr() != nullptr);
    CHECK(display.roundtrip() >= 0);
    CHECK(s.server.session_lock_created());
    CHECK(!lock->is_locked());

    s.server.send_session_locked();
    CHECK(display.roundtrip() >= 0);
    CHECK(lock->is_locked());
    CHECK(!lock->is_finished());
    CHECK(lock->snapshot().locked);
    auto events = display.events().drain();
    CHECK(find_event<SessionLockedEvent>(events) != nullptr);

    auto output = display.default_output();
    REQUIRE(output != nullptr);
    auto surface = lock->create_surface(*output);
    REQUIRE(surface != nullptr);
    CHECK(surface->wl_surface_ptr() != nullptr);
    CHECK(surface->ext_lock_surface_ptr() != nullptr);
    CHECK_EQ(surface->output_id(), output->id());
    CHECK(display.roundtrip() >= 0);
    CHECK(s.server.lock_surface_created());

    s.server.configure_lock_surface(1920, 1080);
    CHECK(display.roundtrip() >= 0);
    CHECK(surface->configured_size() == (Size{1920, 1080}));
    events = display.events().drain();
    const auto* conf = find_event<SessionLockSurfaceConfigureEvent>(
        events, [&](const SessionLockSurfaceConfigureEvent& e) { return e.surface_id == surface->id(); });
    REQUIRE(conf != nullptr);
    CHECK_EQ(conf->output_id, output->id());
    CHECK_EQ(conf->width, 1920u);
    CHECK_EQ(conf->height, 1080u);
    CHECK_EQ(conf->serial, surface->configured_serial());

    surface->ack_configure(surface->configured_serial());
    surface->commit();
    CHECK(display.roundtrip() >= 0);
    CHECK_EQ(s.server.last_lock_surface_ack_serial(), surface->configured_serial());

    surface.reset();
    CHECK(!s.server.session_unlock_received());
    lock->unlock_and_destroy();
    CHECK(display.roundtrip() >= 0);
    CHECK(s.server.session_unlock_received());
    CHECK(lock->ext_lock_ptr() == nullptr);
}

// Dropping a locked SessionLock must leave the session locked: no unlock
// request reaches the compositor, and the connection stays healthy (no
// protocol error from a destroy after locked).
void test_drop_locked_lock() {
    bstest::FakeSession s;
    REQUIRE(s.display != nullptr);
    Display& display = *s.display;
    auto lock = display.create_session_lock();
    REQUIRE(lock != nullptr);
    CHECK(display.roundtrip() >= 0);
    s.server.send_session_locked();
    CHECK(display.roundtrip() >= 0);
    REQUIRE(lock->is_locked());
    lock.reset();
    CHECK(display.roundtrip() >= 0);
    CHECK(!s.server.session_unlock_received());
}

// A lock the compositor refused (finished before locked) is destroyed, not
// unlocked.
void test_refused_lock() {
    bstest::FakeSession s;
    REQUIRE(s.display != nullptr);
    Display& display = *s.display;
    auto lock = display.create_session_lock();
    REQUIRE(lock != nullptr);
    CHECK(display.roundtrip() >= 0);
    s.server.send_session_lock_finished();
    CHECK(display.roundtrip() >= 0);
    CHECK(lock->is_finished());
    CHECK(!lock->is_locked());
    auto events = display.events().drain();
    CHECK(find_event<SessionLockFinishedEvent>(events) != nullptr);
    lock->unlock_and_destroy();
    CHECK(display.roundtrip() >= 0);
    CHECK(!s.server.session_unlock_received());
}

}  // namespace

int main() {
    test_lock_unlock();
    test_drop_locked_lock();
    test_refused_lock();
    return bstest::finish("test_session_lock");
}
