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
    std::string description;
    /// The output's place and size in the compositor's logical space
    /// (zxdg_output_v1; without it, geometry's position and the current mode
    /// divided by scale).
    Rect logical;

    bool operator==(const OutputSnapshot&) const = default;
};

/// An application window (Window) as the compositor last configured it.
struct WindowSnapshot {
    SurfaceId id = kNoSurface;
    /// The size the compositor asked for, in logical px; 0 means "choose".
    Size configured_size;
    /// The largest size that would fit (xdg_toplevel.configure_bounds); 0 unknown.
    Size bounds;
    uint32_t states = 0;  // window_state bits
    uint32_t wm_capabilities = wm_capability::All;
    DecorationMode decoration = DecorationMode::Unknown;
    /// The scale the compositor prefers for this surface's buffers, in 120ths
    /// (wp_fractional_scale_v1; 180 = 1.5). Without fractional scale, the
    /// integer preferred_buffer_scale * 120, else the largest scale of the
    /// outputs the surface is on, else 120.
    uint32_t scale120 = 120;
    std::vector<OutputId> outputs;  // the outputs the surface is on (wl_surface.enter)
    bool configured = false;        // a first configure has been acked
    bool close_requested = false;

    bool operator==(const WindowSnapshot&) const = default;
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

// --- Application windows (window.h) ----------------------------------------

/// A configure sequence finished (xdg_surface.configure) and was acked; the
/// next commit must show it. `snapshot` is the window's state after it.
struct WindowConfigureEvent {
    SurfaceId surface_id = kNoSurface;
    uint32_t serial = 0;
    WindowSnapshot snapshot;

    bool operator==(const WindowConfigureEvent&) const = default;
};

/// The compositor asked for the window to close (xdg_toplevel.close).
struct WindowCloseEvent {
    SurfaceId surface_id = kNoSurface;

    bool operator==(const WindowCloseEvent&) const = default;
};

/// The window's preferred scale changed (fractional scale, preferred buffer
/// scale, or the outputs it is on); WindowSnapshot::scale120 has the new one.
struct WindowScaleEvent {
    SurfaceId surface_id = kNoSurface;
    uint32_t scale120 = 120;

    bool operator==(const WindowScaleEvent&) const = default;
};

/// The surface entered or left an output.
struct WindowOutputsEvent {
    SurfaceId surface_id = kNoSurface;
    std::vector<OutputId> outputs;

    bool operator==(const WindowOutputsEvent&) const = default;
};

/// An xdg_activation_v1 token asked for with Display::request_activation_token.
/// `token` is empty when the request object went away without one.
struct ActivationTokenEvent {
    RequestId request = 0;
    std::string token;

    bool operator==(const ActivationTokenEvent&) const = default;
};

/// The content update a Display::request_presentation_feedback named was
/// shown (presented = true; the time is the presentation clock's, normally
/// CLOCK_MONOTONIC) or never will be (presented = false: discarded).
struct PresentationFeedbackEvent {
    RequestId request = 0;
    SurfaceId surface_id = kNoSurface;
    bool presented = false;
    uint64_t time_ns = 0;     // when the update turned to light
    uint32_t refresh_ns = 0;  // the output's refresh period; 0 unknown / variable
    uint64_t sequence = 0;    // the output's vblank counter; 0 unknown
    uint32_t flags = 0;       // presentation_flags
    OutputId output = kNoOutput;

    bool operator==(const PresentationFeedbackEvent&) const = default;
};

/// The wl_surface.frame callback a Display::request_frame_callback named
/// fired: the compositor says now is a good time to draw the next frame.
struct FrameDoneEvent {
    RequestId request = 0;
    SurfaceId surface_id = kNoSurface;
    uint32_t time_ms = 0;  // the callback's timestamp (ms, unspecified base)

    bool operator==(const FrameDoneEvent&) const = default;
};

// --- Input (seat.h; delivered once Display::enable_input() is called) ------
// Surface ids name surfaces browl created (Window, LayerSurface, Popup, ...);
// kNoSurface for any other. Positions are surface-local logical px.

struct PointerEnterEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;
    uint32_t serial = 0;
    double x = 0, y = 0;

    bool operator==(const PointerEnterEvent&) const = default;
};

struct PointerLeaveEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;
    uint32_t serial = 0;

    bool operator==(const PointerLeaveEvent&) const = default;
};

struct PointerMotionEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;
    uint32_t time_ms = 0;
    double x = 0, y = 0;

    bool operator==(const PointerMotionEvent&) const = default;
};

/// `button` is the Linux input code (BTN_LEFT = 0x110, BTN_RIGHT, BTN_MIDDLE,
/// BTN_SIDE, BTN_EXTRA, ...).
struct PointerButtonEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;
    uint32_t serial = 0;
    uint32_t time_ms = 0;
    uint32_t button = 0;
    bool pressed = false;

    bool operator==(const PointerButtonEvent&) const = default;
};

/// One wl_pointer.frame's worth of scrolling. `dx`/`dy` are the continuous
/// amounts in surface px (+y down, as wl_pointer has it); `v120x`/`v120y`
/// the wheel detents in 120ths (axis_value120, or axis_discrete * 120) — 0
/// for a touchpad. `stop_x`/`stop_y`: a kinetic scroll ended on that axis.
struct PointerAxisEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;
    uint32_t time_ms = 0;
    double dx = 0, dy = 0;
    int32_t v120x = 0, v120y = 0;
    AxisSource source = AxisSource::Unknown;
    bool stop_x = false, stop_y = false;
    bool inverted_x = false, inverted_y = false;  // axis_relative_direction

    bool operator==(const PointerAxisEvent&) const = default;
};

/// Unaccelerated-and-accelerated pointer motion while the pointer is locked
/// (Seat::lock_pointer); zwp_relative_pointer_v1.
struct PointerRelativeMotionEvent {
    SeatId seat = kNoSeat;
    uint64_t time_us = 0;
    double dx = 0, dy = 0;
    double dx_unaccel = 0, dy_unaccel = 0;

    bool operator==(const PointerRelativeMotionEvent&) const = default;
};

/// Pointer lock state changed (zwp_locked_pointer_v1 locked / unlocked).
struct PointerLockEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;
    bool locked = false;

    bool operator==(const PointerLockEvent&) const = default;
};

struct KeyboardEnterEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;
    uint32_t serial = 0;
    std::vector<uint32_t> keys;  // Linux key codes held at enter

    bool operator==(const KeyboardEnterEvent&) const = default;
};

struct KeyboardLeaveEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;
    uint32_t serial = 0;

    bool operator==(const KeyboardLeaveEvent&) const = default;
};

/// One key press or release. `key` is the Linux key code (KEY_A = 30, as
/// evdev numbers them; xkb keycode - 8). `keysym` is what the key produces
/// in the current state (shift level and layout applied); `utf8` is the text
/// it types, after compose (dead keys): empty for a non-text key, for a
/// release, and mid-sequence. `modifiers` is the state the press was
/// interpreted in (modifier bits). Compositors do not repeat keys; see
/// RepeatInfoEvent and Keymap::repeats.
struct KeyEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;
    uint32_t serial = 0;
    uint32_t time_ms = 0;
    uint32_t key = 0;
    bool pressed = false;
    uint32_t keysym = 0;
    std::string utf8;
    uint32_t modifiers = 0;

    bool operator==(const KeyEvent&) const = default;
};

struct ModifiersEvent {
    SeatId seat = kNoSeat;
    uint32_t serial = 0;
    uint32_t depressed = 0, latched = 0, locked = 0, group = 0;  // raw xkb masks
    uint32_t modifiers = 0;                                      // modifier bits, effective

    bool operator==(const ModifiersEvent&) const = default;
};

/// Key repeat settings: `rate` repeats per second (0 = no repeat) after
/// `delay_ms`.
struct RepeatInfoEvent {
    SeatId seat = kNoSeat;
    int32_t rate = 0;
    int32_t delay_ms = 0;

    bool operator==(const RepeatInfoEvent&) const = default;
};

/// A new keymap (layout) is in effect; Seat::keymap() returns it.
struct KeymapEvent {
    SeatId seat = kNoSeat;

    bool operator==(const KeymapEvent&) const = default;
};

struct TouchDownEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;
    uint32_t serial = 0;
    uint32_t time_ms = 0;
    int32_t id = 0;
    double x = 0, y = 0;

    bool operator==(const TouchDownEvent&) const = default;
};

struct TouchMotionEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;  // the surface the touch went down on
    uint32_t time_ms = 0;
    int32_t id = 0;
    double x = 0, y = 0;

    bool operator==(const TouchMotionEvent&) const = default;
};

struct TouchUpEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;  // the surface the touch went down on
    uint32_t serial = 0;
    uint32_t time_ms = 0;
    int32_t id = 0;
    double x = 0, y = 0;  // the touch's last position

    bool operator==(const TouchUpEvent&) const = default;
};

/// The compositor took every touch point of the seat over (a gesture).
struct TouchCancelEvent {
    SeatId seat = kNoSeat;

    bool operator==(const TouchCancelEvent&) const = default;
};

/// A selection changed hands: another client set it, or it was cleared
/// (mime_types empty), or this client set it (owned).
struct SelectionChangedEvent {
    SeatId seat = kNoSeat;
    Selection selection = Selection::Clipboard;
    std::vector<std::string> mime_types;
    bool owned = false;

    bool operator==(const SelectionChangedEvent&) const = default;
};

/// A drag entered a surface (wl_data_device.enter). browl accepts the first
/// of Seat::set_drag_mime_types the offer has, with the copy action.
struct DragEnterEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;
    double x = 0, y = 0;
    std::vector<std::string> mime_types;

    bool operator==(const DragEnterEvent&) const = default;
};

struct DragMotionEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;
    uint32_t time_ms = 0;
    double x = 0, y = 0;

    bool operator==(const DragMotionEvent&) const = default;
};

struct DragLeaveEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;

    bool operator==(const DragLeaveEvent&) const = default;
};

/// The drag was dropped on the surface. Read it with Seat::read_drop, then
/// Seat::finish_drop (which a new drag also does for you).
struct DragDropEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;
    double x = 0, y = 0;
    std::vector<std::string> mime_types;

    bool operator==(const DragDropEvent&) const = default;
};

/// Text input focus (zwp_text_input_v3.enter / leave): the seat's input
/// method may now serve this surface. Enable text input after enter.
struct TextInputFocusEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;
    bool entered = false;

    bool operator==(const TextInputFocusEvent&) const = default;
};

/// One zwp_text_input_v3.done: apply in order — delete `delete_before` /
/// `delete_after` bytes around the cursor (and any old preedit), insert
/// `commit`, then show `preedit` with its cursor span (byte offsets into
/// preedit; -1/-1 hides the cursor). Empty preedit = no composition.
struct TextInputEvent {
    SeatId seat = kNoSeat;
    SurfaceId surface_id = kNoSurface;
    std::string preedit;
    int32_t preedit_cursor_begin = 0;
    int32_t preedit_cursor_end = 0;
    std::string commit;
    uint32_t delete_before = 0;
    uint32_t delete_after = 0;
    uint32_t serial = 0;

    bool operator==(const TextInputEvent&) const = default;
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
    DisplayErrorEvent,
    WindowConfigureEvent,
    WindowCloseEvent,
    WindowScaleEvent,
    WindowOutputsEvent,
    ActivationTokenEvent,
    PresentationFeedbackEvent,
    FrameDoneEvent,
    PointerEnterEvent,
    PointerLeaveEvent,
    PointerMotionEvent,
    PointerButtonEvent,
    PointerAxisEvent,
    PointerRelativeMotionEvent,
    PointerLockEvent,
    KeyboardEnterEvent,
    KeyboardLeaveEvent,
    KeyEvent,
    ModifiersEvent,
    RepeatInfoEvent,
    KeymapEvent,
    TouchDownEvent,
    TouchMotionEvent,
    TouchUpEvent,
    TouchCancelEvent,
    SelectionChangedEvent,
    DragEnterEvent,
    DragMotionEvent,
    DragLeaveEvent,
    DragDropEvent,
    TextInputFocusEvent,
    TextInputEvent>;

}  // namespace browl
