// The test double's seat and what hangs off it: wl_pointer, wl_keyboard
// (keymaps compiled with xkbcommon, as a compositor sends them), wl_touch,
// wl_data_device and the primary selection (selections forwarded between
// clients exactly as a compositor does: offers to every device, receive
// passed to the source's send), and zwp_text_input_v3.
#include "headless_compositor.h"

#include <sys/mman.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>

#include "primary-selection-unstable-v1-server-protocol.h"
#include "text-input-unstable-v3-server-protocol.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace browl::test {

namespace {

HeadlessCompositor* comp_of(struct wl_resource* resource) {
    return static_cast<HeadlessCompositor*>(wl_resource_get_user_data(resource));
}

void destroy_req(struct wl_client* /*client*/, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

template <class T>
void forget(T*& slot, struct wl_resource* resource) {
    if (slot == resource) slot = nullptr;
}

// --- wl_pointer / wl_keyboard / wl_touch ---------------------------------------------

void pointer_set_cursor_req(struct wl_client*, struct wl_resource* resource, uint32_t /*serial*/,
                            struct wl_resource* surface, int32_t, int32_t) {
    if (!surface) ++comp_of(resource)->app_.cursor_hidden;
}

const struct wl_pointer_interface pointer_impl = {
    .set_cursor = pointer_set_cursor_req,
    .release = destroy_req,
};

const struct wl_keyboard_interface keyboard_impl = {
    .release = destroy_req,
};

const struct wl_touch_interface touch_impl = {
    .release = destroy_req,
};

void pointer_destroyed(struct wl_resource* r) { forget(comp_of(r)->pointer_resource_, r); }
void keyboard_destroyed(struct wl_resource* r) { forget(comp_of(r)->keyboard_resource_, r); }
void touch_destroyed(struct wl_resource* r) { forget(comp_of(r)->touch_resource_, r); }

void seat_get_pointer_req(struct wl_client* client, struct wl_resource* resource, uint32_t id) {
    auto* comp = comp_of(resource);
    auto* res = wl_resource_create(client, &wl_pointer_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &pointer_impl, comp, pointer_destroyed);
    comp->pointer_resource_ = res;
}

void seat_get_keyboard_req(struct wl_client* client, struct wl_resource* resource, uint32_t id) {
    auto* comp = comp_of(resource);
    auto* res = wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &keyboard_impl, comp, keyboard_destroyed);
    comp->keyboard_resource_ = res;
}

void seat_get_touch_req(struct wl_client* client, struct wl_resource* resource, uint32_t id) {
    auto* comp = comp_of(resource);
    auto* res = wl_resource_create(client, &wl_touch_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &touch_impl, comp, touch_destroyed);
    comp->touch_resource_ = res;
}

const struct wl_seat_interface seat_impl = {
    .get_pointer = seat_get_pointer_req,
    .get_keyboard = seat_get_keyboard_req,
    .get_touch = seat_get_touch_req,
    .release = destroy_req,
};

void bind_seat(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    auto* comp = static_cast<HeadlessCompositor*>(data);
    auto* res = wl_resource_create(client, &wl_seat_interface, static_cast<int>(version), id);
    wl_resource_set_implementation(res, &seat_impl, data, nullptr);
    comp->seat_resource_ = res;
    wl_seat_send_capabilities(res, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
    if (version >= 2) {
        wl_seat_send_name(res, "seat0");
    }
}

// --- selections (clipboard and primary) -----------------------------------------------

void source_offer_req(struct wl_client*, struct wl_resource* resource, const char* mime) {
    static_cast<CompositorSource*>(wl_resource_get_user_data(resource))->mimes.emplace_back(mime);
}

void data_source_set_actions_req(struct wl_client*, struct wl_resource*, uint32_t) {}

const struct wl_data_source_interface data_source_impl = {
    .offer = source_offer_req,
    .destroy = destroy_req,
    .set_actions = data_source_set_actions_req,
};

const struct zwp_primary_selection_source_v1_interface primary_source_impl = {
    .offer = source_offer_req,
    .destroy = destroy_req,
};

void source_destroyed(struct wl_resource* resource) {
    static_cast<CompositorSource*>(wl_resource_get_user_data(resource))->resource = nullptr;
}

// An offer's user data is the CompositorSource it offers.
void offer_receive(struct wl_resource* resource, const char* mime, int32_t fd) {
    auto* src = static_cast<CompositorSource*>(wl_resource_get_user_data(resource));
    if (src && src->resource) {
        if (src->primary) {
            zwp_primary_selection_source_v1_send_send(src->resource, mime, fd);
        } else {
            wl_data_source_send_send(src->resource, mime, fd);
        }
    }
    close(fd);
}

void data_offer_accept_req(struct wl_client*, struct wl_resource*, uint32_t, const char*) {}
void data_offer_receive_req(struct wl_client*, struct wl_resource* resource, const char* mime, int32_t fd) {
    offer_receive(resource, mime, fd);
}
void data_offer_finish_req(struct wl_client*, struct wl_resource*) {}
void data_offer_set_actions_req(struct wl_client*, struct wl_resource*, uint32_t, uint32_t) {}

const struct wl_data_offer_interface data_offer_impl = {
    .accept = data_offer_accept_req,
    .receive = data_offer_receive_req,
    .destroy = destroy_req,
    .finish = data_offer_finish_req,
    .set_actions = data_offer_set_actions_req,
};

void primary_offer_receive_req(struct wl_client*, struct wl_resource* resource, const char* mime, int32_t fd) {
    offer_receive(resource, mime, fd);
}

const struct zwp_primary_selection_offer_v1_interface primary_offer_impl = {
    .receive = primary_offer_receive_req,
    .destroy = destroy_req,
};

// Sends the current selection to one device (a fresh offer, or null).
void send_selection_to(CompositorSource* src, struct wl_resource* device, bool primary) {
    struct wl_client* client = wl_resource_get_client(device);
    const int version = wl_resource_get_version(device);
    if (!src || !src->resource) {
        if (primary) {
            zwp_primary_selection_device_v1_send_selection(device, nullptr);
        } else {
            wl_data_device_send_selection(device, nullptr);
        }
        return;
    }
    if (primary) {
        auto* offer = wl_resource_create(client, &zwp_primary_selection_offer_v1_interface, version, 0);
        wl_resource_set_implementation(offer, &primary_offer_impl, src, nullptr);
        zwp_primary_selection_device_v1_send_data_offer(device, offer);
        for (const auto& m : src->mimes) zwp_primary_selection_offer_v1_send_offer(offer, m.c_str());
        zwp_primary_selection_device_v1_send_selection(device, offer);
    } else {
        auto* offer = wl_resource_create(client, &wl_data_offer_interface, version, 0);
        wl_resource_set_implementation(offer, &data_offer_impl, src, nullptr);
        wl_data_device_send_data_offer(device, offer);
        for (const auto& m : src->mimes) wl_data_offer_send_offer(offer, m.c_str());
        wl_data_device_send_selection(device, offer);
    }
}

void set_selection(HeadlessCompositor* comp, struct wl_resource* source, bool primary) {
    CompositorSource*& current = primary ? comp->primary_selection_ : comp->clipboard_selection_;
    CompositorSource* next =
        source ? static_cast<CompositorSource*>(wl_resource_get_user_data(source)) : nullptr;
    if (current && current != next && current->resource) {
        if (primary) {
            zwp_primary_selection_source_v1_send_cancelled(current->resource);
        } else {
            wl_data_source_send_cancelled(current->resource);
        }
    }
    current = next;
    for (auto* device : primary ? comp->primary_devices_ : comp->data_devices_) {
        send_selection_to(current, device, primary);
    }
}

void data_device_start_drag_req(struct wl_client*, struct wl_resource*, struct wl_resource*,
                                struct wl_resource*, struct wl_resource*, uint32_t) {}

void data_device_set_selection_req(struct wl_client*, struct wl_resource* resource, struct wl_resource* source,
                                   uint32_t /*serial*/) {
    set_selection(comp_of(resource), source, false);
}

const struct wl_data_device_interface data_device_impl = {
    .start_drag = data_device_start_drag_req,
    .set_selection = data_device_set_selection_req,
    .release = destroy_req,
};

void primary_device_set_selection_req(struct wl_client*, struct wl_resource* resource,
                                      struct wl_resource* source, uint32_t /*serial*/) {
    set_selection(comp_of(resource), source, true);
}

const struct zwp_primary_selection_device_v1_interface primary_device_impl = {
    .set_selection = primary_device_set_selection_req,
    .destroy = destroy_req,
};

void data_device_destroyed(struct wl_resource* resource) {
    auto& v = comp_of(resource)->data_devices_;
    v.erase(std::remove(v.begin(), v.end(), resource), v.end());
}

void primary_device_destroyed(struct wl_resource* resource) {
    auto& v = comp_of(resource)->primary_devices_;
    v.erase(std::remove(v.begin(), v.end(), resource), v.end());
}

struct wl_resource* create_source(HeadlessCompositor* comp, struct wl_client* client, struct wl_resource* manager,
                                  uint32_t id, bool primary) {
    auto src = std::make_unique<CompositorSource>();
    src->primary = primary;
    auto* res = wl_resource_create(client, primary ? &zwp_primary_selection_source_v1_interface : &wl_data_source_interface,
                                   wl_resource_get_version(manager), id);
    wl_resource_set_implementation(res, primary ? static_cast<const void*>(&primary_source_impl)
                                                : static_cast<const void*>(&data_source_impl),
                                   src.get(), source_destroyed);
    src->resource = res;
    comp->sources_.push_back(std::move(src));
    return res;
}

void data_manager_create_source_req(struct wl_client* client, struct wl_resource* resource, uint32_t id) {
    create_source(comp_of(resource), client, resource, id, false);
}

void data_manager_get_device_req(struct wl_client* client, struct wl_resource* resource, uint32_t id,
                                 struct wl_resource* /*seat*/) {
    auto* comp = comp_of(resource);
    auto* res = wl_resource_create(client, &wl_data_device_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &data_device_impl, comp, data_device_destroyed);
    comp->data_devices_.push_back(res);
    // As on keyboard focus: the client learns the current selection.
    if (comp->clipboard_selection_) send_selection_to(comp->clipboard_selection_, res, false);
}

const struct wl_data_device_manager_interface data_manager_impl = {
    .create_data_source = data_manager_create_source_req,
    .get_data_device = data_manager_get_device_req,
};

void primary_manager_create_source_req(struct wl_client* client, struct wl_resource* resource, uint32_t id) {
    create_source(comp_of(resource), client, resource, id, true);
}

void primary_manager_get_device_req(struct wl_client* client, struct wl_resource* resource, uint32_t id,
                                    struct wl_resource* /*seat*/) {
    auto* comp = comp_of(resource);
    auto* res = wl_resource_create(client, &zwp_primary_selection_device_v1_interface,
                                   wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &primary_device_impl, comp, primary_device_destroyed);
    comp->primary_devices_.push_back(res);
    if (comp->primary_selection_) send_selection_to(comp->primary_selection_, res, true);
}

const struct zwp_primary_selection_device_manager_v1_interface primary_manager_impl = {
    .create_source = primary_manager_create_source_req,
    .get_device = primary_manager_get_device_req,
    .destroy = destroy_req,
};

// --- zwp_text_input_v3 ---------------------------------------------------------------

void ti_enable_req(struct wl_client*, struct wl_resource* r) { comp_of(r)->app_.text_input_enabled = true; }
void ti_disable_req(struct wl_client*, struct wl_resource* r) { comp_of(r)->app_.text_input_enabled = false; }
void ti_set_surrounding_req(struct wl_client*, struct wl_resource*, const char*, int32_t, int32_t) {}
void ti_set_cause_req(struct wl_client*, struct wl_resource*, uint32_t) {}
void ti_set_content_type_req(struct wl_client*, struct wl_resource* r, uint32_t hint, uint32_t purpose) {
    comp_of(r)->app_.text_input_hints = hint;
    comp_of(r)->app_.text_input_purpose = purpose;
}
void ti_set_cursor_rect_req(struct wl_client*, struct wl_resource*, int32_t, int32_t, int32_t, int32_t) {}
void ti_commit_req(struct wl_client*, struct wl_resource* r) { ++comp_of(r)->app_.text_input_commits; }

const struct zwp_text_input_v3_interface text_input_impl = [] {
    struct zwp_text_input_v3_interface i{};  // `struct`: the wl_interface variable shares the name
    i.destroy = destroy_req;
    i.enable = ti_enable_req;
    i.disable = ti_disable_req;
    i.set_surrounding_text = ti_set_surrounding_req;
    i.set_text_change_cause = ti_set_cause_req;
    i.set_content_type = ti_set_content_type_req;
    i.set_cursor_rectangle = ti_set_cursor_rect_req;
    i.commit = ti_commit_req;
    return i;
}();

void text_input_destroyed(struct wl_resource* r) { forget(comp_of(r)->text_input_resource_, r); }

void ti_manager_get_req(struct wl_client* client, struct wl_resource* resource, uint32_t id,
                        struct wl_resource* /*seat*/) {
    auto* comp = comp_of(resource);
    auto* res = wl_resource_create(client, &zwp_text_input_v3_interface, wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &text_input_impl, comp, text_input_destroyed);
    comp->text_input_resource_ = res;
}

const struct zwp_text_input_manager_v3_interface ti_manager_impl = {
    .destroy = destroy_req,
    .get_text_input = ti_manager_get_req,
};

template <const wl_interface* Iface, auto Impl>
void bind_with(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    auto* res = wl_resource_create(client, Iface, static_cast<int>(version), id);
    wl_resource_set_implementation(res, Impl, data, nullptr);
}

}  // namespace

void HeadlessCompositor::init_input_globals() {
    seat_global_ = wl_global_create(display_, &wl_seat_interface, 9, this, bind_seat);
    wl_global_create(display_, &wl_data_device_manager_interface, 3, this,
                     bind_with<&wl_data_device_manager_interface, &data_manager_impl>);
    wl_global_create(display_, &zwp_primary_selection_device_manager_v1_interface, 1, this,
                     bind_with<&zwp_primary_selection_device_manager_v1_interface, &primary_manager_impl>);
    wl_global_create(display_, &zwp_text_input_manager_v3_interface, 1, this,
                     bind_with<&zwp_text_input_manager_v3_interface, &ti_manager_impl>);
}

void HeadlessCompositor::send_keymap(const std::string& layout, const std::string& variant) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!keyboard_resource_) return;
    xkb_context* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_rule_names names = {};
    names.layout = layout.c_str();
    names.variant = variant.empty() ? nullptr : variant.c_str();
    xkb_keymap* km = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    char* text = km ? xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1) : nullptr;
    if (text) {
        const size_t size = std::strlen(text) + 1;
        int fd = memfd_create("browl-test-keymap", MFD_CLOEXEC);
        size_t off = 0;
        while (fd >= 0 && off < size) {
            const ssize_t n = write(fd, text + off, size - off);
            if (n <= 0) break;
            off += static_cast<size_t>(n);
        }
        if (fd >= 0) {
            wl_keyboard_send_keymap(keyboard_resource_, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd,
                                    static_cast<uint32_t>(size));
            close(fd);
        }
        std::free(text);
    }
    if (km) xkb_keymap_unref(km);
    xkb_context_unref(ctx);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::send_keyboard_enter(const std::vector<uint32_t>& keys) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!keyboard_resource_ || !window_surface_) return;
    wl_array arr;
    wl_array_init(&arr);
    for (uint32_t k : keys) *static_cast<uint32_t*>(wl_array_add(&arr, sizeof(uint32_t))) = k;
    wl_keyboard_send_enter(keyboard_resource_, next_serial_++, window_surface_, &arr);
    wl_array_release(&arr);
    if (text_input_resource_) zwp_text_input_v3_send_enter(text_input_resource_, window_surface_);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::send_keyboard_leave() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!keyboard_resource_ || !window_surface_) return;
    wl_keyboard_send_leave(keyboard_resource_, next_serial_++, window_surface_);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::send_key(uint32_t key, bool pressed, uint32_t time_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!keyboard_resource_) return;
    wl_keyboard_send_key(keyboard_resource_, next_serial_++, time_ms, key,
                         pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::send_modifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!keyboard_resource_) return;
    wl_keyboard_send_modifiers(keyboard_resource_, next_serial_++, depressed, latched, locked, group);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::send_repeat_info(int32_t rate, int32_t delay_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!keyboard_resource_) return;
    wl_keyboard_send_repeat_info(keyboard_resource_, rate, delay_ms);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::send_pointer_enter(double x, double y) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pointer_resource_ || !window_surface_) return;
    wl_pointer_send_enter(pointer_resource_, next_serial_++, window_surface_, wl_fixed_from_double(x),
                          wl_fixed_from_double(y));
    wl_pointer_send_frame(pointer_resource_);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::send_pointer_leave() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pointer_resource_ || !window_surface_) return;
    wl_pointer_send_leave(pointer_resource_, next_serial_++, window_surface_);
    wl_pointer_send_frame(pointer_resource_);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::send_pointer_motion(double x, double y, uint32_t time_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pointer_resource_) return;
    wl_pointer_send_motion(pointer_resource_, time_ms, wl_fixed_from_double(x), wl_fixed_from_double(y));
    wl_pointer_send_frame(pointer_resource_);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::send_pointer_button(uint32_t button, bool pressed) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pointer_resource_) return;
    wl_pointer_send_button(pointer_resource_, next_serial_++, 0, button,
                           pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
    wl_pointer_send_frame(pointer_resource_);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::send_pointer_scroll(int32_t value120_x, int32_t value120_y, double dx, double dy,
                                             uint32_t source) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pointer_resource_) return;
    const int version = wl_resource_get_version(pointer_resource_);
    wl_pointer_send_axis_source(pointer_resource_, source);
    if (value120_y && version >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION) {
        wl_pointer_send_axis_value120(pointer_resource_, WL_POINTER_AXIS_VERTICAL_SCROLL, value120_y);
    }
    if (value120_x && version >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION) {
        wl_pointer_send_axis_value120(pointer_resource_, WL_POINTER_AXIS_HORIZONTAL_SCROLL, value120_x);
    }
    if (dy != 0) {
        wl_pointer_send_axis(pointer_resource_, 7, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_double(dy));
    }
    if (dx != 0) {
        wl_pointer_send_axis(pointer_resource_, 7, WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double(dx));
    }
    wl_pointer_send_frame(pointer_resource_);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::send_touch_down(int32_t id, double x, double y) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!touch_resource_ || !window_surface_) return;
    wl_touch_send_down(touch_resource_, next_serial_++, 0, window_surface_, id, wl_fixed_from_double(x),
                       wl_fixed_from_double(y));
    wl_touch_send_frame(touch_resource_);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::send_touch_motion(int32_t id, double x, double y) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!touch_resource_) return;
    wl_touch_send_motion(touch_resource_, 0, id, wl_fixed_from_double(x), wl_fixed_from_double(y));
    wl_touch_send_frame(touch_resource_);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::send_touch_up(int32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!touch_resource_) return;
    wl_touch_send_up(touch_resource_, next_serial_++, 0, id);
    wl_touch_send_frame(touch_resource_);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::send_text_input_enter() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!text_input_resource_ || !window_surface_) return;
    zwp_text_input_v3_send_enter(text_input_resource_, window_surface_);
    wl_display_flush_clients(display_);
}

void HeadlessCompositor::send_text_input_done(const std::string& preedit, int32_t begin, int32_t end,
                                              const std::string& commit, uint32_t delete_before,
                                              uint32_t delete_after) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!text_input_resource_) return;
    if (delete_before || delete_after) {
        zwp_text_input_v3_send_delete_surrounding_text(text_input_resource_, delete_before, delete_after);
    }
    if (!commit.empty()) zwp_text_input_v3_send_commit_string(text_input_resource_, commit.c_str());
    zwp_text_input_v3_send_preedit_string(text_input_resource_, preedit.empty() ? nullptr : preedit.c_str(),
                                          begin, end);
    zwp_text_input_v3_send_done(text_input_resource_, static_cast<uint32_t>(app_.text_input_commits));
    wl_display_flush_clients(display_);
}

uint32_t HeadlessCompositor::last_serial() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return next_serial_ - 1;
}

bool HeadlessCompositor::input_bound() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pointer_resource_ != nullptr && keyboard_resource_ != nullptr;
}

}  // namespace browl::test
