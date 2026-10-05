#pragma once

#include "browl/types.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace browl {

// ============================================================================
// State Snapshots
// ============================================================================

struct OutputSnapshot {
    OutputId id = kNoOutput;
    std::string name;
    std::string make;
    std::string model;
    Rect geometry;
    int32_t scale = 1;
    OutputTransform transform = OutputTransform::Normal;
    Size physical_size_mm;
    std::vector<OutputMode> modes;
    OutputMode current_mode;

    bool operator==(const OutputSnapshot&) const = default;
};

struct SeatSnapshot {
    SeatId id = kNoSeat;
    std::string name;
    bool has_pointer = false;
    bool has_keyboard = false;
    bool has_touch = false;

    bool operator==(const SeatSnapshot&) const = default;
};

struct ForeignToplevelSnapshot {
    ToplevelId id = kNoToplevel;
    std::string title;
    std::string app_id;
    uint32_t state = 0;
    ToplevelId parent_id = kNoToplevel;
    std::vector<OutputId> outputs;

    bool is_activated() const { return (state & toplevel_state::Activated) != 0; }
    bool is_maximized() const { return (state & toplevel_state::Maximized) != 0; }
    bool is_minimized() const { return (state & toplevel_state::Minimized) != 0; }
    bool is_fullscreen() const { return (state & toplevel_state::Fullscreen) != 0; }

    bool operator==(const ForeignToplevelSnapshot&) const = default;
};

struct LayerSurfaceSnapshot {
    SurfaceId id = kNoSurface;
    Size configured_size;
    uint32_t configured_serial = 0;
    Layer layer = Layer::Top;
    Anchor anchor = Anchor::None;
    Margins margins;
    int32_t exclusive_zone = 0;
    KeyboardInteractivity keyboard_interactivity = KeyboardInteractivity::None;
    bool closed = false;

    bool operator==(const LayerSurfaceSnapshot&) const = default;
};

struct PopupSnapshot {
    SurfaceId id = kNoSurface;
    Rect geometry;
    bool configured = false;
    bool dismissed = false;
    uint32_t repositioned_token = 0;

    bool operator==(const PopupSnapshot&) const = default;
};

struct SessionLockSnapshot {
    bool locked = false;
    bool finished = false;

    bool operator==(const SessionLockSnapshot&) const = default;
};

struct SessionLockSurfaceSnapshot {
    SurfaceId id = kNoSurface;
    OutputId output_id = kNoOutput;
    Size configured_size;
    uint32_t configured_serial = 0;

    bool operator==(const SessionLockSurfaceSnapshot&) const = default;
};

struct ScreenCopyFrameSnapshot {
    enum class Status { Pending, Ready, Failed } status = Status::Pending;
    uint32_t format = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint32_t flags = 0;
    Rect damage;
    uint64_t tv_sec = 0;
    uint32_t tv_nsec = 0;

    bool operator==(const ScreenCopyFrameSnapshot&) const = default;
};

struct IdleNotificationSnapshot {
    uint64_t id = 0;
    bool idled = false;

    bool operator==(const IdleNotificationSnapshot&) const = default;
};

// ============================================================================
// Discrete Events
// ============================================================================

struct LayerConfigureEvent {
    SurfaceId surface_id = kNoSurface;
    uint32_t serial = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    bool operator==(const LayerConfigureEvent&) const = default;
};

struct LayerClosedEvent {
    SurfaceId surface_id = kNoSurface;

    bool operator==(const LayerClosedEvent&) const = default;
};

struct PopupConfigureEvent {
    SurfaceId surface_id = kNoSurface;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;

    bool operator==(const PopupConfigureEvent&) const = default;
};

struct PopupDoneEvent {
    SurfaceId surface_id = kNoSurface;

    bool operator==(const PopupDoneEvent&) const = default;
};

struct PopupRepositionedEvent {
    SurfaceId surface_id = kNoSurface;
    uint32_t token = 0;

    bool operator==(const PopupRepositionedEvent&) const = default;
};

struct ToplevelCreatedEvent {
    ForeignToplevelSnapshot toplevel;

    bool operator==(const ToplevelCreatedEvent&) const = default;
};

struct ToplevelClosedEvent {
    ToplevelId id = kNoToplevel;

    bool operator==(const ToplevelClosedEvent&) const = default;
};

struct ToplevelTitleEvent {
    ToplevelId id = kNoToplevel;
    std::string title;

    bool operator==(const ToplevelTitleEvent&) const = default;
};

struct ToplevelAppIdEvent {
    ToplevelId id = kNoToplevel;
    std::string app_id;

    bool operator==(const ToplevelAppIdEvent&) const = default;
};

struct ToplevelStateEvent {
    ToplevelId id = kNoToplevel;
    uint32_t state = 0;

    bool operator==(const ToplevelStateEvent&) const = default;
};

struct ToplevelParentEvent {
    ToplevelId id = kNoToplevel;
    ToplevelId parent_id = kNoToplevel;

    bool operator==(const ToplevelParentEvent&) const = default;
};

struct ToplevelOutputEnterEvent {
    ToplevelId id = kNoToplevel;
    OutputId output_id = kNoOutput;

    bool operator==(const ToplevelOutputEnterEvent&) const = default;
};

struct ToplevelOutputLeaveEvent {
    ToplevelId id = kNoToplevel;
    OutputId output_id = kNoOutput;

    bool operator==(const ToplevelOutputLeaveEvent&) const = default;
};

struct ToplevelDoneEvent {
    ToplevelId id = kNoToplevel;
    ForeignToplevelSnapshot snapshot;

    bool operator==(const ToplevelDoneEvent&) const = default;
};

struct SessionLockedEvent {
    bool operator==(const SessionLockedEvent&) const = default;
};

struct SessionLockFinishedEvent {
    bool operator==(const SessionLockFinishedEvent&) const = default;
};

struct SessionLockSurfaceConfigureEvent {
    SurfaceId surface_id = kNoSurface;
    OutputId output_id = kNoOutput;
    uint32_t serial = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    bool operator==(const SessionLockSurfaceConfigureEvent&) const = default;
};

struct ScreenCopyBufferEvent {
    uint32_t format = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;

    bool operator==(const ScreenCopyBufferEvent&) const = default;
};

struct ScreenCopyReadyEvent {
    ScreenCopyFrameSnapshot frame;

    bool operator==(const ScreenCopyReadyEvent&) const = default;
};

struct ScreenCopyFailedEvent {
    std::string reason;

    bool operator==(const ScreenCopyFailedEvent&) const = default;
};

struct IdleNotificationIdledEvent {
    uint64_t id = 0;

    bool operator==(const IdleNotificationIdledEvent&) const = default;
};

struct IdleNotificationResumedEvent {
    uint64_t id = 0;

    bool operator==(const IdleNotificationResumedEvent&) const = default;
};

struct OutputAddedEvent {
    OutputSnapshot output;

    bool operator==(const OutputAddedEvent&) const = default;
};

struct OutputRemovedEvent {
    OutputId id = kNoOutput;

    bool operator==(const OutputRemovedEvent&) const = default;
};

struct OutputChangedEvent {
    OutputSnapshot output;

    bool operator==(const OutputChangedEvent&) const = default;
};

struct SeatAddedEvent {
    SeatSnapshot seat;

    bool operator==(const SeatAddedEvent&) const = default;
};

struct SeatRemovedEvent {
    SeatId id = kNoSeat;

    bool operator==(const SeatRemovedEvent&) const = default;
};

struct SeatChangedEvent {
    SeatSnapshot seat;

    bool operator==(const SeatChangedEvent&) const = default;
};

struct DisplayErrorEvent {
    int32_t code = 0;
    std::string message;

    bool operator==(const DisplayErrorEvent&) const = default;
};

using ShellEvent = std::variant<
    LayerConfigureEvent,
    LayerClosedEvent,
    PopupConfigureEvent,
    PopupDoneEvent,
    PopupRepositionedEvent,
    ToplevelCreatedEvent,
    ToplevelClosedEvent,
    ToplevelTitleEvent,
    ToplevelAppIdEvent,
    ToplevelStateEvent,
    ToplevelParentEvent,
    ToplevelOutputEnterEvent,
    ToplevelOutputLeaveEvent,
    ToplevelDoneEvent,
    SessionLockedEvent,
    SessionLockFinishedEvent,
    SessionLockSurfaceConfigureEvent,
    ScreenCopyBufferEvent,
    ScreenCopyReadyEvent,
    ScreenCopyFailedEvent,
    IdleNotificationIdledEvent,
    IdleNotificationResumedEvent,
    OutputAddedEvent,
    OutputRemovedEvent,
    OutputChangedEvent,
    SeatAddedEvent,
    SeatRemovedEvent,
    SeatChangedEvent,
    DisplayErrorEvent>;

}  // namespace browl
