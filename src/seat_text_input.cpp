// A seat's text input (zwp_text_input_v3): the channel to the input method.
// Its events are double-buffered and applied at done, which browl turns
// into one TextInputEvent.
#include "browl/display.h"

#include "app_globals.h"
#include "seat_impl.h"
#include "text-input-unstable-v3-client-protocol.h"

#include <wayland-client.h>

namespace browl {

namespace {

Seat::Impl* impl_of(void* data) {
    return static_cast<Seat::Impl*>(data);
}

static void text_input_handle_enter(void* data, struct zwp_text_input_v3* /*ti*/,
                                    struct wl_surface* surface) {
    auto* impl = impl_of(data);
    const SurfaceId sid = impl->surface_id(surface);
    impl->text_input_surface = surface;
    impl->text_input_focus = sid;
    impl->display->events().push(TextInputFocusEvent{impl->seat_id(), sid, true});
}

static void text_input_handle_leave(void* data, struct zwp_text_input_v3* ti, struct wl_surface* surface) {
    auto* impl = impl_of(data);
    const SurfaceId sid = impl->surface_id(surface);
    if (impl->text_input_enabled) {
        // Leaving the surface ends the session; the next enter starts over.
        zwp_text_input_v3_disable(ti);
        zwp_text_input_v3_commit(ti);
        ++impl->text_input_commits;
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->text_input_enabled = false;
    }
    impl->text_input_surface = nullptr;
    impl->text_input_focus = kNoSurface;
    impl->text_pending = Seat::Impl::TextInputPending{};
    impl->display->events().push(TextInputFocusEvent{impl->seat_id(), sid, false});
}

static void text_input_handle_preedit_string(void* data, struct zwp_text_input_v3* /*ti*/, const char* text,
                                             int32_t cursor_begin, int32_t cursor_end) {
    auto& p = impl_of(data)->text_pending;
    p.preedit = text ? text : "";
    p.cursor_begin = cursor_begin;
    p.cursor_end = cursor_end;
}

static void text_input_handle_commit_string(void* data, struct zwp_text_input_v3* /*ti*/, const char* text) {
    impl_of(data)->text_pending.commit = text ? text : "";
}

static void text_input_handle_delete_surrounding_text(void* data, struct zwp_text_input_v3* /*ti*/,
                                                      uint32_t before_length, uint32_t after_length) {
    auto& p = impl_of(data)->text_pending;
    p.delete_before = before_length;
    p.delete_after = after_length;
}

static void text_input_handle_done(void* data, struct zwp_text_input_v3* /*ti*/, uint32_t serial) {
    auto* impl = impl_of(data);
    auto& p = impl->text_pending;
    TextInputEvent ev;
    ev.seat = impl->seat_id();
    ev.surface_id = impl->text_input_focus;
    ev.preedit = std::move(p.preedit);
    ev.preedit_cursor_begin = p.cursor_begin;
    ev.preedit_cursor_end = p.cursor_end;
    ev.commit = std::move(p.commit);
    ev.delete_before = p.delete_before;
    ev.delete_after = p.delete_after;
    ev.serial = serial;
    p = Seat::Impl::TextInputPending{};
    impl->display->events().push(std::move(ev));
}

static const struct zwp_text_input_v3_listener text_input_listener = {
    .enter = text_input_handle_enter,
    .leave = text_input_handle_leave,
    .preedit_string = text_input_handle_preedit_string,
    .commit_string = text_input_handle_commit_string,
    .delete_surrounding_text = text_input_handle_delete_surrounding_text,
    .done = text_input_handle_done,
    .action = nullptr,          // v2; bound at v1
    .language = nullptr,        // v2
    .preedit_hint = nullptr,    // v2
};

}  // namespace

void Seat::Impl::bind_text_input() {
    auto& app = display->app_globals();
    if (text_input || !app.text_input_manager) {
        return;
    }
    text_input = zwp_text_input_manager_v3_get_text_input(app.text_input_manager, seat->wl_seat_ptr());
    zwp_text_input_v3_add_listener(text_input, &text_input_listener, this);
}

void Seat::Impl::release_text_input() {
    if (text_input) {
        zwp_text_input_v3_destroy(text_input);
        text_input = nullptr;
    }
    text_input_surface = nullptr;
    text_input_focus = kNoSurface;
    std::lock_guard<std::mutex> lock(mutex);
    text_input_enabled = false;
}

// ---- Seat ---------------------------------------------------------------------------

bool Seat::has_text_input() const {
    return impl_->text_input != nullptr;
}

void Seat::enable_text_input(uint32_t hints, ContentPurpose purpose) {
    zwp_text_input_v3* ti = impl_->text_input;
    if (!ti) {
        return;
    }
    zwp_text_input_v3_enable(ti);
    // content_hint and ContentPurpose carry the protocol's own values.
    zwp_text_input_v3_set_content_type(ti, hints & 0x3ffu, static_cast<uint32_t>(purpose));
    zwp_text_input_v3_commit(ti);
    ++impl_->text_input_commits;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->text_input_enabled = true;
}

void Seat::disable_text_input() {
    zwp_text_input_v3* ti = impl_->text_input;
    if (!ti) {
        return;
    }
    zwp_text_input_v3_disable(ti);
    zwp_text_input_v3_commit(ti);
    ++impl_->text_input_commits;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->text_input_enabled = false;
}

bool Seat::text_input_enabled() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->text_input_enabled;
}

void Seat::set_text_input_cursor_rect(const Rect& rect) {
    zwp_text_input_v3* ti = impl_->text_input;
    if (!ti || !text_input_enabled()) {
        return;
    }
    zwp_text_input_v3_set_cursor_rectangle(ti, rect.x, rect.y, rect.width, rect.height);
    zwp_text_input_v3_commit(ti);
    ++impl_->text_input_commits;
}

void Seat::set_surrounding_text(const std::string& text, int32_t cursor, int32_t anchor) {
    zwp_text_input_v3* ti = impl_->text_input;
    // The protocol caps the text at 4000 bytes (the wire's message size).
    if (!ti || !text_input_enabled() || text.size() >= 4000) {
        return;
    }
    zwp_text_input_v3_set_surrounding_text(ti, text.c_str(), cursor, anchor);
    zwp_text_input_v3_commit(ti);
    ++impl_->text_input_commits;
}

}  // namespace browl
