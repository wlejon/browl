#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace browl {

using ToplevelId = uint64_t;
using OutputId = uint32_t;
using SeatId = uint32_t;
using SurfaceId = uint64_t;
/// Names one request whose answer arrives later as an event (an activation
/// token, a presentation feedback). Never 0.
using RequestId = uint64_t;

inline constexpr ToplevelId kNoToplevel = 0;
inline constexpr OutputId kNoOutput = 0;
inline constexpr SeatId kNoSeat = 0;
inline constexpr SurfaceId kNoSurface = 0;

struct Point {
    int32_t x = 0;
    int32_t y = 0;

    bool operator==(const Point&) const = default;
};

struct Size {
    int32_t width = 0;
    int32_t height = 0;

    bool operator==(const Size&) const = default;
};

struct Rect {
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;

    bool operator==(const Rect&) const = default;
};

struct Margins {
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
    int32_t left = 0;

    bool operator==(const Margins&) const = default;
};

enum class Layer : uint32_t {
    Background = 0,
    Bottom = 1,
    Top = 2,
    Overlay = 3,
};

enum class Anchor : uint32_t {
    None = 0,
    Top = 1u << 0,
    Bottom = 1u << 1,
    Left = 1u << 2,
    Right = 1u << 3,
};

inline constexpr Anchor operator|(Anchor a, Anchor b) {
    return static_cast<Anchor>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr Anchor operator&(Anchor a, Anchor b) {
    return static_cast<Anchor>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline constexpr Anchor operator^(Anchor a, Anchor b) {
    return static_cast<Anchor>(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b));
}

inline constexpr Anchor operator~(Anchor a) {
    return static_cast<Anchor>(~static_cast<uint32_t>(a));
}

inline constexpr Anchor& operator|=(Anchor& a, Anchor b) {
    return a = a | b;
}

inline constexpr Anchor& operator&=(Anchor& a, Anchor b) {
    return a = a & b;
}

enum class KeyboardInteractivity : uint32_t {
    None = 0,
    Exclusive = 1,
    OnDemand = 2,
};

namespace toplevel_state {
inline constexpr uint32_t Maximized = 1u << 0;
inline constexpr uint32_t Minimized = 1u << 1;
inline constexpr uint32_t Activated = 1u << 2;
inline constexpr uint32_t Fullscreen = 1u << 3;
}  // namespace toplevel_state

enum class OutputTransform : uint32_t {
    Normal = 0,
    Rot90 = 1,
    Rot180 = 2,
    Rot270 = 3,
    Flipped = 4,
    FlippedRot90 = 5,
    FlippedRot180 = 6,
    FlippedRot270 = 7,
};

enum class PositionerAnchor : uint32_t {
    None = 0,
    Top = 1,
    Bottom = 2,
    Left = 3,
    Right = 4,
    TopLeft = 5,
    BottomLeft = 6,
    TopRight = 7,
    BottomRight = 8,
};

enum class Gravity : uint32_t {
    None = 0,
    Top = 1,
    Bottom = 2,
    Left = 3,
    Right = 4,
    TopLeft = 5,
    BottomLeft = 6,
    TopRight = 7,
    BottomRight = 8,
};

namespace constraint_adjustment {
inline constexpr uint32_t None = 0;
inline constexpr uint32_t SlideX = 1u << 0;
inline constexpr uint32_t SlideY = 1u << 1;
inline constexpr uint32_t FlipX = 1u << 2;
inline constexpr uint32_t FlipY = 1u << 3;
inline constexpr uint32_t ResizeX = 1u << 4;
inline constexpr uint32_t ResizeY = 1u << 5;
}  // namespace constraint_adjustment

namespace screencopy_flags {
inline constexpr uint32_t None = 0;
inline constexpr uint32_t YInverted = 1u << 0;
}  // namespace screencopy_flags

// ---------------------------------------------------------------------------
// Application windows and input (window.h, seat.h, keymap.h)
// ---------------------------------------------------------------------------

/// xdg_toplevel states, as a bitmask (WindowSnapshot::states).
namespace window_state {
inline constexpr uint32_t Maximized = 1u << 0;
inline constexpr uint32_t Fullscreen = 1u << 1;
inline constexpr uint32_t Resizing = 1u << 2;
inline constexpr uint32_t Activated = 1u << 3;
inline constexpr uint32_t TiledLeft = 1u << 4;
inline constexpr uint32_t TiledRight = 1u << 5;
inline constexpr uint32_t TiledTop = 1u << 6;
inline constexpr uint32_t TiledBottom = 1u << 7;
inline constexpr uint32_t Suspended = 1u << 8;
}  // namespace window_state

/// xdg_toplevel.wm_capabilities, as a bitmask. A compositor older than
/// xdg_wm_base v5 sends none; WindowSnapshot then reports all of them.
namespace wm_capability {
inline constexpr uint32_t WindowMenu = 1u << 0;
inline constexpr uint32_t Maximize = 1u << 1;
inline constexpr uint32_t Fullscreen = 1u << 2;
inline constexpr uint32_t Minimize = 1u << 3;
inline constexpr uint32_t All = WindowMenu | Maximize | Fullscreen | Minimize;
}  // namespace wm_capability

/// Who draws a window's title bar and borders (zxdg_decoration_v1).
/// Unknown: the compositor offers no decoration protocol, so by the protocol's
/// default the client decorates.
enum class DecorationMode : uint32_t {
    Unknown = 0,
    ClientSide = 1,
    ServerSide = 2,
};

/// Resize edges (xdg_toplevel.resize_edge).
enum class ResizeEdge : uint32_t {
    None = 0,
    Top = 1,
    Bottom = 2,
    Left = 4,
    TopLeft = 5,
    BottomLeft = 6,
    Right = 8,
    TopRight = 9,
    BottomRight = 10,
};

/// Pointer shapes, named as in wp_cursor_shape_device_v1 (CSS cursor names).
/// Hidden hides the pointer over the surface.
enum class CursorShape : uint32_t {
    Default = 1,
    ContextMenu,
    Help,
    Pointer,
    Progress,
    Wait,
    Cell,
    Crosshair,
    Text,
    VerticalText,
    Alias,
    Copy,
    Move,
    NoDrop,
    NotAllowed,
    Grab,
    Grabbing,
    EResize,
    NResize,
    NeResize,
    NwResize,
    SResize,
    SeResize,
    SwResize,
    WResize,
    EwResize,
    NsResize,
    NeswResize,
    NwseResize,
    ColResize,
    RowResize,
    AllScroll,
    ZoomIn,
    ZoomOut,
    Hidden = 1000,
};

/// The two selections a seat carries: the clipboard (wl_data_device) and the
/// primary selection (zwp_primary_selection_device_v1, middle-click paste).
enum class Selection : uint32_t {
    Clipboard = 0,
    Primary = 1,
};

/// Drag-and-drop actions (wl_data_device_manager.dnd_action), as bits.
namespace dnd_action {
inline constexpr uint32_t None = 0;
inline constexpr uint32_t Copy = 1u << 0;
inline constexpr uint32_t Move = 1u << 1;
inline constexpr uint32_t Ask = 1u << 2;
}  // namespace dnd_action

/// Modifier state, decoded from the keymap (Keymap / ModifiersEvent).
namespace modifier {
inline constexpr uint32_t Shift = 1u << 0;
inline constexpr uint32_t Ctrl = 1u << 1;
inline constexpr uint32_t Alt = 1u << 2;
inline constexpr uint32_t Logo = 1u << 3;
inline constexpr uint32_t CapsLock = 1u << 4;
inline constexpr uint32_t NumLock = 1u << 5;
inline constexpr uint32_t AltGr = 1u << 6;   // ISO Level3 shift (Mod5 on most layouts)
inline constexpr uint32_t Level5 = 1u << 7;  // ISO Level5 shift
}  // namespace modifier

/// wl_pointer.axis_source.
enum class AxisSource : uint32_t {
    Unknown = 0xffffffffu,
    Wheel = 0,
    Finger = 1,
    Continuous = 2,
    WheelTilt = 3,
};

/// wp_presentation_feedback.kind, as a bitmask (PresentationFeedbackEvent::flags).
namespace presentation_flags {
inline constexpr uint32_t Vsync = 1u << 0;
inline constexpr uint32_t HwClock = 1u << 1;
inline constexpr uint32_t HwCompletion = 1u << 2;
inline constexpr uint32_t ZeroCopy = 1u << 3;
}  // namespace presentation_flags

/// zwp_text_input_v3 content hints (bitmask) and purpose, for an input method.
namespace content_hint {
inline constexpr uint32_t None = 0;
inline constexpr uint32_t Completion = 1u << 0;
inline constexpr uint32_t Spellcheck = 1u << 1;
inline constexpr uint32_t AutoCapitalization = 1u << 2;
inline constexpr uint32_t Lowercase = 1u << 3;
inline constexpr uint32_t Uppercase = 1u << 4;
inline constexpr uint32_t Titlecase = 1u << 5;
inline constexpr uint32_t HiddenText = 1u << 6;
inline constexpr uint32_t SensitiveData = 1u << 7;
inline constexpr uint32_t Latin = 1u << 8;
inline constexpr uint32_t Multiline = 1u << 9;
}  // namespace content_hint

enum class ContentPurpose : uint32_t {
    Normal = 0,
    Alpha,
    Digits,
    Number,
    Phone,
    Url,
    Email,
    Name,
    Password,
    Pin,
    Date,
    Time,
    Datetime,
    Terminal,
};

struct OutputMode {
    int32_t width = 0;
    int32_t height = 0;
    int32_t refresh_mhz = 0;  // in millihertz (e.g. 60000 = 60Hz)
    bool current = false;
    bool preferred = false;

    bool operator==(const OutputMode&) const = default;
};

}  // namespace browl
