// Keymap over xkbcommon, without a compositor: keymaps from RMLVO names
// (and from their own text, as wl_keyboard.keymap carries it), keysyms and
// text per modifier state, the reverse lookup, repeat, layout names,
// modifier decoding, and concurrent queries from several threads.
#include "browl/keymap.h"
#include "browl/types.h"
#include "check.h"

#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace browl;

namespace {

constexpr uint32_t KEY_1 = 2;
constexpr uint32_t KEY_A = 30;
constexpr uint32_t KEY_E = 18;
constexpr uint32_t KEY_Q = 16;
constexpr uint32_t KEY_LEFTSHIFT = 42;
constexpr uint32_t KEY_ENTER = 28;
constexpr uint32_t KEY_Y = 21;
constexpr uint32_t KEY_Z = 44;

void test_us() {
    auto km = Keymap::from_names("us");
    REQUIRE(km != nullptr);
    CHECK_EQ(km->keysym(KEY_A), uint32_t(XKB_KEY_a));
    CHECK_EQ(km->keysym(KEY_A, modifier::Shift), uint32_t(XKB_KEY_A));
    CHECK_EQ(km->keysym(KEY_A, modifier::CapsLock), uint32_t(XKB_KEY_A));
    CHECK_EQ(km->keysym(KEY_1, modifier::Shift), uint32_t(XKB_KEY_exclam));
    CHECK_EQ(km->utf8(KEY_A), std::string("a"));
    CHECK_EQ(km->utf8(KEY_A, modifier::Shift), std::string("A"));
    CHECK_EQ(km->utf8(KEY_1, modifier::Shift), std::string("!"));
    CHECK_EQ(km->keysym(KEY_ENTER), uint32_t(XKB_KEY_Return));
    CHECK_EQ(km->keysym(1000), uint32_t(XKB_KEY_NoSymbol));

    uint32_t key = 0, mods = 0;
    CHECK(km->key_for_keysym(XKB_KEY_A, &key, &mods));
    CHECK_EQ(key, KEY_A);
    CHECK_EQ(mods, modifier::Shift);
    CHECK(km->key_for_keysym(XKB_KEY_a, &key, &mods));
    CHECK_EQ(key, KEY_A);
    CHECK_EQ(mods, 0u);
    CHECK(km->key_for_keysym(XKB_KEY_exclam, &key, &mods));
    CHECK_EQ(key, KEY_1);
    CHECK_EQ(mods, modifier::Shift);
    CHECK(!km->key_for_keysym(XKB_KEY_Greek_alpha, &key, &mods));

    CHECK(km->repeats(KEY_A));
    CHECK(!km->repeats(KEY_LEFTSHIFT));
    CHECK_EQ(km->layout_count(), 1u);
    CHECK(km->layout_name(0).find("English") != std::string::npos);

    // Raw masks as wl_keyboard.modifiers carries them: Shift is xkb's
    // modifier 0, Lock 1, Control 2, Mod1 3, Mod2 4, Mod4 6.
    CHECK_EQ(km->decode_modifiers(1u << 0, 0, 0, 0), modifier::Shift);
    CHECK_EQ(km->decode_modifiers(1u << 2, 0, 1u << 1, 0), modifier::Ctrl | modifier::CapsLock);
    CHECK_EQ(km->decode_modifiers(1u << 3 | 1u << 6, 0, 0, 0), modifier::Alt | modifier::Logo);
    CHECK_EQ(km->decode_modifiers(0, 0, 1u << 4, 0), modifier::NumLock);

    CHECK_EQ(Keymap::keysym_to_utf32(XKB_KEY_eacute), 0xe9u);
    CHECK_EQ(Keymap::keysym_to_utf32(XKB_KEY_Return), 0x0du);
    CHECK_EQ(Keymap::keysym_name(XKB_KEY_Return), std::string("Return"));
    CHECK(km->xkb_keymap_ptr() != nullptr);

    // The keymap's own text compiles back to the same layout.
    char* text = xkb_keymap_get_as_string(static_cast<xkb_keymap*>(km->xkb_keymap_ptr()),
                                          XKB_KEYMAP_FORMAT_TEXT_V1);
    REQUIRE(text != nullptr);
    auto again = Keymap::from_string(text);
    free(text);
    REQUIRE(again != nullptr);
    CHECK_EQ(again->keysym(KEY_A, modifier::Shift), uint32_t(XKB_KEY_A));
    CHECK(Keymap::from_string("not a keymap") == nullptr);
}

void test_layouts() {
    // German: y and z trade places; AltGr+q is @ and AltGr+e the euro sign.
    auto de = Keymap::from_names("de");
    REQUIRE(de != nullptr);
    CHECK_EQ(de->keysym(KEY_Y), uint32_t(XKB_KEY_z));
    CHECK_EQ(de->keysym(KEY_Z), uint32_t(XKB_KEY_y));
    CHECK_EQ(de->utf8(KEY_Q, modifier::AltGr), std::string("@"));
    CHECK_EQ(de->utf8(KEY_E, modifier::AltGr), std::string("\xe2\x82\xac"));
    uint32_t key = 0, mods = 0;
    CHECK(de->key_for_keysym(XKB_KEY_at, &key, &mods));
    CHECK_EQ(key, KEY_Q);
    CHECK_EQ(mods, modifier::AltGr);

    // Two layouts: the second group is Russian.
    auto two = Keymap::from_names("us,ru");
    REQUIRE(two != nullptr);
    CHECK_EQ(two->layout_count(), 2u);
    CHECK_EQ(two->keysym(KEY_A, 0, 0), uint32_t(XKB_KEY_a));
    CHECK_EQ(two->keysym(KEY_A, 0, 1), uint32_t(XKB_KEY_Cyrillic_ef));
    CHECK_EQ(two->utf8(KEY_A, 0, 1), std::string("\xd1\x84"));
    CHECK(two->layout_name(1).find("Russian") != std::string::npos);
}

void test_threads() {
    auto km = Keymap::from_names("us");
    REQUIRE(km != nullptr);
    std::atomic<int> wrong{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < 20000; ++i) {
                const bool shift = ((i + t) & 1) != 0;
                const uint32_t sym = km->keysym(KEY_A, shift ? modifier::Shift : 0);
                if (sym != (shift ? uint32_t(XKB_KEY_A) : uint32_t(XKB_KEY_a))) ++wrong;
            }
        });
    }
    for (auto& th : threads) th.join();
    CHECK_EQ(wrong.load(), 0);
}

}  // namespace

int main() {
    test_us();
    test_layouts();
    test_threads();
    return bstest::finish("test_keymap");
}
