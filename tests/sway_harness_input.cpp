#include "sway_harness.h"

#include "virtual-keyboard-unstable-v1-client-protocol.h"

#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdlib>
#include <cstring>

namespace swaytest {

struct VirtualKeyboardRegistry {
    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t) {
        auto* vk = static_cast<VirtualKeyboard*>(data);
        if (std::strcmp(interface, wl_seat_interface.name) == 0 && !vk->seat_) {
            vk->seat_ = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
        } else if (std::strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
            vk->manager_ = static_cast<zwp_virtual_keyboard_manager_v1*>(
                wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
        }
    }
    static void global_remove(void*, wl_registry*, uint32_t) {}
};

namespace {
const wl_registry_listener vk_registry_listener = {
    .global = VirtualKeyboardRegistry::global,
    .global_remove = VirtualKeyboardRegistry::global_remove,
};
}  // namespace

VirtualKeyboard::~VirtualKeyboard() {
    if (keyboard_) zwp_virtual_keyboard_v1_destroy(keyboard_);
    if (manager_) zwp_virtual_keyboard_manager_v1_destroy(manager_);
    if (seat_) wl_seat_destroy(seat_);
    if (registry_) wl_registry_destroy(registry_);
    if (display_) wl_display_disconnect(display_);
}

bool VirtualKeyboard::connect(const std::string& name, std::string* error) {
    display_ = wl_display_connect(name.c_str());
    if (!display_) {
        if (error) *error = "cannot connect to " + name;
        return false;
    }
    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &vk_registry_listener, this);
    wl_display_roundtrip(display_);
    if (!seat_ || !manager_) {
        if (error) *error = "the compositor has no wl_seat or zwp_virtual_keyboard_manager_v1";
        return false;
    }
    keyboard_ = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(manager_, seat_);

    // The keymap the keyboard types with.
    xkb_context* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_rule_names names = {};
    names.layout = "us";
    xkb_keymap* km = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    char* text = km ? xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1) : nullptr;
    bool ok = false;
    if (text) {
        const size_t size = std::strlen(text) + 1;
        const int fd = memfd_create("browl-test-vk-keymap", MFD_CLOEXEC);
        size_t off = 0;
        while (fd >= 0 && off < size) {
            const ssize_t n = write(fd, text + off, size - off);
            if (n <= 0) break;
            off += static_cast<size_t>(n);
        }
        if (fd >= 0 && off == size) {
            zwp_virtual_keyboard_v1_keymap(keyboard_, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd,
                                           static_cast<uint32_t>(size));
            ok = true;
        }
        if (fd >= 0) close(fd);
        std::free(text);
    }
    if (km) xkb_keymap_unref(km);
    xkb_context_unref(ctx);
    if (!ok) {
        if (error) *error = "could not build the virtual keyboard's keymap";
        return false;
    }
    return wl_display_roundtrip(display_) >= 0;
}

void VirtualKeyboard::key(uint32_t key, bool pressed) {
    if (!keyboard_) return;
    zwp_virtual_keyboard_v1_key(keyboard_, time_++, key,
                                pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
    wl_display_flush(display_);
}

int VirtualKeyboard::roundtrip() {
    return display_ ? wl_display_roundtrip(display_) : -1;
}

}  // namespace swaytest
