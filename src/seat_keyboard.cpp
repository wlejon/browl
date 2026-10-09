// A seat's keyboard: the keymap (wl_keyboard.keymap -> Keymap), an xkb
// state that interprets keys and modifiers, and compose (dead keys and
// Compose sequences) so KeyEvent::utf8 is the text after composition.
// browl does not repeat keys; RepeatInfoEvent and Keymap::repeats tell the
// consumer how to.
#include "browl/display.h"

#include "keymap_impl.h"
#include "seat_impl.h"

#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdlib>
#include <cstring>

namespace browl {

namespace {

Seat::Impl* impl_of(void* data) {
    return static_cast<Seat::Impl*>(data);
}

// The locale compose sequences are looked up in, as setlocale(LC_CTYPE, "")
// would choose it.
std::string compose_locale() {
    for (const char* var : {"LC_ALL", "LC_CTYPE", "LANG"}) {
        const char* v = std::getenv(var);
        if (v && *v) {
            return v;
        }
    }
    return "C";
}

// Text a key types, without the C0 controls and DEL that xkb produces for
// Return, Tab, Escape, BackSpace, Delete and Ctrl+letter: those are not text,
// and the keysym says what they are.
std::string printable(const char* utf8) {
    if (!utf8 || !*utf8) {
        return {};
    }
    const auto c = static_cast<unsigned char>(utf8[0]);
    if (utf8[1] == '\0' && (c < 0x20 || c == 0x7f)) {
        return {};
    }
    return utf8;
}

// --- wl_keyboard ----------------------------------------------------------------

static void keyboard_handle_keymap(void* data, struct wl_keyboard* /*keyboard*/, uint32_t format,
                                   int32_t fd, uint32_t size) {
    auto* impl = impl_of(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0) {
        close(fd);
        return;
    }
    // MAP_PRIVATE: from wl_keyboard v7 the compositor may share one
    // read-only file among its clients.
    void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) {
        return;
    }
    const auto* text = static_cast<const char*>(map);
    std::string keymap_text(text, strnlen(text, size));
    munmap(map, size);

    auto keymap = Keymap::from_string(keymap_text);
    if (!keymap) {
        return;
    }
    xkb_state* state = xkb_state_new(static_cast<xkb_keymap*>(keymap->xkb_keymap_ptr()));
    if (!state) {
        return;
    }
    if (impl->xkb) {
        xkb_state_unref(impl->xkb);
    }
    impl->xkb = state;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->keymap = std::move(keymap);
        impl->modifiers = 0;
    }
    if (!impl->compose_tried) {
        impl->compose_tried = true;
        xkb_context* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (ctx) {
            std::string locale = compose_locale();
            impl->compose_table =
                xkb_compose_table_new_from_locale(ctx, locale.c_str(), XKB_COMPOSE_COMPILE_NO_FLAGS);
            if (!impl->compose_table && (locale == "C" || locale == "POSIX")) {
                impl->compose_table =
                    xkb_compose_table_new_from_locale(ctx, "C.UTF-8", XKB_COMPOSE_COMPILE_NO_FLAGS);
            }
            if (impl->compose_table) {
                impl->compose = xkb_compose_state_new(impl->compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
            }
            xkb_context_unref(ctx);
        }
    } else if (impl->compose) {
        xkb_compose_state_reset(impl->compose);
    }
    impl->display->events().push(KeymapEvent{impl->seat_id()});
}

static void keyboard_handle_enter(void* data, struct wl_keyboard* /*keyboard*/, uint32_t serial,
                                  struct wl_surface* surface, struct wl_array* keys) {
    auto* impl = impl_of(data);
    const SurfaceId sid = impl->surface_id(surface);
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->keyboard_surface = surface;
        impl->keyboard_focus = sid;
        impl->last_serial = serial;
    }
    KeyboardEnterEvent ev;
    ev.seat = impl->seat_id();
    ev.surface_id = sid;
    ev.serial = serial;
    const auto* k = static_cast<const uint32_t*>(keys->data);
    ev.keys.assign(k, k + keys->size / sizeof(uint32_t));
    impl->display->events().push(std::move(ev));
}

static void keyboard_handle_leave(void* data, struct wl_keyboard* /*keyboard*/, uint32_t serial,
                                  struct wl_surface* surface) {
    auto* impl = impl_of(data);
    const SurfaceId sid = impl->surface_id(surface);
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->keyboard_surface = nullptr;
        impl->keyboard_focus = kNoSurface;
    }
    if (impl->compose) {
        xkb_compose_state_reset(impl->compose);
    }
    impl->display->events().push(KeyboardLeaveEvent{impl->seat_id(), sid, serial});
}

static void keyboard_handle_key(void* data, struct wl_keyboard* /*keyboard*/, uint32_t serial,
                                uint32_t time, uint32_t key, uint32_t state) {
    auto* impl = impl_of(data);
    impl->note_serial(serial);
    KeyEvent ev;
    ev.seat = impl->seat_id();
    ev.surface_id = impl->keyboard_focus;
    ev.serial = serial;
    ev.time_ms = time;
    ev.key = key;
    ev.pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        ev.modifiers = impl->modifiers;
    }
    if (impl->xkb) {
        const xkb_keycode_t code = key + 8;
        const xkb_keysym_t sym = xkb_state_key_get_one_sym(impl->xkb, code);
        ev.keysym = sym;
        if (ev.pressed) {
            bool composed = false;
            if (impl->compose && sym != XKB_KEY_NoSymbol &&
                xkb_compose_state_feed(impl->compose, sym) == XKB_COMPOSE_FEED_ACCEPTED) {
                switch (xkb_compose_state_get_status(impl->compose)) {
                    case XKB_COMPOSE_COMPOSING:
                        composed = true;  // mid-sequence: no text yet
                        break;
                    case XKB_COMPOSE_COMPOSED: {
                        char buf[64];
                        xkb_compose_state_get_utf8(impl->compose, buf, sizeof(buf));
                        ev.utf8 = printable(buf);
                        const xkb_keysym_t csym = xkb_compose_state_get_one_sym(impl->compose);
                        if (csym != XKB_KEY_NoSymbol) {
                            ev.keysym = csym;
                        }
                        xkb_compose_state_reset(impl->compose);
                        composed = true;
                        break;
                    }
                    case XKB_COMPOSE_CANCELLED:
                        xkb_compose_state_reset(impl->compose);
                        composed = true;  // the sequence was abandoned: no text
                        break;
                    case XKB_COMPOSE_NOTHING:
                        break;
                }
            }
            if (!composed) {
                char buf[64];
                if (xkb_state_key_get_utf8(impl->xkb, code, buf, sizeof(buf)) > 0) {
                    ev.utf8 = printable(buf);
                }
            }
        }
    }
    impl->display->events().push(std::move(ev));
}

static void keyboard_handle_modifiers(void* data, struct wl_keyboard* /*keyboard*/, uint32_t serial,
                                      uint32_t depressed, uint32_t latched, uint32_t locked,
                                      uint32_t group) {
    auto* impl = impl_of(data);
    uint32_t bits = 0;
    if (impl->xkb) {
        xkb_state_update_mask(impl->xkb, depressed, latched, locked, 0, 0, group);
        const xkb_mod_mask_t effective = xkb_state_serialize_mods(impl->xkb, XKB_STATE_MODS_EFFECTIVE);
        std::shared_ptr<const Keymap> km;
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            km = impl->keymap;
        }
        if (km) {
            bits = km->decode_modifiers(effective, 0, 0, group);
        }
    }
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->modifiers = bits;
    }
    impl->display->events().push(
        ModifiersEvent{impl->seat_id(), serial, depressed, latched, locked, group, bits});
}

static void keyboard_handle_repeat_info(void* data, struct wl_keyboard* /*keyboard*/, int32_t rate,
                                        int32_t delay) {
    auto* impl = impl_of(data);
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->repeat_rate = rate;
        impl->repeat_delay_ms = delay;
    }
    impl->display->events().push(RepeatInfoEvent{impl->seat_id(), rate, delay});
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_handle_keymap,
    .enter = keyboard_handle_enter,
    .leave = keyboard_handle_leave,
    .key = keyboard_handle_key,
    .modifiers = keyboard_handle_modifiers,
    .repeat_info = keyboard_handle_repeat_info,
};

}  // namespace

void Seat::Impl::bind_keyboard() {
    keyboard = wl_seat_get_keyboard(seat->wl_seat_ptr());
    if (keyboard) {
        wl_keyboard_add_listener(keyboard, &keyboard_listener, this);
    }
}

void Seat::Impl::release_keyboard() {
    if (keyboard) {
        if (wl_keyboard_get_version(keyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION) {
            wl_keyboard_release(keyboard);
        } else {
            wl_keyboard_destroy(keyboard);
        }
        keyboard = nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex);
    keyboard_surface = nullptr;
    keyboard_focus = kNoSurface;
    modifiers = 0;
}

void Seat::Impl::release_xkb() {
    if (compose) {
        xkb_compose_state_unref(compose);
        compose = nullptr;
    }
    if (compose_table) {
        xkb_compose_table_unref(compose_table);
        compose_table = nullptr;
    }
    compose_tried = false;
    if (xkb) {
        xkb_state_unref(xkb);
        xkb = nullptr;
    }
}

}  // namespace browl
