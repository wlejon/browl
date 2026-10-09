// A seat's touch: wl_touch points, each reported against the surface it
// went down on.
#include "browl/display.h"

#include "seat_impl.h"

#include <wayland-client.h>

namespace browl {

namespace {

Seat::Impl* impl_of(void* data) {
    return static_cast<Seat::Impl*>(data);
}

static void touch_handle_down(void* data, struct wl_touch* /*touch*/, uint32_t serial, uint32_t time,
                              struct wl_surface* surface, int32_t id, wl_fixed_t x, wl_fixed_t y) {
    auto* impl = impl_of(data);
    impl->note_serial(serial);
    const SurfaceId sid = impl->surface_id(surface);
    const double fx = wl_fixed_to_double(x);
    const double fy = wl_fixed_to_double(y);
    impl->touches[id] = Seat::Impl::TouchPoint{sid, fx, fy};
    impl->display->events().push(TouchDownEvent{impl->seat_id(), sid, serial, time, id, fx, fy});
}

static void touch_handle_up(void* data, struct wl_touch* /*touch*/, uint32_t serial, uint32_t time,
                            int32_t id) {
    auto* impl = impl_of(data);
    impl->note_serial(serial);
    Seat::Impl::TouchPoint point;
    auto it = impl->touches.find(id);
    if (it != impl->touches.end()) {
        point = it->second;
        impl->touches.erase(it);
    }
    impl->display->events().push(
        TouchUpEvent{impl->seat_id(), point.surface_id, serial, time, id, point.x, point.y});
}

static void touch_handle_motion(void* data, struct wl_touch* /*touch*/, uint32_t time, int32_t id,
                                wl_fixed_t x, wl_fixed_t y) {
    auto* impl = impl_of(data);
    auto& point = impl->touches[id];
    point.x = wl_fixed_to_double(x);
    point.y = wl_fixed_to_double(y);
    impl->display->events().push(
        TouchMotionEvent{impl->seat_id(), point.surface_id, time, id, point.x, point.y});
}

static void touch_handle_frame(void* /*data*/, struct wl_touch* /*touch*/) {}

static void touch_handle_cancel(void* data, struct wl_touch* /*touch*/) {
    auto* impl = impl_of(data);
    impl->touches.clear();
    impl->display->events().push(TouchCancelEvent{impl->seat_id()});
}

static void touch_handle_shape(void* /*data*/, struct wl_touch* /*touch*/, int32_t /*id*/,
                               wl_fixed_t /*major*/, wl_fixed_t /*minor*/) {}

static void touch_handle_orientation(void* /*data*/, struct wl_touch* /*touch*/, int32_t /*id*/,
                                     wl_fixed_t /*orientation*/) {}

static const struct wl_touch_listener touch_listener = {
    .down = touch_handle_down,
    .up = touch_handle_up,
    .motion = touch_handle_motion,
    .frame = touch_handle_frame,
    .cancel = touch_handle_cancel,
    .shape = touch_handle_shape,
    .orientation = touch_handle_orientation,
};

}  // namespace

void Seat::Impl::bind_touch() {
    touch = wl_seat_get_touch(seat->wl_seat_ptr());
    if (touch) {
        wl_touch_add_listener(touch, &touch_listener, this);
    }
}

void Seat::Impl::release_touch() {
    if (touch) {
        if (wl_touch_get_version(touch) >= WL_TOUCH_RELEASE_SINCE_VERSION) {
            wl_touch_release(touch);
        } else {
            wl_touch_destroy(touch);
        }
        touch = nullptr;
    }
    touches.clear();
}

}  // namespace browl
