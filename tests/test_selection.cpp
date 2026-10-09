// Selections between two browl clients of the in-process test double
// (fake_session.h), which forwards them as a compositor does: the clipboard
// and the primary selection set by one client, announced to the other, read
// through a pipe the owner writes (off its dispatch thread, so a payload far
// larger than a pipe's buffer gets through), answered from memory for the
// owner itself, taken over (the old owner's source cancelled) and cleared;
// and a drag dropped on a window, at the position of its last motion.
#include "browl/browl.h"
#include "fake_session.h"

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
}

}  // namespace

int main() {
    run();
    return bstest::finish("test_selection");
}
