#pragma once

// A wl_seat. Without Display::enable_input() it only reports its name and
// capabilities. With input enabled it also binds the seat's pointer,
// keyboard and touch, and offers what an application needs from a seat:
//
//   input events      Pointer*/Key*/Touch* events in the Display's queue
//   keymap            the active layout (Keymap), for key → text questions
//   cursor            set_cursor (wp_cursor_shape_v1, else an XCursor theme)
//   pointer lock      lock_pointer + relative motion (pointer constraints and
//                     relative pointer); warp_pointer
//   selections        the clipboard and the primary selection: set, read, own
//   drag and drop     drops onto the client's surfaces (DragDropEvent), and
//                     drags out of them (start_drag, DragSourceEvent)
//   text input        zwp_text_input_v3, an input method's composition
//
// Requests that need a serial (setting a selection, a cursor, a move) use the
// newest input serial the seat has seen from the compositor.
//
// Thread-safety: like the rest of browl, call into a Seat from the thread that
// dispatches the Display; snapshot(), keymap() and the selection reads may be
// called from any thread.

#include "browl/events.h"
#include "browl/keymap.h"
#include "browl/types.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct wl_seat;
struct wl_surface;

namespace browl {

class Display;

/// Contents offered for a selection: (MIME type, bytes) pairs, in order of
/// preference. Text is usually offered as "text/plain;charset=utf-8",
/// "text/plain", "UTF8_STRING", "TEXT" and "STRING" with the same bytes.
using SelectionContents = std::vector<std::pair<std::string, std::vector<uint8_t>>>;

/// The MIME types the usual UTF-8 text offer carries.
SelectionContents text_selection(const std::string& utf8);

class Seat {
public:
    Seat(SeatId id, wl_seat* wl_seat, Display* display);
    ~Seat();

    Seat(const Seat&) = delete;
    Seat& operator=(const Seat&) = delete;

    SeatId id() const { return id_; }
    wl_seat* wl_seat_ptr() const { return wl_seat_; }

    SeatSnapshot snapshot() const;

    const std::string& name() const { return name_; }
    bool has_pointer() const { return has_pointer_; }
    bool has_keyboard() const { return has_keyboard_; }
    bool has_touch() const { return has_touch_; }

    // --- Input (after Display::enable_input) ---

    /// The keyboard's layout; null before the compositor sent a keymap.
    std::shared_ptr<const Keymap> keymap() const;
    /// The modifier bits held now.
    uint32_t modifiers() const;
    /// Key repeat: repeats per second (0 = off) and the delay before the
    /// first, from wl_keyboard.repeat_info (25/600 until it arrives).
    int32_t repeat_rate() const;
    int32_t repeat_delay_ms() const;
    /// The newest serial of an input event (press, key, enter), for requests
    /// that must prove user interaction.
    uint32_t last_input_serial() const;
    /// The serial of the pointer's enter on its current surface (0 if none).
    uint32_t pointer_enter_serial() const;
    /// The surface the pointer / keyboard is on (kNoSurface when none or not
    /// one of browl's).
    SurfaceId pointer_focus() const;
    SurfaceId keyboard_focus() const;

    /// Show `shape` while the pointer is over this client's surfaces (kept
    /// across enters). Hidden hides it. Uses wp_cursor_shape_v1 when the
    /// compositor has it, else the XCursor theme ($XCURSOR_THEME /
    /// $XCURSOR_SIZE) at the scale of the focused surface.
    void set_cursor(CursorShape shape);
    CursorShape cursor() const;

    /// Lock the pointer in place over `surface` (pointer lock): the cursor
    /// stops, and PointerRelativeMotionEvents report the motion. False when
    /// the compositor lacks pointer constraints or relative pointer.
    bool lock_pointer(wl_surface* surface);
    void unlock_pointer();
    bool pointer_locked() const;
    /// Move the pointer to (x, y) on `surface`, as far as Wayland allows: a
    /// locked pointer gets a position hint; an unlocked one is locked for a
    /// moment with the hint, which most compositors honour on unlock.
    void warp_pointer(wl_surface* surface, double x, double y);

    /// Offer `contents` as the selection. False when the compositor lacks
    /// the selection's protocol or there was no input serial yet.
    bool set_selection(Selection which, SelectionContents contents);
    void clear_selection(Selection which);
    /// The MIME types the current selection offers (empty: none).
    std::vector<std::string> selection_mime_types(Selection which) const;
    bool owns_selection(Selection which) const;
    bool has_selection_protocol(Selection which) const;
    /// Read the selection as `mime_type`. A selection this client owns is
    /// answered from memory; another client's is read through a pipe, waiting
    /// at most `timeout` for it. nullopt when there is no such offer or the
    /// read failed / timed out.
    std::optional<std::vector<uint8_t>> read_selection(
        Selection which, const std::string& mime_type,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));

    /// The MIME types a drag onto this client is accepted with, in order of
    /// preference (default: text/uri-list, then UTF-8 text).
    void set_drag_mime_types(std::vector<std::string> mime_types);
    /// After a DragDropEvent: read the dropped data, then finish the drop.
    std::optional<std::vector<uint8_t>> read_drop(
        const std::string& mime_type,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));
    void finish_drop();

    /// A drag's look: premultiplied ARGB8888 pixels (B, G, R, A bytes in
    /// memory), `width` * 4 bytes a row, held at the pointer by the
    /// hotspot (the pixel under the pointer).
    struct DragIcon {
        int32_t width = 0, height = 0;
        std::vector<uint8_t> pixels;
        int32_t hotspot_x = 0, hotspot_y = 0;
    };
    /// Start a drag out of `origin` (one of this client's surfaces) carrying
    /// `contents`, while the pointer button pressed on it is still held
    /// (the press's serial proves it). `actions` are the dnd_action bits it
    /// allows. The compositor draws `icon` at the pointer, when given. Its
    /// progress comes back as DragSourceEvents; a drop onto this client's
    /// own surfaces arrives as the usual DragEnter/Motion/Drop events, and
    /// read_drop answers it from `contents`. False when the compositor
    /// lacks a data device or no button press was seen.
    bool start_drag(wl_surface* origin, SelectionContents contents, const DragIcon* icon = nullptr,
                    uint32_t actions = dnd_action::Copy);
    /// A drag this client started is still under way.
    bool dragging() const;
    /// Withdraw the drag this client started (the compositor ends it).
    void cancel_drag();

    /// zwp_text_input_v3 for this seat. False when the compositor lacks it.
    bool has_text_input() const;
    /// Enable / disable text input on the focused surface (after a
    /// TextInputFocusEvent entered), with where the caret is (surface
    /// coordinates) and what kind of text it is. Each call commits.
    void enable_text_input(uint32_t hints = content_hint::None,
                           ContentPurpose purpose = ContentPurpose::Normal);
    void disable_text_input();
    bool text_input_enabled() const;
    void set_text_input_cursor_rect(const Rect& rect);
    /// The text around the caret, for an input method that wants context.
    void set_surrounding_text(const std::string& text, int32_t cursor, int32_t anchor);

    // Internal callbacks from wl_seat_listener and the input listeners.
    void handle_capabilities(uint32_t capabilities);
    void handle_name(const char* name);
    void enable_input();
    void detach();

    struct Impl;
    Impl* impl() const { return impl_.get(); }

private:
    SeatId id_ = kNoSeat;
    wl_seat* wl_seat_ = nullptr;
    Display* display_ = nullptr;

    std::string name_;
    bool has_pointer_ = false;
    bool has_keyboard_ = false;
    bool has_touch_ = false;
    std::unique_ptr<Impl> impl_;
};

}  // namespace browl
