#pragma once

// Keymap::Impl, shared by keymap.cpp and the seat's keyboard (which maps
// xkb modifier masks to modifier bits with the same indices).

#include "browl/keymap.h"

#include <xkbcommon/xkbcommon.h>

#include <mutex>

namespace browl {

struct Keymap::Impl {
    ~Impl();
    void init_mods();
    /// modifier bits <-> the keymap's xkb modifier mask.
    uint32_t mask_of(uint32_t modifiers) const;
    uint32_t bits_of(uint32_t mask) const;
    /// Puts the query state into `modifiers` / `group` (mutex held).
    void set_state(uint32_t modifiers, uint32_t group) const;

    xkb_context* context = nullptr;
    xkb_keymap* keymap = nullptr;
    // The state the query methods use, under the mutex: Keymap is shared
    // across threads, xkb_state is not thread-safe.
    xkb_state* state = nullptr;
    mutable std::mutex mutex;

    xkb_mod_index_t shift = XKB_MOD_INVALID;
    xkb_mod_index_t ctrl = XKB_MOD_INVALID;
    xkb_mod_index_t alt = XKB_MOD_INVALID;
    xkb_mod_index_t logo = XKB_MOD_INVALID;
    xkb_mod_index_t caps = XKB_MOD_INVALID;
    xkb_mod_index_t num = XKB_MOD_INVALID;
    xkb_mod_index_t level3 = XKB_MOD_INVALID;
    xkb_mod_index_t level5 = XKB_MOD_INVALID;
};

}  // namespace browl
