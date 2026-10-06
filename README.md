# browl

[![CI](https://github.com/wlejon/browl/actions/workflows/ci.yml/badge.svg)](https://github.com/wlejon/browl/actions/workflows/ci.yml)
[![CodeQL](https://github.com/wlejon/browl/actions/workflows/codeql.yml/badge.svg)](https://github.com/wlejon/browl/actions/workflows/codeql.yml)

A small C++20 library for the client side of the Wayland shell protocols
that desktop components speak: panels, docks and launchers (layer shell),
their menus (xdg popups), taskbars (foreign toplevel management), lock
screens (session lock), screenshot tools (screencopy), and idle handling.
Objects are RAII wrappers; what the compositor says arrives as immutable
snapshots and as events in a thread-safe queue you drain when you like.
It needs nothing but `libwayland-client`.

Part of the **[bro](https://github.com/wlejon/bro)** ecosystem, built in the
mould of [brodisplays](https://github.com/wlejon/brodisplays) and
[brocompositor](https://github.com/wlejon/brocompositor).

## Platform support

Wayland is a Linux display protocol, so browl does its job **on Linux
only**, against a compositor that offers the protocols (wlroots-based ones
such as sway offer all of them; others offer a subset, which `Display`'s
`has_*()` queries report).

On Windows and macOS the library configures and builds with no Wayland
dependencies, the whole API compiles and links, and `Display::connect()`
fails with the reason (`browl::unavailable_reason()`), so nothing is ever
handed out.

| Protocol | Interface | Bound up to |
| :--- | :--- | :--- |
| Layer shell | `zwlr_layer_shell_v1` | v4 |
| XDG shell (popups) | `xdg_wm_base` | v5 |
| Foreign toplevel management | `zwlr_foreign_toplevel_manager_v1` | v3 |
| Session lock | `ext_session_lock_manager_v1` | v1 |
| Screencopy | `zwlr_screencopy_manager_v1` | v3 (shm buffers) |
| Idle inhibit | `zwp_idle_inhibit_manager_v1` | v1 |
| Idle notify | `ext_idle_notifier_v1` | v1 |
| Core | `wl_compositor`, `wl_shm`, `wl_output`, `wl_seat` | v4, v1, v4, v7 |

## Build

```bash
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release   # Linux
cmake --build build-release
ctest --test-dir build-release --output-on-failure

cmake -B build                                               # Windows (VS)
cmake --build build --config Release
```

Linux needs `libwayland-client` and `wayland-scanner` (Debian/Ubuntu:
`libwayland-dev libwayland-bin`); the tests also need `libwayland-server`
(same package) and, for the real-compositor tests, `sway`. The protocol XML
is in `protocols/`. Options: `BROWL_BUILD_TESTS` (on when top level),
`BROWL_COVERAGE` (gcov instrumentation, GCC/Clang). Consumers use the
`browl::browl` target via `add_subdirectory`.

## API

```cpp
#include <browl/browl.h>

std::string error;
auto display = browl::Display::connect("", &error);   // $WAYLAND_DISPLAY
if (!display) return fail(error);

// A 30 px panel along the top that keeps other surfaces out of its way.
browl::LayerSurfaceConfig cfg;
cfg.name_space = "panel";
cfg.layer = browl::Layer::Top;
cfg.anchor = browl::Anchor::Top | browl::Anchor::Left | browl::Anchor::Right;
cfg.size = {0, 30};
cfg.exclusive_zone = 30;
auto panel = display->create_layer_surface(cfg);
panel->commit();

// Pump the connection however your loop likes (fd(), prepare_read(),
// read_events(), dispatch_pending(), or just roundtrip()), then drain.
display->roundtrip();
for (auto& ev : display->events().drain()) {
    if (auto* c = std::get_if<browl::LayerConfigureEvent>(&ev)) {
        auto pool = display->create_shm_pool(c->width * c->height * 4);
        auto buf = pool->allocate_buffer(c->width, c->height, c->width * 4, 0 /* ARGB8888 */);
        // ... draw into buf->data() ...
        panel->ack_configure(c->serial);
        panel->attach_buffer(buf->wl_buffer_ptr());
        panel->damage(0, 0, c->width, c->height);
        panel->commit();
    }
}
```

| Header | What |
| :--- | :--- |
| `display.h` | `Display`: connect, dispatch, capability queries, factories, outputs and seats |
| `layer_surface.h` | `LayerSurface`: layer, anchors, margins, exclusive zone, keyboard interactivity, configure/ack, popups |
| `popup.h` | `Positioner`, `Popup`: anchor rect, gravity, constraint adjustment, reposition, grab |
| `foreign_toplevel.h` | `ForeignToplevelManager`, `ForeignToplevel`: title, app id, states, outputs, parent; activate, close, (un)maximize, (un)minimize, (un)fullscreen |
| `session_lock.h` | `SessionLock`, `SessionLockSurface`: lock, per-output lock surfaces, `unlock_and_destroy()` |
| `screencopy.h` | `ScreenCopyManager`, `ScreenCopyFrame`: output or region capture into shm, flags, damage, timestamps |
| `idle_inhibit.h`, `idle_notify.h` | `IdleInhibitor`, `IdleNotification` (idled / resumed) |
| `shm_pool.h` | `ShmPool`, `ShmBuffer`: memfd-backed `wl_shm` pools that grow without invalidating existing buffers |
| `output.h`, `seat.h` | `Output`, `Seat`: snapshots of what the compositor announces |
| `events.h`, `event_queue.h` | snapshot types, the `ShellEvent` variant, `EventQueue` |

Session lock semantics worth knowing: only `unlock_and_destroy()` unlocks.
Destroying a `SessionLock` that is locked leaves the session locked (the
compositor shows a solid colour once the client is gone), as the protocol
intends for a crashed or misbehaving locker.

## Tests

`tests/check.h` holds the checks (real in every configuration, no
`assert()`). A test that cannot run exits 77 with the reason and ctest
reports it skipped, never passed.

| Test | Where | Against | Oracle |
| :--- | :--- | :--- | :--- |
| `test_event_queue` | everywhere | — | ordering, timed waits, wake hook, four producers against a draining consumer |
| `test_unavailable` | Windows, macOS | — | `connect()` fails with `unavailable_reason()` |
| `test_display`, `test_layer_surface`, `test_popup`, `test_foreign_toplevel`, `test_session_lock`, `test_idle`, `test_screencopy`, `test_shm_pool` | Linux | `HeadlessCompositor` (test double) | what browl puts on the wire and how it turns scripted events into snapshots and queue entries; for `test_shm_pool`, the pool's own memory |
| `test_sway_shell` | Linux with sway | headless sway | sizes sway configures (output size; the space an exclusive zone leaves), where it places a popup and slides it back on screen, and the composited pixels read back with screencopy (whole output and a region) |
| `test_sway_toplevel` | Linux with sway | headless sway + a second client's window | the window as sway reports it (title, app id, output, activation, retitle); fullscreen and close requested through browl arriving at the window |
| `test_sway_lock_idle` | Linux with sway | headless sway | idled after the timeout and not while a visible surface inhibits; the lock surface is what the output shows; a second lock is refused while one holds and accepted after unlock; dropping a locked lock does not reveal the desktop |

`HeadlessCompositor` (`tests/headless_compositor*.cpp`) is a hand-written
wayland-server that advertises every global browl binds and sends exactly
the events a test scripts. It checks browl's marshalling and event handling
and covers events a real compositor cannot be made to send on demand
(`closed`, `popup_done`, `finished`, `resumed`, a failed capture), but it
does not validate requests the way a compositor does. The `test_sway_*`
tests are the real check: `tests/run-headless-sway.sh` starts a private
headless sway per test (its own `XDG_RUNTIME_DIR`, the pixman renderer, no
input devices) and hands it to the test as `BROWL_TEST_WAYLAND_DISPLAY`;
the tests never touch the desktop's `$WAYLAND_DISPLAY`. Without sway
installed they skip.

Not exercised against a real compositor: the `resumed` idle event (sway
has no input devices to wake it here), popup grabs (need input),
maximize/minimize through foreign toplevel (sway does not implement them),
and keyboard interactivity.

CI (`.github/workflows/ci.yml`) runs the Linux tests, sway included, on
Ubuntu 24.04 (sway 1.9, wlroots 0.17); they are also verified on Arch with
sway 1.12. Each job's log ends with the tests and checks it skipped, and why
(`.github/ci/ctest.sh`).

## License

MIT, see [LICENSE](LICENSE).
