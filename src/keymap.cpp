#include "browl/keymap.h"

#include "browl/types.h"
#include "keymap_impl.h"

#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <mutex>

namespace browl {

Keymap::Impl::~Impl() {
    if (state) {
        xkb_state_unref(state);
    }
    if (keymap) {
        xkb_keymap_unref(keymap);
    }
    if (context) {
        xkb_context_unref(context);
    }
}

void Keymap::Impl::init_mods() {
    shift = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
    ctrl = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
    alt = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_ALT);
    logo = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_LOGO);
    caps = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CAPS);
    num = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_NUM);
    // ISO_Level3_Shift and ISO_Level5_Shift are Mod5 and Mod3 in every
    // xkeyboard-config layout.
    level3 = xkb_keymap_mod_get_index(keymap, "Mod5");
    level5 = xkb_keymap_mod_get_index(keymap, "Mod3");
}

namespace {

uint32_t bit_of(xkb_mod_index_t idx) {
    return (idx == XKB_MOD_INVALID || idx >= 32) ? 0 : (1u << idx);
}

std::shared_ptr<const Keymap> wrap(xkb_context* ctx, xkb_keymap* km) {
    if (!km) {
        xkb_context_unref(ctx);
        return nullptr;
    }
    auto impl = std::make_unique<Keymap::Impl>();
    impl->context = ctx;
    impl->keymap = km;
    impl->state = xkb_state_new(km);
    impl->init_mods();
    return std::make_shared<const Keymap>(std::move(impl));
}

}  // namespace

uint32_t Keymap::Impl::mask_of(uint32_t modifiers) const {
    uint32_t mask = 0;
    if (modifiers & modifier::Shift) mask |= bit_of(shift);
    if (modifiers & modifier::Ctrl) mask |= bit_of(ctrl);
    if (modifiers & modifier::Alt) mask |= bit_of(alt);
    if (modifiers & modifier::Logo) mask |= bit_of(logo);
    if (modifiers & modifier::CapsLock) mask |= bit_of(caps);
    if (modifiers & modifier::NumLock) mask |= bit_of(num);
    if (modifiers & modifier::AltGr) mask |= bit_of(level3);
    if (modifiers & modifier::Level5) mask |= bit_of(level5);
    return mask;
}

uint32_t Keymap::Impl::bits_of(uint32_t mask) const {
    uint32_t bits = 0;
    if (mask & bit_of(shift)) bits |= modifier::Shift;
    if (mask & bit_of(ctrl)) bits |= modifier::Ctrl;
    if (mask & bit_of(alt)) bits |= modifier::Alt;
    if (mask & bit_of(logo)) bits |= modifier::Logo;
    if (mask & bit_of(caps)) bits |= modifier::CapsLock;
    if (mask & bit_of(num)) bits |= modifier::NumLock;
    if (mask & bit_of(level3)) bits |= modifier::AltGr;
    if (mask & bit_of(level5)) bits |= modifier::Level5;
    return bits;
}

void Keymap::Impl::set_state(uint32_t modifiers, uint32_t group) const {
    // Caps Lock and Num Lock are locks; the rest are held.
    const uint32_t locks = modifiers & (modifier::CapsLock | modifier::NumLock);
    xkb_state_update_mask(state, mask_of(modifiers & ~locks), 0, mask_of(locks), 0, 0, group);
}

Keymap::Keymap(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Keymap::~Keymap() = default;

std::shared_ptr<const Keymap> Keymap::from_string(const std::string& text) {
    xkb_context* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx) {
        return nullptr;
    }
    xkb_keymap* km = xkb_keymap_new_from_buffer(ctx, text.data(), text.size(),
                                                XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    return wrap(ctx, km);
}

std::shared_ptr<const Keymap> Keymap::from_names(const std::string& layout, const std::string& variant,
                                                 const std::string& options) {
    xkb_context* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx) {
        return nullptr;
    }
    xkb_rule_names names = {};
    names.layout = layout.empty() ? nullptr : layout.c_str();
    names.variant = variant.empty() ? nullptr : variant.c_str();
    names.options = options.empty() ? nullptr : options.c_str();
    xkb_keymap* km = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    return wrap(ctx, km);
}

uint32_t Keymap::keysym(uint32_t key, uint32_t modifiers, uint32_t group) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->set_state(modifiers, group);
    return xkb_state_key_get_one_sym(impl_->state, key + 8);
}

std::string Keymap::utf8(uint32_t key, uint32_t modifiers, uint32_t group) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->set_state(modifiers, group);
    char buf[64];
    const int n = xkb_state_key_get_utf8(impl_->state, key + 8, buf, sizeof(buf));
    return n > 0 ? std::string(buf, std::min<size_t>(size_t(n), sizeof(buf) - 1)) : std::string();
}

bool Keymap::key_for_keysym(uint32_t keysym, uint32_t* key, uint32_t* modifiers, uint32_t group) const {
    xkb_keymap* km = impl_->keymap;
    const xkb_keycode_t min = xkb_keymap_min_keycode(km);
    const xkb_keycode_t max = xkb_keymap_max_keycode(km);
    for (xkb_keycode_t kc = std::max<xkb_keycode_t>(min, 8); kc <= max; ++kc) {
        const xkb_layout_index_t layouts = xkb_keymap_num_layouts_for_key(km, kc);
        if (layouts == 0) {
            continue;
        }
        const xkb_layout_index_t layout = group < layouts ? group : 0;
        const xkb_level_index_t levels = xkb_keymap_num_levels_for_key(km, kc, layout);
        for (xkb_level_index_t level = 0; level < levels; ++level) {
            const xkb_keysym_t* syms = nullptr;
            const int n = xkb_keymap_key_get_syms_by_level(km, kc, layout, level, &syms);
            if (n != 1 || syms[0] != keysym) {
                continue;
            }
            uint32_t bits = 0;
            if (level > 0) {
                xkb_mod_mask_t masks[16];
                const size_t m = xkb_keymap_key_get_mods_for_level(km, kc, layout, level, masks, 16);
                if (m == 0) {
                    continue;
                }
                // The fewest modifiers that select the level.
                xkb_mod_mask_t best = masks[0];
                for (size_t i = 1; i < m; ++i) {
                    if (__builtin_popcount(masks[i]) < __builtin_popcount(best)) {
                        best = masks[i];
                    }
                }
                bits = impl_->bits_of(best);
            }
            if (key) *key = kc - 8;
            if (modifiers) *modifiers = bits;
            return true;
        }
    }
    return false;
}

bool Keymap::repeats(uint32_t key) const {
    return xkb_keymap_key_repeats(impl_->keymap, key + 8) != 0;
}

uint32_t Keymap::layout_count() const {
    return xkb_keymap_num_layouts(impl_->keymap);
}

std::string Keymap::layout_name(uint32_t group) const {
    const char* name = xkb_keymap_layout_get_name(impl_->keymap, group);
    return name ? std::string(name) : std::string();
}

uint32_t Keymap::decode_modifiers(uint32_t depressed, uint32_t latched, uint32_t locked,
                                  uint32_t /*group*/) const {
    return impl_->bits_of(depressed | latched | locked);
}

uint32_t Keymap::keysym_to_utf32(uint32_t keysym) {
    return xkb_keysym_to_utf32(keysym);
}

std::string Keymap::keysym_name(uint32_t keysym) {
    char buf[128];
    const int n = xkb_keysym_get_name(keysym, buf, sizeof(buf));
    return n > 0 ? std::string(buf) : std::string();
}

void* Keymap::xkb_keymap_ptr() const {
    return impl_->keymap;
}

}  // namespace browl
