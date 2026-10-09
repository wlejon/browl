#include "browl/seat.h"

#include "browl/display.h"

#include "app_globals.h"
#include "seat_impl.h"

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

SelectionContents text_selection(const std::string& utf8) {
    std::vector<uint8_t> bytes(utf8.begin(), utf8.end());
    SelectionContents contents;
    for (const char* mime : {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "TEXT", "STRING"}) {
        contents.emplace_back(mime, bytes);
    }
    return contents;
}

// ---- Impl: shared bookkeeping ---------------------------------------------------

void Seat::Impl::note_serial(uint32_t serial) {
    std::lock_guard<std::mutex> lock(mutex);
    last_serial = serial;
}

SeatId Seat::Impl::seat_id() const {
    return seat->id();
}

SurfaceId Seat::Impl::surface_id(wl_surface* surface) const {
    return display ? display->surface_id_of(surface) : kNoSurface;
}

void Seat::Impl::update_devices() {
    if (!input_enabled || !seat->wl_seat_ptr()) {
        return;
    }
    if (seat->has_pointer() && !pointer) {
        bind_pointer();
    } else if (!seat->has_pointer() && pointer) {
        release_pointer();
    }
    if (seat->has_keyboard() && !keyboard) {
        bind_keyboard();
    } else if (!seat->has_keyboard() && keyboard) {
        release_keyboard();
    }
    if (seat->has_touch() && !touch) {
        bind_touch();
    } else if (!seat->has_touch() && touch) {
        release_touch();
    }
    bind_data_devices();
    bind_text_input();
}

void Seat::Impl::release_all() {
    release_text_input();
    release_data_devices();
    release_touch();
    release_keyboard();
    release_pointer();
    release_xkb();
}

// ---- Seat ---------------------------------------------------------------------------

Seat::Seat(SeatId id, wl_seat* wl_seat, Display* display)
    : id_(id), wl_seat_(wl_seat), display_(display), impl_(std::make_unique<Impl>(this, display)) {
    if (wl_seat_) {
        wl_seat_add_listener(wl_seat_, &seat_listener, this);
    }
}

Seat::~Seat() {
    detach();
}

void Seat::detach() {
    if (impl_) {
        impl_->release_all();
        impl_->input_enabled = false;
    }
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

    impl_->update_devices();

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

void Seat::enable_input() {
    impl_->input_enabled = true;
    impl_->update_devices();
}

std::shared_ptr<const Keymap> Seat::keymap() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->keymap;
}

uint32_t Seat::modifiers() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->modifiers;
}

int32_t Seat::repeat_rate() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->repeat_rate;
}

int32_t Seat::repeat_delay_ms() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->repeat_delay_ms;
}

uint32_t Seat::last_input_serial() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->last_serial;
}

uint32_t Seat::pointer_enter_serial() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->pointer_enter_serial;
}

SurfaceId Seat::pointer_focus() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->pointer_focus;
}

SurfaceId Seat::keyboard_focus() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->keyboard_focus;
}

}  // namespace browl
