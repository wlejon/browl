// Idle notification, idle inhibition and the session lock against a real
// compositor: a private headless sway (run-headless-sway.sh) with no input
// devices, so the seat is idle from the start. Oracles: idled arrives after
// the timeout and not while a visible surface inhibits idle; a lock surface
// configured to the output is what the output shows while locked; a second
// lock is refused while the first holds and accepted once it is released;
// dropping a locked SessionLock does not reveal the desktop.
#include "browl/browl.h"
#include "sway_harness.h"

#include <wayland-client.h>

#include <chrono>

using namespace browl;
using swaytest::EventLog;

namespace {

constexpr uint32_t kDesktop = 0xff3366ccu, kLock = 0xff101010u;

using Clock = std::chrono::steady_clock;

void test_idle(Display& d, Output& out, ShmPool& pool, std::unique_ptr<LayerSurface>& desktop,
               std::shared_ptr<ShmBuffer>& desktop_buf) {
    auto seat = d.default_seat();
    REQUIRE(seat != nullptr);

    // No input devices: the notification fires once its timeout passes.
    const auto t0 = Clock::now();
    auto idle = d.create_idle_notification(300, seat.get());
    REQUIRE(idle != nullptr);
    REQUIRE(swaytest::pump(d, [&] { return idle->is_idled(); }, 5000));
    const auto waited = Clock::now() - t0;
    std::printf("idled after %lld ms (timeout 300 ms)\n",
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(waited).count()));
    CHECK(waited >= std::chrono::milliseconds(250));
    idle.reset();

    // A visible surface with an inhibitor: no idled while it lives.
    LayerSurfaceConfig cfg;
    cfg.name_space = "browl-test-desktop";
    cfg.layer = Layer::Bottom;
    cfg.anchor = Anchor::Top | Anchor::Bottom | Anchor::Left | Anchor::Right;
    cfg.output = &out;
    desktop = d.create_layer_surface(cfg);
    REQUIRE(desktop != nullptr);
    desktop->commit();
    EventLog log;
    const SurfaceId id = desktop->id();
    auto mine = [id](const LayerConfigureEvent& e) { return e.surface_id == id; };
    REQUIRE(swaytest::pump(d, [&] {
        log.take(d);
        return log.find<LayerConfigureEvent>(mine) != nullptr;
    }));
    const auto* conf = log.find<LayerConfigureEvent>(mine);
    desktop_buf = swaytest::solid_buffer(pool, int32_t(conf->width), int32_t(conf->height), kDesktop);
    REQUIRE(desktop_buf != nullptr);
    desktop->ack_configure(conf->serial);
    desktop->attach_buffer(desktop_buf->wl_buffer_ptr());
    desktop->damage(0, 0, int32_t(conf->width), int32_t(conf->height));
    REQUIRE(swaytest::commit_and_wait_frame(d, desktop->wl_surface_ptr()));

    auto inhibitor = d.create_idle_inhibitor(desktop->wl_surface_ptr());
    REQUIRE(inhibitor != nullptr);
    CHECK(d.roundtrip() >= 0);
    auto inhibited = d.create_idle_notification(300, seat.get());
    REQUIRE(inhibited != nullptr);
    CHECK(!swaytest::pump(d, [&] { return inhibited->is_idled(); }, 1500));

    // Released: the timer runs again.
    inhibitor.reset();
    CHECK(swaytest::pump(d, [&] { return inhibited->is_idled(); }, 5000));
    log.take(d);
    CHECK(log.find<IdleNotificationIdledEvent>([&](const IdleNotificationIdledEvent& e) {
              return e.id == inhibited->id();
          }) != nullptr);
}

// Locks the session with one lock surface on `out`. Returns the lock once
// the compositor says locked (or finished).
std::unique_ptr<SessionLock> lock_session(Display& d, Output& out, ShmPool& pool,
                                          std::unique_ptr<SessionLockSurface>& surface,
                                          std::shared_ptr<ShmBuffer>& buf) {
    auto lock = d.create_session_lock();
    if (!lock) return nullptr;
    surface = lock->create_surface(out);
    if (!surface) return lock;
    swaytest::pump(d, [&] { return surface->configured_serial() != 0 || lock->is_finished(); });
    if (lock->is_finished()) return lock;
    const Size sz = surface->configured_size();
    buf = swaytest::solid_buffer(pool, sz.width, sz.height, kLock);
    if (!buf) return lock;
    surface->ack_configure(surface->configured_serial());
    surface->attach_buffer(buf->wl_buffer_ptr());
    surface->damage(0, 0, sz.width, sz.height);
    surface->commit();
    swaytest::pump(d, [&] { return lock->is_locked() || lock->is_finished(); }, 5000);
    return lock;
}

void test_lock(Display& d, Output& out, ShmPool& pool) {
    const uint32_t cx = uint32_t(out.current_mode().width / 2), cy = uint32_t(out.current_mode().height / 2);
    auto before = swaytest::capture(d, out);
    REQUIRE(before.error.empty());
    REQUIRE(before.at(cx, cy) == kDesktop);

    std::unique_ptr<SessionLockSurface> surface;
    std::shared_ptr<ShmBuffer> buf;
    auto lock = lock_session(d, out, pool, surface, buf);
    REQUIRE(lock != nullptr && surface != nullptr);
    CHECK(surface->configured_size() == (Size{out.current_mode().width, out.current_mode().height}));
    REQUIRE(lock->is_locked());
    CHECK(!lock->is_finished());

    // The output shows the lock surface, not the desktop under it.
    auto locked = swaytest::capture(d, out);
    REQUIRE(locked.error.empty());
    CHECK_EQ(locked.at(cx, cy), kLock);
    CHECK_EQ(locked.at(0, 0), kLock);

    // Only one lock at a time: a second is refused while the first holds.
    auto second = d.create_session_lock();
    REQUIRE(second != nullptr);
    CHECK(swaytest::pump(d, [&] { return second->is_finished() || second->is_locked(); }));
    CHECK(second->is_finished());
    CHECK(!second->is_locked());
    second->unlock_and_destroy();  // destroy, since it never locked

    // Unlock: the desktop is back, and a new lock is accepted.
    surface.reset();
    lock->unlock_and_destroy();
    CHECK(swaytest::pump(d, [&] {
        auto c = swaytest::capture(d, out);
        return c.error.empty() && c.at(cx, cy) == kDesktop;
    }));
    std::unique_ptr<SessionLockSurface> surface3;
    std::shared_ptr<ShmBuffer> buf3;
    auto third = lock_session(d, out, pool, surface3, buf3);
    REQUIRE(third != nullptr);
    CHECK(third->is_locked());

    // Dropping a locked SessionLock (no unlock request) must not reveal the
    // desktop, and must not cost the client its connection.
    third.reset();
    surface3.reset();
    CHECK(d.roundtrip() >= 0);
    auto dropped = swaytest::capture(d, out);
    REQUIRE(dropped.error.empty());
    CHECK(dropped.at(cx, cy) != kDesktop);
    std::printf("after dropping a locked lock the output shows 0x%08x\n", dropped.at(cx, cy));
}

}  // namespace

int main() {
    auto display = swaytest::connect_or_skip("test_sway_lock_idle");
    if (display) {
        auto out = display->default_output();
        auto pool = display->create_shm_pool(4096);
        if (out && pool) {
            std::unique_ptr<LayerSurface> desktop;
            std::shared_ptr<ShmBuffer> desktop_buf;
            test_idle(*display, *out, *pool, desktop, desktop_buf);
            if (desktop) test_lock(*display, *out, *pool);
        } else {
            bstest::fail(__FILE__, __LINE__, "no output or shm pool");
        }
    }
    return bstest::finish("test_sway_lock_idle");
}
