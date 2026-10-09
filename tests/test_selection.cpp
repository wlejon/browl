// Selections between two browl clients of the in-process test double
// (fake_session.h), which forwards them as a compositor does: the clipboard
// and the primary selection set by one client, announced to the other, read
// through a pipe the owner writes (off its dispatch thread, so a payload far
// larger than a pipe's buffer gets through), answered from memory for the
// owner itself, taken over (the old owner's source cancelled) and cleared;
// and a drag dropped on a window, at the position of its last motion; and a
// drag out of a window (start_drag), from the compositor's side.
#include "browl/browl.h"
#include "fake_session.h"

#include <unistd.h>

#include <atomic>
#include <thread>

using namespace browl;
using bstest::find_event;

namespace {

const std::string kText = "text/plain;charset=utf-8";

std::vector<ShellEvent> settle(Display& d) {
    d.roundtrip();
    d.roundtrip();
    return d.events().drain();
}

std::string as_string(const std::optional<std::vector<uint8_t>>& bytes) {
    return bytes ? std::string(bytes->begin(), bytes->end()) : std::string("<none>");
}

// Dispatches `d` on a thread of its own while alive: the owner of a
// selection must answer send requests while the reader waits.
class Pump {
public:
    explicit Pump(Display& d) : thread_([this, &d] {
        while (!stop_) {
            if (d.roundtrip() < 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }) {}
    ~Pump() {
        stop_ = true;
        thread_.join();
    }

private:
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

void drag_out(bstest::FakeSession& s, Display& b, Seat& seat, Window& win);

void run() {
    bstest::FakeSession s;
    REQUIRE(s.display != nullptr);
    Display& a = *s.display;
    a.enable_input();
    auto win_a = a.create_window(WindowConfig{});
    REQUIRE(win_a != nullptr);
    settle(a);
    s.server.send_keyboard_enter();  // A's keyboard, A's window: A has a serial
    settle(a);

    std::string error;
    auto b_ptr = Display::connect_to_fd(s.server.create_client_fd(), &error);
    REQUIRE(b_ptr != nullptr);
    Display& b = *b_ptr;
    b.enable_input();
    auto win_b = b.create_window(WindowConfig{});
    REQUIRE(win_b != nullptr);
    settle(b);

    auto seat_a = a.default_seat();
    auto seat_b = b.default_seat();
    REQUIRE(seat_a != nullptr);
    REQUIRE(seat_b != nullptr);
    CHECK(seat_a->has_selection_protocol(Selection::Clipboard));
    CHECK(seat_a->has_selection_protocol(Selection::Primary));
    CHECK(seat_a->last_input_serial() != 0);
    // No serial yet: B cannot set a selection.
    CHECK_EQ(seat_b->last_input_serial(), 0u);
    CHECK(!seat_b->set_selection(Selection::Clipboard, text_selection("x")));

    // A copies.
    CHECK(seat_a->set_selection(Selection::Clipboard, text_selection("hello browl")));
    auto events = settle(a);
    const auto* mine = find_event<SelectionChangedEvent>(events);
    REQUIRE(mine != nullptr);
    CHECK(mine->owned);
    CHECK(mine->selection == Selection::Clipboard);
    CHECK_EQ(mine->mime_types.front(), kText);
    CHECK(seat_a->owns_selection(Selection::Clipboard));
    CHECK_EQ(as_string(seat_a->read_selection(Selection::Clipboard, kText)), std::string("hello browl"));
    CHECK(!seat_a->read_selection(Selection::Clipboard, "image/png"));
    CHECK_EQ(seat_a->selection_mime_types(Selection::Clipboard).size(), size_t(5));

    // B sees it and reads it from A.
    events = settle(b);
    const auto* theirs = find_event<SelectionChangedEvent>(events);
    REQUIRE(theirs != nullptr);
    CHECK(!theirs->owned);
    CHECK_EQ(theirs->mime_types.size(), size_t(5));
    CHECK(!seat_b->owns_selection(Selection::Clipboard));
    {
        Pump pump(a);
        CHECK_EQ(as_string(seat_b->read_selection(Selection::Clipboard, kText)), std::string("hello browl"));
        CHECK_EQ(as_string(seat_b->read_selection(Selection::Clipboard, "UTF8_STRING")), std::string("hello browl"));
        CHECK(!seat_b->read_selection(Selection::Clipboard, "image/png"));
    }

    // A big selection: 4 MiB through a 64 KiB pipe, while A keeps dispatching.
    std::vector<uint8_t> big(4u << 20);
    for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<uint8_t>(i * 7);
    CHECK(seat_a->set_selection(Selection::Clipboard, {{"application/octet-stream", big}}));
    settle(a);
    settle(b);
    {
        Pump pump(a);
        auto got = seat_b->read_selection(Selection::Clipboard, "application/octet-stream",
                                          std::chrono::milliseconds(5000));
        REQUIRE(got.has_value());
        CHECK(*got == big);
    }
    // The first source was replaced by A's own second one: still A's.
    settle(a);
    CHECK(seat_a->owns_selection(Selection::Clipboard));

    // Primary selection, the same way.
    CHECK(seat_a->set_selection(Selection::Primary, text_selection("middle click")));
    settle(a);
    events = settle(b);
    const auto* prim = find_event<SelectionChangedEvent>(events, [](const SelectionChangedEvent& e) {
        return e.selection == Selection::Primary;
    });
    REQUIRE(prim != nullptr);
    CHECK(!prim->owned);
    {
        Pump pump(a);
        CHECK_EQ(as_string(seat_b->read_selection(Selection::Primary, kText)), std::string("middle click"));
    }
    CHECK_EQ(as_string(seat_a->read_selection(Selection::Primary, kText)), std::string("middle click"));

    // B gets a serial (keyboard focus) and takes the clipboard over: A's
    // source is cancelled, and A now reads B's.
    s.server.send_keyboard_enter();  // B's keyboard, B's window
    settle(b);
    REQUIRE(seat_b->last_input_serial() != 0);
    CHECK(seat_b->set_selection(Selection::Clipboard, text_selection("from b")));
    settle(b);
    events = settle(a);
    CHECK(!seat_a->owns_selection(Selection::Clipboard));
    CHECK(find_event<SelectionChangedEvent>(events, [](const SelectionChangedEvent& e) {
              return e.selection == Selection::Clipboard && !e.owned && !e.mime_types.empty();
          }) != nullptr);
    {
        Pump pump(b);
        CHECK_EQ(as_string(seat_a->read_selection(Selection::Clipboard, kText)), std::string("from b"));
    }

    // B clears it: A hears of an empty selection.
    seat_b->clear_selection(Selection::Clipboard);
    CHECK(!seat_b->owns_selection(Selection::Clipboard));
    settle(b);
    events = settle(a);
    CHECK(find_event<SelectionChangedEvent>(events, [](const SelectionChangedEvent& e) {
              return e.selection == Selection::Clipboard && e.mime_types.empty();
          }) != nullptr);
    CHECK(seat_a->selection_mime_types(Selection::Clipboard).empty());
    CHECK(!seat_a->read_selection(Selection::Clipboard, kText));

    // Drag mime preference is settable; there is no drag here to accept.
    seat_b->set_drag_mime_types({"text/uri-list"});
    CHECK(!seat_b->read_drop("text/uri-list"));
    seat_b->finish_drop();

    // A drag into B's window: enter, a motion, the drop. wl_data_device.drop
    // carries no position, so the drop is where the last motion left it.
    s.server.send_drag({"text/uri-list", kText}, 5, 6, 40.5, 60.25);
    events = settle(b);
    const auto* enter = find_event<DragEnterEvent>(events);
    REQUIRE(enter != nullptr);
    CHECK_EQ(enter->surface_id, win_b->id());
    CHECK_EQ(enter->x, 5.0);
    const auto* dmotion = find_event<DragMotionEvent>(events);
    REQUIRE(dmotion != nullptr);
    CHECK_EQ(dmotion->surface_id, win_b->id());
    CHECK_EQ(dmotion->x, 40.5);
    const auto* drop = find_event<DragDropEvent>(events);
    REQUIRE(drop != nullptr);
    CHECK_EQ(drop->surface_id, win_b->id());
    CHECK_EQ(drop->x, 40.5);
    CHECK_EQ(drop->y, 60.25);
    CHECK_EQ(drop->mime_types.size(), size_t(2));
    seat_b->finish_drop();

    drag_out(s, b, *seat_b, *win_b);
}

// A drag out of a window: refused without a press; started with the press's
// serial, the source's types and actions, and an icon committed after it
// with its hotspot under the pointer; the compositor reads it through the
// source; target / action / drop / finish come back as DragSourceEvents, and
// the finish (or a cancel) ends it, source and icon gone.
void drag_out(bstest::FakeSession& s, Display& b, Seat& seat, Window& win) {
    constexpr uint32_t kBtnLeft = 0x110;
    CHECK(!seat.dragging());
    CHECK(!seat.start_drag(win.wl_surface_ptr(), text_selection("dragged")));  // no press yet

    s.server.send_pointer_enter(10, 10);
    s.server.send_pointer_button(kBtnLeft, true);
    settle(b);
    const uint32_t press = s.server.last_serial();

    Seat::DragIcon icon;
    icon.width = 8;
    icon.height = 4;
    icon.pixels.assign(8 * 4 * 4, 0);
    icon.pixels[0] = 0x11;  // B
    icon.pixels[1] = 0x22;  // G
    icon.pixels[2] = 0x33;  // R
    icon.pixels[3] = 0xff;  // A
    icon.hotspot_x = 3;
    icon.hotspot_y = 2;
    SelectionContents contents = text_selection("dragged");
    contents.emplace_back("text/uri-list", std::vector<uint8_t>{'f', 'i', 'l', 'e', ':', '/', '/', '/', 'x'});
    CHECK(seat.start_drag(win.wl_surface_ptr(), contents, &icon, dnd_action::Copy | dnd_action::Move));
    CHECK(seat.dragging());
    settle(b);
    auto st = s.server.app_state();
    CHECK_EQ(st.drag_starts, 1);
    CHECK_EQ(st.drag_serial, press);
    CHECK(st.drag_origin_is_window);
    CHECK_EQ(st.drag_mimes.size(), size_t(6));
    CHECK_EQ(st.drag_mimes.back(), std::string("text/uri-list"));
    CHECK_EQ(st.drag_source_actions, dnd_action::Copy | dnd_action::Move);
    CHECK(st.drag_has_icon);
    CHECK_EQ(st.drag_icon_width, 8);
    CHECK_EQ(st.drag_icon_first_pixel, 0xff332211u);
    CHECK_EQ(st.drag_icon_offset_x, -3);
    CHECK_EQ(st.drag_icon_offset_y, -2);
    CHECK_EQ(st.drag_icon_commits, 1);

    // The target reads it while the drag is under way.
    {
        const int fd = s.server.drag_source_receive("text/uri-list");
        REQUIRE(fd >= 0);
        settle(b);
        std::string got;
        char buf[64];
        for (ssize_t n; (n = read(fd, buf, sizeof(buf))) > 0;) got.append(buf, static_cast<size_t>(n));
        close(fd);
        CHECK_EQ(got, std::string("file:///x"));
    }

    s.server.drag_source_target("text/uri-list");
    s.server.drag_source_action(dnd_action::Copy);
    s.server.drag_source_dropped();
    auto events = settle(b);
    const auto* target = find_event<DragSourceEvent>(events, [](const DragSourceEvent& e) {
        return e.kind == DragSourceEvent::Kind::Target;
    });
    REQUIRE(target != nullptr);
    CHECK_EQ(target->mime_type, std::string("text/uri-list"));
    const auto* action = find_event<DragSourceEvent>(events, [](const DragSourceEvent& e) {
        return e.kind == DragSourceEvent::Kind::Action;
    });
    REQUIRE(action != nullptr);
    CHECK_EQ(action->action, dnd_action::Copy);
    CHECK(find_event<DragSourceEvent>(events, [](const DragSourceEvent& e) {
              return e.kind == DragSourceEvent::Kind::Dropped;
          }) != nullptr);
    CHECK(seat.dragging());  // still: the target has not finished

    s.server.drag_source_finished();
    events = settle(b);
    const auto* finished = find_event<DragSourceEvent>(events, [](const DragSourceEvent& e) {
        return e.kind == DragSourceEvent::Kind::Finished;
    });
    REQUIRE(finished != nullptr);
    CHECK_EQ(finished->action, dnd_action::Copy);
    CHECK(!seat.dragging());
    CHECK(!s.server.drag_source_alive());
    CHECK_EQ(s.server.app_state().drag_icons_destroyed, 1);

    // A second drag, without an icon, cancelled by the compositor.
    CHECK(seat.start_drag(win.wl_surface_ptr(), text_selection("again")));
    settle(b);
    CHECK_EQ(s.server.app_state().drag_starts, 2);
    CHECK(!s.server.app_state().drag_has_icon);
    s.server.drag_source_cancelled();
    events = settle(b);
    CHECK(find_event<DragSourceEvent>(events, [](const DragSourceEvent& e) {
              return e.kind == DragSourceEvent::Kind::Cancelled;
          }) != nullptr);
    CHECK(!seat.dragging());
    CHECK(!s.server.drag_source_alive());

    // A third, withdrawn by the client.
    CHECK(seat.start_drag(win.wl_surface_ptr(), text_selection("withdrawn")));
    seat.cancel_drag();
    events = settle(b);
    CHECK(find_event<DragSourceEvent>(events, [](const DragSourceEvent& e) {
              return e.kind == DragSourceEvent::Kind::Cancelled;
          }) != nullptr);
    CHECK(!seat.dragging());
    CHECK(!s.server.drag_source_alive());
}

}  // namespace

int main() {
    run();
    return bstest::finish("test_selection");
}
