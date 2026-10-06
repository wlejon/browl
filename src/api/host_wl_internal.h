#pragma once

#include "embed/embed.h"
#include "browl/display.h"
#include "browl/events.h"
#include "browl/foreign_toplevel.h"
#include "browl/layer_surface.h"
#include "browl/output.h"
#include "browl/screencopy.h"
#include "browl/seat.h"
#include "browl/session_lock.h"

#include <memory>
#include <string>

namespace browl::api {

namespace ev = bronze::embed;
using Value = bronze::Value;

/// Returns the currently active Display (or nullptr if unavailable).
std::shared_ptr<browl::Display> activeDisplay();

/// Constructs an Error object in the current realm.
Value makeError(const std::string& msg);

/// Mounts outputs and capability inspection onto `bro.wl`.
void installOutputsOnto(Value wlObj);

/// Mounts foreign toplevel APIs onto `bro.wl`.
void installToplevelOnto(Value wlObj);

/// Mounts layer surface APIs onto `bro.wl`.
void installLayersOnto(Value wlObj);

/// Mounts screencopy APIs onto `bro.wl`.
void installScreencopyOnto(Value wlObj);

/// Mounts session lock and idle APIs onto `bro.wl`.
void installLockOnto(Value wlObj);

/// Mounts event listener APIs (`on`, `off`, `addEventListener`, etc.) onto `bro.wl`.
void installEventListenersOnto(Value wlObj);

/// Dispatches a single ShellEvent to registered JS event listeners and pending operations.
void dispatchWlShellEvent(const browl::ShellEvent& ev);

/// Drains and processes events from the active display's event queue.
void drainWlEvents();

/// Clears all registered JS event listeners.
void clearWlListeners();

/// Cleans up any pending screencopy frame captures.
void cleanupPendingCaptures();

/// Cleans up any active session locks created by the API.
void cleanupActiveLocks();

/// Cleans up any active idle inhibitors created by the API.
void cleanupActiveInhibitors();

/// Helpers to convert native snapshots into JS objects (GC-safe).
Value outputSnapshotToJs(const browl::OutputSnapshot& snap);
Value seatSnapshotToJs(const browl::SeatSnapshot& snap);
Value toplevelSnapshotToJs(const browl::ForeignToplevelSnapshot& snap);
Value layerSurfaceSnapshotToJs(const browl::LayerSurfaceSnapshot& snap);

} // namespace browl::api
