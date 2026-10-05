#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace browl {

using ToplevelId = uint64_t;
using OutputId = uint32_t;
using SeatId = uint32_t;
using SurfaceId = uint64_t;

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

struct OutputMode {
    int32_t width = 0;
    int32_t height = 0;
    int32_t refresh_mhz = 0;  // in millihertz (e.g. 60000 = 60Hz)
    bool current = false;
    bool preferred = false;

    bool operator==(const OutputMode&) const = default;
};

}  // namespace browl
