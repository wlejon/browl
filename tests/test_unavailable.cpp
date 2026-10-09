// Off Linux: browl builds, and says why it cannot connect rather than
// pretending. Windows and macOS only.
#include "browl/browl.h"
#include "check.h"

#include <string>

using namespace browl;

int main() {
    const std::string reason = unavailable_reason();
    std::printf("unavailable: %s\n", reason.c_str());
    CHECK(!reason.empty());

    std::string error;
    CHECK(Display::connect("", &error) == nullptr);
    CHECK_EQ(error, reason);
    error.clear();
    CHECK(Display::connect("wayland-0", &error) == nullptr);
    CHECK_EQ(error, reason);
    error.clear();
    CHECK(Display::connect_to_fd(3, &error) == nullptr);
    CHECK_EQ(error, reason);
    CHECK(Display::connect() == nullptr);  // no error out-parameter: still nullptr

    // Code written against the whole API links and stays inert.
    CHECK(ShmPool::create(nullptr, 4096) == nullptr);

    // The application-window half: every call links and does nothing.
    CHECK(Keymap::from_string("xkb_keymap {};") == nullptr);
    CHECK(Keymap::keysym_to_utf32(0x61) == 0);
    CHECK(Keymap::keysym_name(0x61).empty());
    {
        WindowConfig config;
        config.title = "stub";
        Window window(1, nullptr, nullptr, nullptr, config, nullptr);
        window.map();
        window.unmap();
        CHECK(!window.mapped());
        window.set_title("t");
        window.set_app_id("a");
        window.set_min_size(1, 1);
        window.set_max_size(2, 2);
        window.set_maximized(true);
        window.set_fullscreen(true);
        window.set_minimized();
        window.set_server_side_decorations(false);
        const uint8_t pixel[4] = {};
        CHECK(!window.set_icon(1, 1, pixel));
        CHECK(!window.has_icon_protocol());
        CHECK(!window.set_logical_size(10, 10));
        window.set_buffer_scale(2);
        window.set_window_geometry(Rect{0, 0, 1, 1});
        window.attach_buffer(nullptr);
        window.damage(0, 0, 1, 1);
        window.commit();
        (void)window.snapshot();

        Seat seat(1, nullptr, nullptr);
        window.start_move(seat, 0);
        window.start_resize(seat, 0, ResizeEdge::BottomRight);
        window.show_window_menu(seat, 0, 0, 0);
        CHECK(seat.keymap() == nullptr);
        (void)seat.modifiers();
        (void)seat.repeat_rate();
        (void)seat.repeat_delay_ms();
        (void)seat.last_input_serial();
        (void)seat.pointer_enter_serial();
        (void)seat.pointer_focus();
        (void)seat.keyboard_focus();
        seat.set_cursor(CursorShape::Hidden);
        (void)seat.cursor();
        CHECK(!seat.lock_pointer(nullptr));
        seat.unlock_pointer();
        CHECK(!seat.pointer_locked());
        seat.warp_pointer(nullptr, 0, 0);
        CHECK(!seat.set_selection(Selection::Clipboard, text_selection("x")));
        seat.clear_selection(Selection::Primary);
        CHECK(seat.selection_mime_types(Selection::Clipboard).empty());
        CHECK(!seat.owns_selection(Selection::Clipboard));
        CHECK(!seat.has_selection_protocol(Selection::Clipboard));
        CHECK(!seat.read_selection(Selection::Clipboard, "text/plain").has_value());
        seat.set_drag_mime_types({"text/plain"});
        CHECK(!seat.read_drop("text/plain").has_value());
        seat.finish_drop();
        CHECK(!seat.has_text_input());
        seat.enable_text_input();
        seat.disable_text_input();
        CHECK(!seat.text_input_enabled());
        seat.set_text_input_cursor_rect(Rect{0, 0, 1, 1});
        seat.set_surrounding_text("", 0, 0);
    }
    return bstest::finish("test_unavailable");
}
