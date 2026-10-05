# browl

`browl` is a standalone modern C++20 library implementing the client side of Wayland shell protocols for desktop shell components: panels, docks, application launchers, lock screens, taskbars, on-screen displays (OSD), screencapture utilities, and idle power inhibitors.

It is part of the `bro` ecosystem but is strictly standalone: usable without any desktop environment or compositor running, with zero external GUI dependencies beyond `libwayland-client`.

## Features & Protocols

`browl` wraps native Wayland shell protocols into RAII C++20 classes with immutable value snapshots and non-blocking event queues:

1. **Layer Shell (`zwlr_layer_shell_v1`)**:
   - Anchors (`Top`, `Bottom`, `Left`, `Right`), margins, exclusive zones, keyboard interactivity (`None`, `Exclusive`, `OnDemand`), layer selection (`Background`, `Bottom`, `Top`, `Overlay`).
   - Configure events, acknowledgment (`ack_configure`), dynamic commit, and associating XDG popups.
2. **XDG Popups & Positioners (`xdg_wm_base`, `xdg_positioner`, `xdg_popup`)**:
   - Menus, dropdowns, and submenus positioned relative to layer surfaces or toplevel windows.
   - Positioner configuration: anchor rect, gravity, constraint adjustments (slide, flip, resize), reactive tracking.
3. **Foreign Toplevel Management (`zwlr_foreign_toplevel_management_v1`)**:
   - Desktop taskbars and window switchers: discovering active toplevel windows across outputs.
   - Real-time snapshots: titles, application IDs, states (maximized, minimized, activated, fullscreen), output entry/exit, parent relationships.
   - Window control requests: activate, close, minimize, maximize, fullscreen.
4. **Session Lock (`ext_session_lock_v1`)**:
   - Secure screen lockers: locking protocol handshake, per-output lock surface creation, configuration acknowledgment, and atomic unlocking.
5. **Screen Copy (`zwlr_screencopy_v1`)**:
   - Output and region capture into shared memory (`wl_shm`) or DMA-BUF.
   - Format and stride negotiation, damage tracking, presentation timestamps, and frame capture events.
6. **Idle Inhibition & Notifications**:
   - `zwp_idle_inhibit_v1`: Inhibiting compositor idle timeouts during media playback or presentation.
   - `ext_idle_notify_v1`: Receiving timeout events when a seat becomes idle and resumes user activity.
7. **Value-Snapshot API & Event Queue**:
   - Thread-safe `EventQueue` (`MessageQueue<ShellEvent>`) storing immutable snapshot events.
   - Discrete event variants (`LayerConfigureEvent`, `ToplevelCreatedEvent`, `SessionLockedEvent`, etc.) draining without blocking.
   - Pollable display file descriptor integration (`prepare_read`, `read_events`, `dispatch_pending`, `flush`).

## Directory Structure

```
browl/
├── CMakeLists.txt
├── LICENSE
├── README.md
├── include/
│   └── browl/
│       ├── browl.h             # Umbrella header
│       ├── display.h           # Display connection, registry, dispatching
│       ├── event_queue.h       # Thread-safe MessageQueue<ShellEvent>
│       ├── events.h            # State snapshots and discrete events variant
│       ├── foreign_toplevel.h  # Taskbar client window tracking and management
│       ├── idle_inhibit.h      # Idle inhibitor RAII wrapper
│       ├── idle_notify.h       # Idle notifications wrapper
│       ├── layer_surface.h     # Shell surface wrapper (panels, docks, OSDs)
│       ├── output.h            # Output tracking and geometry/mode snapshots
│       ├── popup.h             # Positioner and Popup wrappers
│       ├── screencopy.h        # Frame capture manager
│       ├── seat.h              # Input seat capabilities and naming
│       ├── session_lock.h      # Lock screen protocol wrapper
│       ├── shm_pool.h          # Shared memory pool and buffer manager
│       └── types.h             # Geometry primitives, bitmasks, and enums
├── protocols/                  # Protocol XML specifications
├── src/                        # Implementation files (strictly < 1,000 lines each)
└── tests/                      # Standalone test suite with in-process headless compositor
```

## Building & Testing

### Dependencies
- C++20 compiler (GCC 12+ or Clang 15+)
- CMake 3.24+
- `libwayland-client`
- `wayland-scanner`
- `libwayland-server` (for standalone headless compositor tests)
- `threads`

### Build

```bash
cmake -B build -G Ninja
cmake --build build -j 2
```

### Run Tests

Tests run standalone without requiring any running compositor or X11/Wayland desktop session, using an in-process headless Wayland compositor over a direct UNIX `socketpair`:

```bash
ctest --test-dir build --output-on-failure
```

## License

MIT License.
