#pragma once

// Seat::Impl: a seat's input objects and their state, shared by
//   seat.cpp             capabilities, enabling input, the plain getters
//   seat_pointer.cpp     wl_pointer, cursor (shape / XCursor theme), pointer lock
//   seat_keyboard.cpp    wl_keyboard, keymap, xkb state, compose
//   seat_touch.cpp       wl_touch
//   seat_data_device.cpp clipboard (wl_data_device), primary selection, drag and drop
//   seat_text_input.cpp  zwp_text_input_v3
//
// Every listener runs on the thread that dispatches the Display. What other
// threads may read (keymap, modifiers, serials, focus, selection state) is
// guarded by `mutex`.

#include "browl/seat.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct wl_pointer;
struct wl_keyboard;
struct wl_touch;
struct wl_surface;
struct wl_cursor_theme;
struct wl_data_device;
struct wl_data_offer;
struct wl_data_source;
struct wp_cursor_shape_device_v1;
struct zwp_locked_pointer_v1;
struct zwp_relative_pointer_v1;
struct zwp_primary_selection_device_v1;
struct zwp_text_input_v3;
struct xkb_context;
struct xkb_state;
struct xkb_compose_table;
struct xkb_compose_state;

namespace browl {

class Display;

class ShmPool;
class ShmBuffer;

/// One selection source this client offered (clipboard or primary), or the
/// source of a drag it started (drag: a wl_data_source in no selection slot).
struct SelectionSource {
    Seat::Impl* impl = nullptr;
    Selection which = Selection::Clipboard;
    void* proxy = nullptr;  // wl_data_source* or zwp_primary_selection_source_v1*
    std::shared_ptr<const SelectionContents> contents;
    bool drag = false;
};

/// The state of one selection (clipboard or primary) on this seat.
struct SelectionSlot {
    // Offers announced by data_offer, with the MIME types they listed.
    std::unordered_map<void*, std::vector<std::string>> offers;
    void* current_offer = nullptr;  // the selection's offer (null: none)
    SelectionSource* source = nullptr;  // ours, while we own the selection
    // The offer the compositor sent back for our own selection.
    void* echo_offer = nullptr;
};

struct Seat::Impl {
    Impl(Seat* seat, Display* display) : seat(seat), display(display) {}

    Seat* seat = nullptr;
    Display* display = nullptr;
    bool input_enabled = false;
    mutable std::mutex mutex;

    // Shared bookkeeping
    uint32_t last_serial = 0;
    void note_serial(uint32_t serial);
    SeatId seat_id() const;
    SurfaceId surface_id(wl_surface* surface) const;

    /// Binds / releases the pointer, keyboard and touch to match the
    /// capabilities, and the per-seat devices (data device, primary
    /// selection, text input) once.
    void update_devices();
    void release_all();

    // --- Pointer (seat_pointer.cpp) ---
    wl_pointer* pointer = nullptr;
    wl_surface* pointer_surface = nullptr;
    SurfaceId pointer_focus = kNoSurface;
    uint32_t pointer_enter_serial = 0;
    uint32_t last_press_serial = 0;  // the newest button press: a drag's implicit grab
    double pointer_x = 0, pointer_y = 0;
    struct AxisFrame {
        bool any = false;
        uint32_t time_ms = 0;
        double dx = 0, dy = 0;
        int32_t v120x = 0, v120y = 0;
        AxisSource source = AxisSource::Unknown;
        bool stop_x = false, stop_y = false;
        bool inverted_x = false, inverted_y = false;
    } axis;
    CursorShape cursor = CursorShape::Default;
    wp_cursor_shape_device_v1* cursor_shape_device = nullptr;
    wl_cursor_theme* cursor_theme = nullptr;
    int32_t cursor_theme_scale = 0;
    wl_surface* cursor_surface = nullptr;
    zwp_locked_pointer_v1* locked_pointer = nullptr;
    zwp_relative_pointer_v1* relative_pointer = nullptr;
    wl_surface* lock_surface = nullptr;
    bool pointer_is_locked = false;

    void bind_pointer();
    void release_pointer();
    void apply_cursor();
    void flush_axis();
    void destroy_lock();

    // --- Keyboard (seat_keyboard.cpp) ---
    wl_keyboard* keyboard = nullptr;
    std::shared_ptr<const Keymap> keymap;
    xkb_state* xkb = nullptr;
    xkb_compose_table* compose_table = nullptr;
    xkb_compose_state* compose = nullptr;
    bool compose_tried = false;
    wl_surface* keyboard_surface = nullptr;
    SurfaceId keyboard_focus = kNoSurface;
    uint32_t modifiers = 0;
    int32_t repeat_rate = 25;
    int32_t repeat_delay_ms = 600;

    void bind_keyboard();
    void release_keyboard();
    void release_xkb();

    // --- Touch (seat_touch.cpp) ---
    wl_touch* touch = nullptr;
    struct TouchPoint {
        SurfaceId surface_id = kNoSurface;
        double x = 0, y = 0;
    };
    std::unordered_map<int32_t, TouchPoint> touches;

    void bind_touch();
    void release_touch();

    // --- Selections and drag and drop (seat_data_device.cpp) ---
    wl_data_device* data_device = nullptr;
    zwp_primary_selection_device_v1* primary_device = nullptr;
    SelectionSlot clipboard;
    SelectionSlot primary;
    std::vector<SelectionSource*> sources;  // live until cancelled (dispatch thread)
    wl_data_offer* drag_offer = nullptr;       // the offer of the drag over us
    std::vector<std::string> drag_offer_mimes;
    std::string drag_accepted_mime;
    uint32_t drag_serial = 0;
    SurfaceId drag_surface = kNoSurface;
    double drag_x = 0, drag_y = 0;  // the drag's last position, surface-local (wl_data_device.drop has none)
    bool drag_dropped = false;
    std::vector<std::string> drag_mime_types{"text/uri-list", "text/plain;charset=utf-8", "text/plain"};
    // The drag this client started (start_drag), until it finishes or is cancelled.
    SelectionSource* drag_source = nullptr;
    uint32_t drag_source_action = 0;  // the action the compositor last chose for it
    wl_surface* drag_icon = nullptr;
    std::shared_ptr<ShmPool> drag_icon_pool;
    std::shared_ptr<ShmBuffer> drag_icon_buffer;

    SelectionSlot& slot(Selection which) { return which == Selection::Primary ? primary : clipboard; }
    const SelectionSlot& slot(Selection which) const {
        return which == Selection::Primary ? primary : clipboard;
    }
    void bind_data_devices();
    void release_data_devices();
    void destroy_offer(Selection which, void* offer);
    void destroy_source(SelectionSource* source);
    void finish_drag_offer();

    // Listener entry points (data device and primary selection).
    void handle_data_offer(Selection which, void* offer);
    void handle_offer_mime(Selection which, void* offer, const char* mime);
    void handle_selection(Selection which, void* offer);
    void handle_source_send(SelectionSource* source, const char* mime, int fd);
    void handle_source_cancelled(SelectionSource* source);
    void handle_drag_enter(uint32_t serial, wl_surface* surface, double x, double y, wl_data_offer* offer);
    void handle_drag_leave();
    void handle_drag_motion(uint32_t time, double x, double y);
    void handle_drop();
    // The drag source's side (drag sources only).
    void handle_drag_source(SelectionSource* source, DragSourceEvent::Kind kind, const char* mime,
                            uint32_t action);
    /// The drag this client started is over: its source and icon go.
    void end_drag_source(SelectionSource* source);

    // --- Text input (seat_text_input.cpp) ---
    zwp_text_input_v3* text_input = nullptr;
    wl_surface* text_input_surface = nullptr;
    SurfaceId text_input_focus = kNoSurface;
    bool text_input_enabled = false;
    uint32_t text_input_commits = 0;
    struct TextInputPending {
        std::string preedit;
        int32_t cursor_begin = -1, cursor_end = -1;
        std::string commit;
        uint32_t delete_before = 0, delete_after = 0;
    } text_pending;

    void bind_text_input();
    void release_text_input();
};

}  // namespace browl
