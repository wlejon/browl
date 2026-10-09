// A seat's pointer: wl_pointer events (axis accumulated per frame), the
// cursor (wp_cursor_shape_v1, else an XCursor theme through
// libwayland-cursor), and pointer lock with relative motion.
#include "browl/display.h"

#include "app_globals.h"
#include "cursor-shape-v1-client-protocol.h"
#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "seat_impl.h"

#include <wayland-client.h>
#include <wayland-cursor.h>

#include <cstdlib>
#include <array>
#include <string>

namespace browl {

namespace {

Seat::Impl* impl_of(void* data) {
    return static_cast<Seat::Impl*>(data);
}

// --- wl_pointer ----------------------------------------------------------------

static void pointer_handle_enter(void* data, struct wl_pointer* /*pointer*/, uint32_t serial,
                                 struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y) {
    auto* impl = impl_of(data);
    const SurfaceId sid = impl->surface_id(surface);
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->pointer_surface = surface;
        impl->pointer_focus = sid;
        impl->pointer_enter_serial = serial;
        impl->last_serial = serial;
        impl->pointer_x = wl_fixed_to_double(x);
        impl->pointer_y = wl_fixed_to_double(y);
    }
    impl->display->events().push(PointerEnterEvent{impl->seat_id(), sid, serial,
                                                   wl_fixed_to_double(x), wl_fixed_to_double(y)});
    // The cursor is per enter: set it again on every one.
    impl->apply_cursor();
}

static void pointer_handle_leave(void* data, struct wl_pointer* /*pointer*/, uint32_t serial,
                                 struct wl_surface* surface) {
    auto* impl = impl_of(data);
    const SurfaceId sid = impl->surface_id(surface);
    impl->flush_axis();
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->pointer_surface = nullptr;
        impl->pointer_focus = kNoSurface;
        impl->pointer_enter_serial = 0;
    }
    impl->display->events().push(PointerLeaveEvent{impl->seat_id(), sid, serial});
}

static void pointer_handle_motion(void* data, struct wl_pointer* /*pointer*/, uint32_t time,
                                  wl_fixed_t x, wl_fixed_t y) {
    auto* impl = impl_of(data);
    impl->pointer_x = wl_fixed_to_double(x);
    impl->pointer_y = wl_fixed_to_double(y);
    impl->display->events().push(PointerMotionEvent{impl->seat_id(), impl->pointer_focus, time,
                                                    impl->pointer_x, impl->pointer_y});
}

static void pointer_handle_button(void* data, struct wl_pointer* /*pointer*/, uint32_t serial,
                                  uint32_t time, uint32_t button, uint32_t state) {
    auto* impl = impl_of(data);
    impl->note_serial(serial);
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->last_press_serial = serial;
    }
    impl->display->events().push(PointerButtonEvent{impl->seat_id(), impl->pointer_focus, serial, time,
                                                    button, state == WL_POINTER_BUTTON_STATE_PRESSED});
}

static void pointer_handle_axis(void* data, struct wl_pointer* pointer, uint32_t time, uint32_t axis,
                                wl_fixed_t value) {
    auto* impl = impl_of(data);
    auto& a = impl->axis;
    a.any = true;
    a.time_ms = time;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        a.dy += wl_fixed_to_double(value);
    } else {
        a.dx += wl_fixed_to_double(value);
    }
    // Before wl_pointer v5 there is no frame: each axis event stands alone.
    if (wl_pointer_get_version(pointer) < WL_POINTER_FRAME_SINCE_VERSION) {
        impl->flush_axis();
    }
}

static void pointer_handle_frame(void* data, struct wl_pointer* /*pointer*/) {
    impl_of(data)->flush_axis();
}

static void pointer_handle_axis_source(void* data, struct wl_pointer* /*pointer*/, uint32_t source) {
    auto& a = impl_of(data)->axis;
    a.any = true;
    a.source = source <= 3 ? static_cast<AxisSource>(source) : AxisSource::Unknown;
}

static void pointer_handle_axis_stop(void* data, struct wl_pointer* /*pointer*/, uint32_t time,
                                     uint32_t axis) {
    auto& a = impl_of(data)->axis;
    a.any = true;
    a.time_ms = time;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        a.stop_y = true;
    } else {
        a.stop_x = true;
    }
}

static void pointer_handle_axis_discrete(void* data, struct wl_pointer* pointer, uint32_t axis,
                                         int32_t discrete) {
    // From v8 the compositor sends axis_value120 instead (and must not send this).
    if (wl_pointer_get_version(pointer) >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION) {
        return;
    }
    auto& a = impl_of(data)->axis;
    a.any = true;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        a.v120y += discrete * 120;
    } else {
        a.v120x += discrete * 120;
    }
}

static void pointer_handle_axis_value120(void* data, struct wl_pointer* /*pointer*/, uint32_t axis,
                                         int32_t value120) {
    auto& a = impl_of(data)->axis;
    a.any = true;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        a.v120y += value120;
    } else {
        a.v120x += value120;
    }
}

static void pointer_handle_axis_relative_direction(void* data, struct wl_pointer* /*pointer*/,
                                                   uint32_t axis, uint32_t direction) {
    auto& a = impl_of(data)->axis;
    const bool inverted = direction == WL_POINTER_AXIS_RELATIVE_DIRECTION_INVERTED;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        a.inverted_y = inverted;
    } else {
        a.inverted_x = inverted;
    }
}

// Filled member by member: newer libwayland headers add events past the
// version browl binds (wl_pointer.warp), which stay null and are never sent.
static const struct wl_pointer_listener pointer_listener = [] {
    wl_pointer_listener l{};
    l.enter = pointer_handle_enter;
    l.leave = pointer_handle_leave;
    l.motion = pointer_handle_motion;
    l.button = pointer_handle_button;
    l.axis = pointer_handle_axis;
    l.frame = pointer_handle_frame;
    l.axis_source = pointer_handle_axis_source;
    l.axis_stop = pointer_handle_axis_stop;
    l.axis_discrete = pointer_handle_axis_discrete;
    l.axis_value120 = pointer_handle_axis_value120;
    l.axis_relative_direction = pointer_handle_axis_relative_direction;
    return l;
}();

// --- zwp_relative_pointer_v1 / zwp_locked_pointer_v1 ----------------------------------

static void relative_handle_motion(void* data, struct zwp_relative_pointer_v1* /*rp*/,
                                   uint32_t utime_hi, uint32_t utime_lo, wl_fixed_t dx, wl_fixed_t dy,
                                   wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel) {
    auto* impl = impl_of(data);
    PointerRelativeMotionEvent ev;
    ev.seat = impl->seat_id();
    ev.time_us = (static_cast<uint64_t>(utime_hi) << 32) | utime_lo;
    ev.dx = wl_fixed_to_double(dx);
    ev.dy = wl_fixed_to_double(dy);
    ev.dx_unaccel = wl_fixed_to_double(dx_unaccel);
    ev.dy_unaccel = wl_fixed_to_double(dy_unaccel);
    impl->display->events().push(ev);
}

static const struct zwp_relative_pointer_v1_listener relative_listener = {
    .relative_motion = relative_handle_motion,
};

static void locked_handle_locked(void* data, struct zwp_locked_pointer_v1* /*lp*/) {
    auto* impl = impl_of(data);
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->pointer_is_locked = true;
    }
    impl->display->events().push(
        PointerLockEvent{impl->seat_id(), impl->surface_id(impl->lock_surface), true});
}

static void locked_handle_unlocked(void* data, struct zwp_locked_pointer_v1* /*lp*/) {
    auto* impl = impl_of(data);
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->pointer_is_locked = false;
    }
    impl->display->events().push(
        PointerLockEvent{impl->seat_id(), impl->surface_id(impl->lock_surface), false});
}

static const struct zwp_locked_pointer_v1_listener locked_listener = {
    .locked = locked_handle_locked,
    .unlocked = locked_handle_unlocked,
};

// XCursor names for a shape: the CSS name modern themes carry, then the
// legacy X11 names older themes have.
using CursorNames = std::array<const char*, 3>;  // unused slots are null

CursorNames cursor_names(CursorShape shape) {
    switch (shape) {
        case CursorShape::Default: return {"default", "left_ptr"};
        case CursorShape::ContextMenu: return {"context-menu", "left_ptr"};
        case CursorShape::Help: return {"help", "question_arrow", "left_ptr"};
        case CursorShape::Pointer: return {"pointer", "hand2", "hand1"};
        case CursorShape::Progress: return {"progress", "left_ptr_watch", "watch"};
        case CursorShape::Wait: return {"wait", "watch"};
        case CursorShape::Cell: return {"cell", "plus", "crosshair"};
        case CursorShape::Crosshair: return {"crosshair", "cross"};
        case CursorShape::Text: return {"text", "xterm"};
        case CursorShape::VerticalText: return {"vertical-text", "xterm"};
        case CursorShape::Alias: return {"alias", "dnd-link", "link"};
        case CursorShape::Copy: return {"copy", "dnd-copy"};
        case CursorShape::Move: return {"move", "fleur"};
        case CursorShape::NoDrop: return {"no-drop", "dnd-none", "crossed_circle"};
        case CursorShape::NotAllowed: return {"not-allowed", "crossed_circle"};
        case CursorShape::Grab: return {"grab", "openhand", "hand1"};
        case CursorShape::Grabbing: return {"grabbing", "closedhand", "fleur"};
        case CursorShape::EResize: return {"e-resize", "right_side"};
        case CursorShape::NResize: return {"n-resize", "top_side"};
        case CursorShape::NeResize: return {"ne-resize", "top_right_corner"};
        case CursorShape::NwResize: return {"nw-resize", "top_left_corner"};
        case CursorShape::SResize: return {"s-resize", "bottom_side"};
        case CursorShape::SeResize: return {"se-resize", "bottom_right_corner"};
        case CursorShape::SwResize: return {"sw-resize", "bottom_left_corner"};
        case CursorShape::WResize: return {"w-resize", "left_side"};
        case CursorShape::EwResize: return {"ew-resize", "sb_h_double_arrow", "h_double_arrow"};
        case CursorShape::NsResize: return {"ns-resize", "sb_v_double_arrow", "v_double_arrow"};
        case CursorShape::NeswResize: return {"nesw-resize", "fd_double_arrow"};
        case CursorShape::NwseResize: return {"nwse-resize", "bd_double_arrow"};
        case CursorShape::ColResize: return {"col-resize", "sb_h_double_arrow"};
        case CursorShape::RowResize: return {"row-resize", "sb_v_double_arrow"};
        case CursorShape::AllScroll: return {"all-scroll", "fleur"};
        case CursorShape::ZoomIn: return {"zoom-in"};
        case CursorShape::ZoomOut: return {"zoom-out"};
        default: return {"default", "left_ptr"};
    }
}

int cursor_size() {
    const char* s = std::getenv("XCURSOR_SIZE");
    const int n = s ? std::atoi(s) : 0;
    return n > 0 && n <= 512 ? n : 24;
}

}  // namespace

// ---- Impl ---------------------------------------------------------------------------

void Seat::Impl::bind_pointer() {
    pointer = wl_seat_get_pointer(seat->wl_seat_ptr());
    if (!pointer) {
        return;
    }
    wl_pointer_add_listener(pointer, &pointer_listener, this);
    auto& app = display->app_globals();
    if (app.cursor_shape_manager) {
        cursor_shape_device = wp_cursor_shape_manager_v1_get_pointer(app.cursor_shape_manager, pointer);
    }
}

void Seat::Impl::release_pointer() {
    destroy_lock();
    if (cursor_shape_device) {
        wp_cursor_shape_device_v1_destroy(cursor_shape_device);
        cursor_shape_device = nullptr;
    }
    if (cursor_surface) {
        wl_surface_destroy(cursor_surface);
        cursor_surface = nullptr;
    }
    if (cursor_theme) {
        wl_cursor_theme_destroy(cursor_theme);
        cursor_theme = nullptr;
        cursor_theme_scale = 0;
    }
    if (pointer) {
        if (wl_pointer_get_version(pointer) >= WL_POINTER_RELEASE_SINCE_VERSION) {
            wl_pointer_release(pointer);
        } else {
            wl_pointer_destroy(pointer);
        }
        pointer = nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex);
    pointer_surface = nullptr;
    pointer_focus = kNoSurface;
    pointer_enter_serial = 0;
}

void Seat::Impl::flush_axis() {
    if (!axis.any) {
        return;
    }
    PointerAxisEvent ev;
    ev.seat = seat_id();
    ev.surface_id = pointer_focus;
    ev.time_ms = axis.time_ms;
    ev.dx = axis.dx;
    ev.dy = axis.dy;
    ev.v120x = axis.v120x;
    ev.v120y = axis.v120y;
    ev.source = axis.source;
    ev.stop_x = axis.stop_x;
    ev.stop_y = axis.stop_y;
    ev.inverted_x = axis.inverted_x;
    ev.inverted_y = axis.inverted_y;
    axis = AxisFrame{};
    display->events().push(ev);
}

void Seat::Impl::apply_cursor() {
    if (!pointer || !pointer_surface || pointer_enter_serial == 0) {
        return;
    }
    const uint32_t serial = pointer_enter_serial;
    if (cursor == CursorShape::Hidden) {
        wl_pointer_set_cursor(pointer, serial, nullptr, 0, 0);
        return;
    }
    if (cursor_shape_device) {
        wp_cursor_shape_device_v1_set_shape(cursor_shape_device, serial, static_cast<uint32_t>(cursor));
        return;
    }
    // No cursor-shape protocol: draw the theme's cursor on a cursor surface
    // at the integer scale of the surface the pointer is over.
    auto& app = display->app_globals();
    wl_shm* shm = display->wl_shm_ptr();
    if (!shm || !display->wl_compositor_ptr()) {
        return;
    }
    const int32_t scale = app.cursor_scale_for(pointer_surface);
    if (!cursor_theme || cursor_theme_scale != scale) {
        if (cursor_theme) {
            wl_cursor_theme_destroy(cursor_theme);
        }
        cursor_theme = wl_cursor_theme_load(std::getenv("XCURSOR_THEME"), cursor_size() * scale, shm);
        cursor_theme_scale = cursor_theme ? scale : 0;
    }
    if (!cursor_theme) {
        return;
    }
    wl_cursor* wc = nullptr;
    for (const char* name : cursor_names(cursor)) {
        if (name && (wc = wl_cursor_theme_get_cursor(cursor_theme, name))) {
            break;
        }
    }
    if (!wc) {
        wc = wl_cursor_theme_get_cursor(cursor_theme, "left_ptr");
    }
    if (!wc || wc->image_count == 0) {
        return;
    }
    wl_cursor_image* image = wc->images[0];
    wl_buffer* buffer = wl_cursor_image_get_buffer(image);
    if (!buffer) {
        return;
    }
    if (!cursor_surface) {
        cursor_surface = wl_compositor_create_surface(display->wl_compositor_ptr());
    }
    wl_pointer_set_cursor(pointer, serial, cursor_surface, static_cast<int32_t>(image->hotspot_x) / scale,
                          static_cast<int32_t>(image->hotspot_y) / scale);
    if (wl_surface_get_version(cursor_surface) >= WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION) {
        wl_surface_set_buffer_scale(cursor_surface, scale);
    }
    wl_surface_attach(cursor_surface, buffer, 0, 0);
    if (wl_surface_get_version(cursor_surface) >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION) {
        wl_surface_damage_buffer(cursor_surface, 0, 0, static_cast<int32_t>(image->width),
                                 static_cast<int32_t>(image->height));
    } else {
        wl_surface_damage(cursor_surface, 0, 0, static_cast<int32_t>(image->width),
                          static_cast<int32_t>(image->height));
    }
    wl_surface_commit(cursor_surface);
}

void Seat::Impl::destroy_lock() {
    const bool was_locked = pointer_is_locked;
    if (locked_pointer) {
        zwp_locked_pointer_v1_destroy(locked_pointer);
        locked_pointer = nullptr;
    }
    if (relative_pointer) {
        zwp_relative_pointer_v1_destroy(relative_pointer);
        relative_pointer = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        pointer_is_locked = false;
    }
    if (was_locked && display) {
        display->events().push(PointerLockEvent{seat_id(), surface_id(lock_surface), false});
    }
    lock_surface = nullptr;
}

// ---- Seat ---------------------------------------------------------------------------

void Seat::set_cursor(CursorShape shape) {
    impl_->cursor = shape;
    impl_->apply_cursor();
}

CursorShape Seat::cursor() const {
    return impl_->cursor;
}

bool Seat::lock_pointer(wl_surface* surface) {
    auto& app = display_->app_globals();
    if (!surface || !impl_->pointer || !app.pointer_constraints || !app.relative_pointer_manager) {
        return false;
    }
    impl_->destroy_lock();
    impl_->lock_surface = surface;
    impl_->relative_pointer =
        zwp_relative_pointer_manager_v1_get_relative_pointer(app.relative_pointer_manager, impl_->pointer);
    zwp_relative_pointer_v1_add_listener(impl_->relative_pointer, &relative_listener, impl_.get());
    impl_->locked_pointer =
        zwp_pointer_constraints_v1_lock_pointer(app.pointer_constraints, surface, impl_->pointer, nullptr,
                                                ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    zwp_locked_pointer_v1_add_listener(impl_->locked_pointer, &locked_listener, impl_.get());
    return true;
}

void Seat::unlock_pointer() {
    impl_->destroy_lock();
}

bool Seat::pointer_locked() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->pointer_is_locked;
}

void Seat::warp_pointer(wl_surface* surface, double x, double y) {
    auto& app = display_->app_globals();
    if (!surface || !impl_->pointer || !app.pointer_constraints) {
        return;
    }
    if (impl_->locked_pointer && impl_->lock_surface == surface) {
        zwp_locked_pointer_v1_set_cursor_position_hint(impl_->locked_pointer, wl_fixed_from_double(x),
                                                       wl_fixed_from_double(y));
        wl_surface_commit(surface);
        return;
    }
    // Lock for a moment with the hint: compositors that honour hints move
    // the pointer there when the lock ends.
    zwp_locked_pointer_v1* lp =
        zwp_pointer_constraints_v1_lock_pointer(app.pointer_constraints, surface, impl_->pointer, nullptr,
                                                ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
    zwp_locked_pointer_v1_set_cursor_position_hint(lp, wl_fixed_from_double(x), wl_fixed_from_double(y));
    wl_surface_commit(surface);
    zwp_locked_pointer_v1_destroy(lp);
}

}  // namespace browl
