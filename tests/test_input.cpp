// Seat input against the in-process test double (fake_session.h): the
// keymap a compositor sends turned into a Keymap and into KeyEvents (keysym,
// text, modifiers, compose / dead keys, no text for control keys), repeat
// info, focus, pointer events with one PointerAxisEvent per frame, the
// cursor set on enter (cursor-shape) and hidden, touch points, and text
// input (zwp_text_input_v3) done-batches; and, against a compositor without
// cursor-shape, the XCursor image on a cursor surface.
#include "browl/browl.h"
#include "fake_session.h"

#include <wayland-client.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdlib>

using namespace browl;
using bstest::find_event;

namespace {

constexpr uint32_t KEY_A = 30;
constexpr uint32_t KEY_E = 18;
constexpr uint32_t KEY_APOSTROPHE = 40;
constexpr uint32_t KEY_ENTER = 28;
constexpr uint32_t BTN_LEFT = 0x110;

std::vector<ShellEvent> settle(Display& d) {
    d.roundtrip();
    d.roundtrip();
    return d.events().drain();
}

template <class E>
int count(const std::vector<ShellEvent>& events) {
    int n = 0;
    for (const auto& ev : events) n += std::holds_alternative<E>(ev) ? 1 : 0;
    return n;
}

bool compose_available(const char* locale) {
    xkb_context* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_compose_table* t = xkb_compose_table_new_from_locale(ctx, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
    const bool ok = t != nullptr;
    if (t) xkb_compose_table_unref(t);
    xkb_context_unref(ctx);
    return ok;
}

void run() {
    // Compose sequences come from the locale's table.
    setenv("LC_ALL", "en_US.UTF-8", 1);
    bstest::FakeSession s;
    REQUIRE(s.display != nullptr);
    Display& d = *s.display;
    auto seat = d.default_seat();
    REQUIRE(seat != nullptr);

    auto window = d.create_window(WindowConfig{});
    REQUIRE(window != nullptr);
    settle(d);
    CHECK(!s.server.input_bound());
    d.enable_input();
    d.enable_input();  // idempotent
    CHECK(d.input_enabled());
    settle(d);
    CHECK(s.server.input_bound());

    // --- Keyboard -------------------------------------------------------------
    CHECK(seat->keymap() == nullptr);
    s.server.send_keymap("us", "intl");
    auto events = settle(d);
    CHECK(find_event<KeymapEvent>(events) != nullptr);
    auto km = seat->keymap();
    REQUIRE(km != nullptr);
    CHECK_EQ(km->keysym(KEY_A), uint32_t(XKB_KEY_a));
    CHECK_EQ(km->keysym(KEY_APOSTROPHE), uint32_t(XKB_KEY_dead_acute));

    CHECK_EQ(seat->repeat_rate(), 25);
    s.server.send_repeat_info(33, 450);
    events = settle(d);
    const auto* rep = find_event<RepeatInfoEvent>(events);
    REQUIRE(rep != nullptr);
    CHECK_EQ(rep->rate, 33);
    CHECK_EQ(rep->delay_ms, 450);
    CHECK_EQ(seat->repeat_delay_ms(), 450);

    s.server.send_keyboard_enter({});
    events = settle(d);
    const auto* kenter = find_event<KeyboardEnterEvent>(events);
    REQUIRE(kenter != nullptr);
    CHECK_EQ(kenter->surface_id, window->id());
    CHECK_EQ(seat->keyboard_focus(), window->id());
    CHECK_EQ(seat->last_input_serial(), kenter->serial);
    // The double gives text-input focus with the keyboard's.
    const auto* tif = find_event<TextInputFocusEvent>(events);
    REQUIRE(tif != nullptr);
    CHECK(tif->entered);
    CHECK_EQ(tif->surface_id, window->id());

    s.server.send_key(KEY_A, true, 1000);
    s.server.send_key(KEY_A, false, 1010);
    events = settle(d);
    REQUIRE(count<KeyEvent>(events) == 2);
    const auto* press = find_event<KeyEvent>(events, [](const KeyEvent& e) { return e.pressed; });
    const auto* release = find_event<KeyEvent>(events, [](const KeyEvent& e) { return !e.pressed; });
    REQUIRE(press != nullptr);
    REQUIRE(release != nullptr);
    CHECK_EQ(press->key, KEY_A);
    CHECK_EQ(press->keysym, uint32_t(XKB_KEY_a));
    CHECK_EQ(press->utf8, std::string("a"));
    CHECK_EQ(press->time_ms, 1000u);
    CHECK_EQ(press->surface_id, window->id());
    CHECK(release->utf8.empty());
    CHECK_EQ(seat->last_input_serial(), release->serial);

    // Shift held (xkb modifier 0).
    s.server.send_modifiers(1u << 0, 0, 0, 0);
    s.server.send_key(KEY_A, true);
    events = settle(d);
    const auto* mods = find_event<ModifiersEvent>(events);
    REQUIRE(mods != nullptr);
    CHECK_EQ(mods->modifiers, modifier::Shift);
    CHECK_EQ(mods->depressed, 1u);
    CHECK_EQ(seat->modifiers(), modifier::Shift);
    press = find_event<KeyEvent>(events);
    REQUIRE(press != nullptr);
    CHECK_EQ(press->keysym, uint32_t(XKB_KEY_A));
    CHECK_EQ(press->utf8, std::string("A"));
    CHECK_EQ(press->modifiers, modifier::Shift);
    s.server.send_key(KEY_A, false);
    s.server.send_modifiers(0, 0, 0, 0);
    settle(d);

    // Return is a key, not text.
    s.server.send_key(KEY_ENTER, true);
    events = settle(d);
    press = find_event<KeyEvent>(events);
    REQUIRE(press != nullptr);
    CHECK_EQ(press->keysym, uint32_t(XKB_KEY_Return));
    CHECK(press->utf8.empty());
    s.server.send_key(KEY_ENTER, false);

    // Dead acute, then e: one é, nothing for the dead key.
    if (compose_available("en_US.UTF-8")) {
        s.server.send_key(KEY_APOSTROPHE, true);
        s.server.send_key(KEY_APOSTROPHE, false);
        s.server.send_key(KEY_E, true);
        s.server.send_key(KEY_E, false);
        events = settle(d);
        const auto* dead = find_event<KeyEvent>(events, [](const KeyEvent& e) {
            return e.pressed && e.key == KEY_APOSTROPHE;
        });
        const auto* e = find_event<KeyEvent>(events, [](const KeyEvent& ev) {
            return ev.pressed && ev.key == KEY_E;
        });
        REQUIRE(dead != nullptr);
        REQUIRE(e != nullptr);
        CHECK(dead->utf8.empty());
        CHECK_EQ(e->utf8, std::string("\xc3\xa9"));
        CHECK_EQ(e->keysym, uint32_t(XKB_KEY_eacute));
    } else {
        bstest::skip_check("compose", "no compose table for en_US.UTF-8 on this machine (libX11 locale data)");
    }

    // --- Text input (entered with the keyboard focus) ------------------------
    CHECK(seat->has_text_input());
    seat->enable_text_input(content_hint::Multiline | content_hint::Spellcheck, ContentPurpose::Email);
    CHECK(seat->text_input_enabled());
    seat->set_text_input_cursor_rect(Rect{10, 20, 2, 16});
    seat->set_surrounding_text("hello", 5, 5);
    settle(d);
    auto st = s.server.app_state();
    CHECK(st.text_input_enabled);
    CHECK_EQ(st.text_input_purpose, uint32_t(ContentPurpose::Email));
    CHECK_EQ(st.text_input_hints, content_hint::Multiline | content_hint::Spellcheck);
    CHECK_EQ(st.text_input_commits, 3);
    s.server.send_text_input_done("ka", 0, 2, "", 0, 0);
    events = settle(d);
    const auto* ti = find_event<TextInputEvent>(events);
    REQUIRE(ti != nullptr);
    CHECK_EQ(ti->preedit, std::string("ka"));
    CHECK_EQ(ti->preedit_cursor_end, 2);
    CHECK(ti->commit.empty());
    CHECK_EQ(ti->surface_id, window->id());
    s.server.send_text_input_done("", -1, -1, "\xe3\x81\x8b", 1, 0);
    events = settle(d);
    ti = find_event<TextInputEvent>(events);
    REQUIRE(ti != nullptr);
    CHECK(ti->preedit.empty());
    CHECK_EQ(ti->commit, std::string("\xe3\x81\x8b"));
    CHECK_EQ(ti->delete_before, 1u);
    seat->disable_text_input();
    settle(d);
    CHECK(!s.server.app_state().text_input_enabled);

    s.server.send_keyboard_leave();
    events = settle(d);
    CHECK(find_event<KeyboardLeaveEvent>(events) != nullptr);
    CHECK_EQ(seat->keyboard_focus(), kNoSurface);

    // --- Pointer ----------------------------------------------------------------
    s.server.send_pointer_enter(10.5, 20.25);
    events = settle(d);
    const auto* penter = find_event<PointerEnterEvent>(events);
    REQUIRE(penter != nullptr);
    CHECK_EQ(penter->surface_id, window->id());
    CHECK_EQ(penter->x, 10.5);
    CHECK_EQ(penter->y, 20.25);
    CHECK_EQ(seat->pointer_focus(), window->id());
    CHECK_EQ(seat->pointer_enter_serial(), penter->serial);
    // The cursor is set on every enter, through cursor-shape.
    st = s.server.app_state();
    CHECK_EQ(st.cursor_shape, uint32_t(CursorShape::Default));
    CHECK_EQ(st.cursor_shape_serial, penter->serial);
    seat->set_cursor(CursorShape::Text);
    CHECK(seat->cursor() == CursorShape::Text);
    settle(d);
    CHECK_EQ(s.server.app_state().cursor_shape, uint32_t(CursorShape::Text));
    seat->set_cursor(CursorShape::Hidden);
    settle(d);
    CHECK_EQ(s.server.app_state().cursor_hidden, 1);
    seat->set_cursor(CursorShape::Pointer);

    s.server.send_pointer_motion(30, 40, 77);
    s.server.send_pointer_button(BTN_LEFT, true);
    events = settle(d);
    const auto* motion = find_event<PointerMotionEvent>(events);
    REQUIRE(motion != nullptr);
    CHECK_EQ(motion->x, 30.0);
    CHECK_EQ(motion->time_ms, 77u);
    const auto* button = find_event<PointerButtonEvent>(events);
    REQUIRE(button != nullptr);
    CHECK_EQ(button->button, BTN_LEFT);
    CHECK(button->pressed);
    CHECK_EQ(seat->last_input_serial(), button->serial);

    // One frame of wheel scrolling: two detents down, 30 px, in one event.
    s.server.send_pointer_scroll(0, 240, 0, 30.0, WL_POINTER_AXIS_SOURCE_WHEEL);
    // A touchpad frame: both axes, no detents.
    s.server.send_pointer_scroll(0, 0, -4.5, 12.0, WL_POINTER_AXIS_SOURCE_FINGER);
    events = settle(d);
    REQUIRE(count<PointerAxisEvent>(events) == 2);
    const auto* wheel = find_event<PointerAxisEvent>(events);
    REQUIRE(wheel != nullptr);
    CHECK_EQ(wheel->v120y, 240);
    CHECK_EQ(wheel->dy, 30.0);
    CHECK_EQ(wheel->dx, 0.0);
    CHECK(wheel->source == AxisSource::Wheel);
    const auto* finger = find_event<PointerAxisEvent>(events, [](const PointerAxisEvent& e) {
        return e.source == AxisSource::Finger;
    });
    REQUIRE(finger != nullptr);
    CHECK_EQ(finger->dx, -4.5);
    CHECK_EQ(finger->dy, 12.0);
    CHECK_EQ(finger->v120y, 0);

    s.server.send_pointer_leave();
    events = settle(d);
    CHECK(find_event<PointerLeaveEvent>(events) != nullptr);
    CHECK_EQ(seat->pointer_focus(), kNoSurface);

    // --- Touch --------------------------------------------------------------------
    s.server.send_seat_caps(WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_TOUCH);
    settle(d);
    CHECK(seat->has_touch());
    s.server.send_touch_down(3, 5, 6);
    s.server.send_touch_motion(3, 7, 8);
    s.server.send_touch_up(3);
    events = settle(d);
    const auto* down = find_event<TouchDownEvent>(events);
    const auto* tmove = find_event<TouchMotionEvent>(events);
    const auto* up = find_event<TouchUpEvent>(events);
    REQUIRE(down != nullptr);
    REQUIRE(tmove != nullptr);
    REQUIRE(up != nullptr);
    CHECK_EQ(down->id, 3);
    CHECK_EQ(down->surface_id, window->id());
    CHECK_EQ(tmove->surface_id, window->id());
    CHECK_EQ(up->surface_id, window->id());
    CHECK_EQ(up->x, 7.0);
    CHECK_EQ(up->y, 8.0);

    // Losing the keyboard capability releases it; the keymap stays readable.
    s.server.send_seat_caps(WL_SEAT_CAPABILITY_POINTER);
    settle(d);
    CHECK(!seat->has_keyboard());
    CHECK(km->keysym(KEY_A) == uint32_t(XKB_KEY_a));
}

// A compositor without cursor-shape: the cursor is the XCursor theme's image
// (libwayland-cursor's built-in set when no theme is installed) on a surface
// of browl's, set on enter and replaced on a change of shape.
void run_xcursor() {
    bstest::FakeSession s(false);
    REQUIRE(s.display != nullptr);
    Display& d = *s.display;
    auto seat = d.default_seat();
    REQUIRE(seat != nullptr);
    auto window = d.create_window(WindowConfig{});
    REQUIRE(window != nullptr);
    d.enable_input();
    settle(d);

    s.server.send_pointer_enter(3, 4);
    settle(d);
    auto st = s.server.app_state();
    CHECK_EQ(st.cursor_shape_serial, 0u);  // no cursor-shape request
    CHECK_EQ(st.cursor_surfaces_set, 1);
    CHECK(st.cursor_buffer_width > 0);
    CHECK(st.cursor_commits >= 1);
    CHECK(st.cursor_hotspot_x >= 0 && st.cursor_hotspot_x < st.cursor_buffer_width);

    seat->set_cursor(CursorShape::Text);
    settle(d);
    st = s.server.app_state();
    CHECK_EQ(st.cursor_surfaces_set, 2);
    CHECK(st.cursor_commits >= 2);

    seat->set_cursor(CursorShape::Hidden);
    settle(d);
    CHECK_EQ(s.server.app_state().cursor_hidden, 1);
}

}  // namespace

int main() {
    run();
    run_xcursor();
    return bstest::finish("test_input");
}
