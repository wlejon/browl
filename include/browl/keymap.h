#pragma once

// A seat's keyboard layout, from the keymap the compositor sent
// (wl_keyboard.keymap, compiled with xkbcommon). Immutable and safe to query
// from any thread; a layout change replaces the Seat's Keymap (KeymapEvent).
// Keys are Linux key codes (evdev: KEY_A = 30); keysyms are XKB keysyms.

#include <cstdint>
#include <memory>
#include <string>

namespace browl {

class Keymap {
public:
    ~Keymap();
    Keymap(const Keymap&) = delete;
    Keymap& operator=(const Keymap&) = delete;

    /// Compile a keymap from its XKB text (what wl_keyboard.keymap carries).
    /// Null when xkbcommon rejects it.
    static std::shared_ptr<const Keymap> from_string(const std::string& xkb_keymap_text);
    /// The keymap for an RMLVO description ("" fields: xkbcommon's defaults,
    /// which honour XKB_DEFAULT_LAYOUT and friends). Null on failure.
    static std::shared_ptr<const Keymap> from_names(const std::string& layout = "",
                                                    const std::string& variant = "",
                                                    const std::string& options = "");

    /// The keysym `key` produces in layout `group` with `modifiers` (modifier
    /// bits) held; 0 for a key the layout does not have.
    uint32_t keysym(uint32_t key, uint32_t modifiers = 0, uint32_t group = 0) const;
    /// The text the key types in that state ("" for none).
    std::string utf8(uint32_t key, uint32_t modifiers = 0, uint32_t group = 0) const;
    /// A key (and the modifier bits it needs, when `modifiers` is given) that
    /// produces `keysym` in layout `group`: the lowest key code and shift
    /// level that does. False when no key does.
    bool key_for_keysym(uint32_t keysym, uint32_t* key, uint32_t* modifiers = nullptr,
                        uint32_t group = 0) const;
    /// Whether holding `key` repeats (xkb_keymap_key_repeats).
    bool repeats(uint32_t key) const;
    uint32_t layout_count() const;
    std::string layout_name(uint32_t group) const;

    /// The modifier bits that raw xkb masks (wl_keyboard.modifiers) mean here.
    uint32_t decode_modifiers(uint32_t depressed, uint32_t latched, uint32_t locked,
                              uint32_t group) const;

    /// The Unicode code point a keysym stands for (0 for none) — keymap-free.
    static uint32_t keysym_to_utf32(uint32_t keysym);
    static std::string keysym_name(uint32_t keysym);

    /// The xkb_keymap* behind this (for a caller that keeps its own xkb_state).
    void* xkb_keymap_ptr() const;

    struct Impl;
    explicit Keymap(std::unique_ptr<Impl> impl);

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace browl
