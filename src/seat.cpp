#include "browl/seat.h"

#include "browl/display.h"

#include <wayland-client.h>

namespace browl {

namespace {

static void seat_handle_capabilities(void* data, struct wl_seat* /*wl_seat*/,
                                     uint32_t capabilities) {
    auto* seat = static_cast<Seat*>(data);
    if (seat) {
        seat->handle_capabilities(capabilities);
    }
}

static void seat_handle_name(void* data, struct wl_seat* /*wl_seat*/, const char* name) {
    auto* seat = static_cast<Seat*>(data);
    if (seat) {
        seat->handle_name(name);
    }
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

}  // namespace

Seat::Seat(SeatId id, wl_seat* wl_seat, Display* display)
    : id_(id), wl_seat_(wl_seat), display_(display) {
    if (wl_seat_) {
        wl_seat_add_listener(wl_seat_, &seat_listener, this);
    }
}

Seat::~Seat() {
    detach();
}

void Seat::detach() {
    if (wl_seat_) {
        if (wl_seat_get_version(wl_seat_) >= WL_SEAT_RELEASE_SINCE_VERSION) {
            wl_seat_release(wl_seat_);
        } else {
            wl_seat_destroy(wl_seat_);
        }
        wl_seat_ = nullptr;
    }
}

SeatSnapshot Seat::snapshot() const {
    SeatSnapshot snap;
    snap.id = id_;
    snap.name = name_;
    snap.has_pointer = has_pointer_;
    snap.has_keyboard = has_keyboard_;
    snap.has_touch = has_touch_;
    return snap;
}

void Seat::handle_capabilities(uint32_t capabilities) {
    has_pointer_ = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
    has_keyboard_ = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
    has_touch_ = (capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0;

    if (display_) {
        display_->events().push(SeatChangedEvent{snapshot()});
    }
}

void Seat::handle_name(const char* name) {
    if (name) {
        name_ = name;
        if (display_) {
            display_->events().push(SeatChangedEvent{snapshot()});
        }
    }
}

}  // namespace browl
