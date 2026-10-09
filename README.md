# browl

[![CI](https://github.com/wlejon/browl/actions/workflows/ci.yml/badge.svg)](https://github.com/wlejon/browl/actions/workflows/ci.yml)
[![CodeQL](https://github.com/wlejon/browl/actions/workflows/codeql.yml/badge.svg)](https://github.com/wlejon/browl/actions/workflows/codeql.yml)

A small C++20 library for the client side of the Wayland shell protocols
that desktop components speak: panels, docks and launchers (layer shell),
their menus (xdg popups), taskbars (foreign toplevel management), lock
screens (session lock), screenshot tools (screencopy), and idle handling;
and the application side: xdg-shell windows (decorations, scale, viewport,
icons, activation, presentation feedback) and seat input (pointer, keyboard
through xkbcommon, touch, text input, clipboard, primary selection, drag
and drop, cursors). Objects are RAII wrappers; what the compositor says
arrives as immutable snapshots and as events in a thread-safe queue you
drain when you like. It needs `libwayland-client`, `libwayland-cursor` and
`libxkbcommon`.

## Where it sits

Part of the **[bro](https://github.com/wlejon/bro)** desktop ecosystem (see the
[ecosystem architecture](https://github.com/wlejon/bro/blob/main/docs/ecosystem.md)).
Within the desktop stack, `browl` sits alongside
[brodisplays](https://github.com/wlejon/brodisplays) and
[brocompositor](https://github.com/wlejon/brocompositor) as the client-side
implementation of Wayland desktop shell protocols, allowing panels, docks,
launchers, lock screens, and system bars to negotiate surface roles and system
states with the compositor.

## Platform support

Wayland is a Linux display protocol, so `browl` does its real work **on Linux
only**, against a compositor that offers the protocols (wlroots-based ones
such as sway offer all of them; others offer a subset, which `Display`'s
`has_*()` queries report).

On Windows and macOS, the library configures and builds with clean stubs without
any Wayland dependencies: the entire API compiles and links unconditionally, and
`Display::connect()` fails with an explanatory error (`browl::unavailable_reason()`),
so downstream callers can link `browl` unconditionally without handing out broken
objects.

| Protocol | Interface | Bound up to |
| :--- | :--- | :--- |
| Layer shell | `zwlr_layer_shell_v1` | v4 |
| XDG shell (windows, popups) | `xdg_wm_base` | v6 |
| XDG decoration | `zxdg_decoration_manager_v1` | v1 |
| XDG output | `zxdg_output_manager_v1` | v3 |
| XDG activation | `xdg_activation_v1` | v1 |
| XDG toplevel icon | `xdg_toplevel_icon_manager_v1` | v1 |
| Viewporter, fractional scale | `wp_viewporter`, `wp_fractional_scale_manager_v1` | v1, v1 |
| Presentation time | `wp_presentation` | v2 |
| Data device (clipboard, DnD) | `wl_data_device_manager` | v3 |
| Primary selection | `zwp_primary_selection_device_manager_v1` | v1 |
| Text input | `zwp_text_input_manager_v3` | v1 |
| Cursor shape | `wp_cursor_shape_manager_v1` | v1 |
| Pointer constraints, relative pointer | `zwp_pointer_constraints_v1`, `zwp_relative_pointer_manager_v1` | v1, v1 |
| Foreign toplevel management | `zwlr_foreign_toplevel_manager_v1` | v3 |
| Session lock | `ext_session_lock_manager_v1` | v1 |
| Screencopy | `zwlr_screencopy_manager_v1` | v3 (shm buffers) |
| Idle inhibit | `zwp_idle_inhibit_manager_v1` | v1 |
| Idle notify | `ext_idle_notifier_v1` | v1 |
| Core | `wl_compositor`, `wl_shm`, `wl_output`, `wl_seat` | v6, v1, v4, v9 |

## Building

### Prerequisites

- **CMake 3.24+** and a **C++20** compiler (GCC 12+, Clang 15+, Apple Clang, MSVC 2022+).
- **Linux**: `libwayland-client`, `libwayland-cursor`, `libxkbcommon` and `wayland-scanner` (Debian/Ubuntu: `libwayland-dev libwayland-bin libxkbcommon-dev`; Arch: `wayland libxkbcommon`).
  The test suite also requires `libwayland-server` (part of `libwayland-dev`) and, for real-compositor integration tests, `sway`.
- **Windows / macOS**: No external Wayland libraries required.

### Standalone build

```bash
# Linux
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure

# Windows (MSVC) / macOS
cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CMake options:
- `BROWL_BUILD_TESTS`: Build tests (default `ON` when top-level, `OFF` when included via `add_subdirectory`).
- `BROWL_COVERAGE`: Instrument the build for gcov coverage (GCC/Clang).
- `BROWL_ENABLE_API`: Build the standalone Bronze JavaScript API (default `ON` when top-level on Linux; bronze, with brass, from `../bronze` beside the top-level project or the pinned commit, fetched at configure by `cmake/bro_deps.cmake`).

### Consuming browl

Downstream projects consume the `browl::browl` CMake target. Ecosystem
consumers pin it with `bro_dependency()` (`cmake/bro_deps.cmake`): a target the
outer project already added wins, else a `../browl` working tree beside the
top-level project, else the pinned commit, fetched at configure
(`-DFETCHCONTENT_SOURCE_DIR_BROWL=<path>` points at another tree):

```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bro_deps.cmake)
bro_dependency(browl GITHUB wlejon/browl REF <40-hex sha>)

target_link_libraries(your_target PRIVATE browl::browl)
```

## API overview

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

| Header | Contents |
| :--- | :--- |
| `display.h` | `Display`: connect, dispatch, capability queries, factories, outputs and seats |
| `layer_surface.h` | `LayerSurface`: layer, anchors, margins, exclusive zone, keyboard interactivity, configure/ack, popups |
| `popup.h` | `Positioner`, `Popup`: anchor rect, gravity, constraint adjustment, reposition, grab |
| `foreign_toplevel.h` | `ForeignToplevelManager`, `ForeignToplevel`: title, app id, states, outputs, parent; activate, close, (un)maximize, (un)minimize, (un)fullscreen |
| `session_lock.h` | `SessionLock`, `SessionLockSurface`: lock, per-output lock surfaces, `unlock_and_destroy()` |
| `screencopy.h` | `ScreenCopyManager`, `ScreenCopyFrame`: output or region capture into shm, flags, damage, timestamps |
| `idle_inhibit.h`, `idle_notify.h` | `IdleInhibitor`, `IdleNotification` (idled / resumed) |
| `shm_pool.h` | `ShmPool`, `ShmBuffer`: memfd-backed `wl_shm` pools that grow without invalidating existing buffers |
| `window.h` | `Window`: xdg toplevel with title, app id, size limits, decorations, maximize/fullscreen, icon, scale (fractional or integer), viewport, map/unmap, configure acks |
| `keymap.h` | `Keymap`: the seat's xkb keymap, thread-safe keysym and modifier queries |
| `output.h`, `seat.h` | `Output`, `Seat`: snapshots of what the compositor announces; seat input (pointer, keyboard, touch, text input), cursors, selections, drag and drop |
| `events.h`, `event_queue.h` | Snapshot types, the `ShellEvent` variant, `EventQueue` |
| `browl.h` | Master umbrella header |

Session lock semantics worth knowing: only `unlock_and_destroy()` unlocks.
Destroying a `SessionLock` that is locked leaves the session locked (the
compositor displays a solid colour once the client is gone), as the protocol
intends for a crashed or misbehaving locker.

## Tests

`tests/check.h` holds the test assertions (active in every configuration, no
`assert()`). Tests that cannot run in the current environment exit with code 77
and ctest reports them as skipped, never passed.

| Test | Platform | Target / Environment | Oracle |
| :--- | :--- | :--- | :--- |
| `test_event_queue` | everywhere | — | Ordering, timed waits, wake hook, four producers against a draining consumer |
| `test_unavailable` | Windows, macOS | — | `connect()` fails with `unavailable_reason()` |
| `test_display`, `test_layer_surface`, `test_popup`, `test_foreign_toplevel`, `test_session_lock`, `test_idle`, `test_screencopy`, `test_shm_pool`, `test_window`, `test_input`, `test_selection` | Linux | `HeadlessCompositor` (test double) | What browl puts on the wire and how it turns scripted events into snapshots and queue entries; for `test_shm_pool`, the pool's own memory |
| `test_sway_shell` | Linux with sway | headless sway | Sizes sway configures (output size; the space an exclusive zone leaves), where it places a popup and slides it back on screen, and the composited pixels read back with screencopy (whole output and a region) |
| `test_sway_toplevel` | Linux with sway | headless sway + a second client's window | The window as sway reports it (title, app id, output, activation, retitle); fullscreen and close requested through browl arriving at the window |
| `test_keymap` | Linux | — | Keymap compiled from text: keysyms, modifiers, compose |
| `test_sway_window` | Linux with sway | headless sway + a virtual keyboard + a second browl client | Configured size, decorations (server-side; client-side when floating), keyboard focus and keys through sway's keymap, clipboard and primary selection between clients, presentation feedback, activation tokens raising a window, unmap/map |
| `test_sway_lock_idle` | Linux with sway | headless sway | Idled after the timeout and not while a visible surface inhibits; the lock surface is what the output shows; a second lock is refused while one holds and accepted after unlock; dropping a locked lock does not reveal the desktop |
| `browl_test_api` | Linux (when API enabled) | `HeadlessCompositor` | Bronze JavaScript bindings (`browl_api`) and garbage collector stress testing |

### Test fixtures & CI skipping

- **`HeadlessCompositor`** (`tests/headless_compositor*.cpp`): A custom, hand-written
  in-process `wayland-server` that advertises every global browl binds and sends
  exact scripted protocol events. It checks browl's wire marshalling and event
  handling and exercises events that a real compositor cannot be made to send on demand
  (`closed`, `popup_done`, `finished`, `resumed`, failed capture).
- **Headless sway tests**: `tests/run-headless-sway.sh` launches a private,
  headless sway instance per test (with its own isolated `XDG_RUNTIME_DIR`, the
  pixman software renderer, and no physical input devices) passed via
  `BROWL_TEST_WAYLAND_DISPLAY`. Tests never touch the desktop's `$WAYLAND_DISPLAY`.
  When sway or `wayland-server` is absent, these tests cleanly exit 77 (skipped).
- **Not exercised against a real compositor**: The `resumed` idle event (sway
  has no simulated input devices in this configuration), popup grabs (requiring input),
  maximize/minimize through foreign toplevel (not implemented by sway), and keyboard
  interactivity.
- **CI**: Runs the Linux test suite (including sway) on Ubuntu 24.04 (sway 1.9,
  wlroots 0.17). Verified also on Arch Linux with sway 1.12. Each job's log ends
  with the skipped tests and the exact reasons reported (`.github/ci/ctest.sh`).

## License

MIT, see [LICENSE](LICENSE).
