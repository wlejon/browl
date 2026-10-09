// Application windows against a real compositor: a private headless sway
// (run-headless-sway.sh), two browl clients and a virtual keyboard (the
// headless seat has no input devices otherwise). Each window is mapped with
// the size sway configures and the decorations it asked for (server-side
// for one, client-side for the borderless other); keyboard focus follows
// the newest window and keys arrive interpreted through sway's keymap; the
// clipboard and the primary selection go from one client to the other;
// presentation feedback comes back for a commit; an activation token from
// one client raises the other's window.
#include "browl/browl.h"
#include "sway_harness.h"

#include <algorithm>
#include <atomic>
#include <thread>
#include <xkbcommon/xkbcommon-keysyms.h>

using namespace browl;
using swaytest::EventLog;

namespace {

const std::string kText = "text/plain;charset=utf-8";
constexpr uint32_t KEY_A = 30;

struct Client {
    std::unique_ptr<Display> display;
    std::unique_ptr<Window> window;
    std::shared_ptr<ShmPool> pool;
    std::shared_ptr<ShmBuffer> buffer;
    EventLog log;
};

// Dispatches a client on its own thread while alive (an owner of a
// selection must answer while the other client reads it).
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

// Creates the window, waits for its first configure, and maps it with a
// buffer of the configured size. Returns the first configure's snapshot.
bool map_window(Client& c, const WindowConfig& config, WindowSnapshot* first) {
    c.window = c.display->create_window(config);
    if (!c.window) return false;
    const SurfaceId id = c.window->id();
    if (!swaytest::pump(*c.display, [&] {
            c.log.take(*c.display);
            return c.log.find<WindowConfigureEvent>([&](const auto& e) { return e.surface_id == id; }) != nullptr;
        })) {
        return false;
    }
    const auto* conf = c.log.find<WindowConfigureEvent>([&](const auto& e) { return e.surface_id == id; });
    *first = conf->snapshot;
    const int32_t w = conf->snapshot.configured_size.width > 0 ? conf->snapshot.configured_size.width : 320;
    const int32_t h = conf->snapshot.configured_size.height > 0 ? conf->snapshot.configured_size.height : 240;
    c.pool = c.display->create_shm_pool(size_t(w) * size_t(h) * 4);
    if (!c.pool) return false;
    c.buffer = swaytest::solid_buffer(*c.pool, w, h, 0xff3060a0);
    c.window->attach_buffer(c.buffer->wl_buffer_ptr());
    c.window->damage(0, 0, w, h);
    return swaytest::commit_and_wait_frame(*c.display, c.window->wl_surface_ptr());
}

std::string as_string(const std::optional<std::vector<uint8_t>>& bytes) {
    return bytes ? std::string(bytes->begin(), bytes->end()) : std::string("<none>");
}

void run() {
    const std::string name = bstest::env("BROWL_TEST_WAYLAND_DISPLAY");
    Client a;
    a.display = swaytest::connect_or_skip("test_sway_window");
    REQUIRE(a.display != nullptr);
    Display& da = *a.display;
    CHECK(da.has_xdg_shell());
    CHECK(da.has_data_device());
    CHECK(da.has_viewporter());
    CHECK(da.has_xdg_output());

    swaytest::VirtualKeyboard vk;
    std::string error;
    if (!vk.connect(name, &error)) {
        bstest::skip("test_sway_window", "no virtual keyboard for the headless seat: " + error);
    }
    da.enable_input();
    auto seat_a = da.default_seat();
    REQUIRE(seat_a != nullptr);
    CHECK(swaytest::pump(da, [&] { return seat_a->has_keyboard(); }));

    // Output geometry in the compositor's logical space.
    auto out = da.default_output();
    REQUIRE(out != nullptr);
    CHECK(out->snapshot().logical == (Rect{0, 0, 640, 480}));

    // Window A: server-side decorations.
    WindowConfig ca;
    ca.title = "browl window A";
    ca.app_id = "org.browl.a";
    WindowSnapshot first_a;
    REQUIRE(map_window(a, ca, &first_a));
    CHECK(first_a.configured);
    if (da.has_decoration_manager()) {
        CHECK(first_a.decoration == DecorationMode::ServerSide);
    } else {
        bstest::skip_check("decoration", "this sway offers no xdg-decoration");
    }
    // Mapped (sway's first configure is 0x0, "choose"): tiled to the output,
    // activated, on the output, with keyboard focus and the keymap.
    CHECK(swaytest::pump(da, [&] {
        a.log.take(da);
        const auto snap = a.window->snapshot();
        return snap.configured_size.width > 0 && (snap.states & window_state::Activated) &&
               !snap.outputs.empty() && seat_a->keyboard_focus() == a.window->id() && seat_a->keymap() != nullptr;
    }));
    const auto snap_a = a.window->snapshot();
    CHECK(snap_a.configured_size.width > 0 && snap_a.configured_size.width <= 640);
    CHECK(snap_a.configured_size.height > 0 && snap_a.configured_size.height <= 480);
    CHECK(snap_a.outputs == std::vector<OutputId>{out->id()});
    CHECK_EQ(snap_a.scale120, 120u);

    // A key typed on the virtual keyboard arrives as an 'a'.
    vk.key(KEY_A, true);
    vk.key(KEY_A, false);
    CHECK(swaytest::pump(da, [&] {
        a.log.take(da);
        return a.log.find<KeyEvent>([](const KeyEvent& e) { return e.pressed && e.key == KEY_A; }) != nullptr;
    }));
    const auto* key = a.log.find<KeyEvent>([](const KeyEvent& e) { return e.pressed && e.key == KEY_A; });
    if (key) {
        CHECK_EQ(key->keysym, uint32_t(XKB_KEY_a));
        CHECK_EQ(key->utf8, std::string("a"));
        CHECK_EQ(key->surface_id, a.window->id());
    }

    // Presentation feedback for a commit of A.
    if (da.has_presentation()) {
        const RequestId fb = da.request_presentation_feedback(a.window->wl_surface_ptr());
        CHECK(fb != 0);
        a.window->attach_buffer(a.buffer->wl_buffer_ptr());
        a.window->damage(0, 0, 16, 16);
        a.window->commit();
        CHECK(swaytest::pump(da, [&] {
            a.log.take(da);
            return a.log.find<PresentationFeedbackEvent>([&](const auto& e) { return e.request == fb; }) != nullptr;
        }));
        const auto* pf = a.log.find<PresentationFeedbackEvent>([&](const auto& e) { return e.request == fb; });
        if (pf && pf->presented) {
            CHECK(pf->time_ns > 0);
            CHECK_EQ(pf->surface_id, a.window->id());
        }
    } else {
        bstest::skip_check("presentation", "this sway offers no wp_presentation");
    }

    // A copies (clipboard and primary) with its keyboard serial.
    REQUIRE(seat_a->last_input_serial() != 0);
    CHECK(seat_a->set_selection(Selection::Clipboard, text_selection("browl clipboard")));
    if (seat_a->has_selection_protocol(Selection::Primary)) {
        CHECK(seat_a->set_selection(Selection::Primary, text_selection("browl primary")));
    }
    da.roundtrip();

    // Client B, borderless: asks for client-side decorations; takes focus
    // and with it the selection.
    Client b;
    b.display = Display::connect(name, &error);
    REQUIRE(b.display != nullptr);
    Display& db = *b.display;
    db.enable_input();
    auto seat_b = db.default_seat();
    REQUIRE(seat_b != nullptr);
    WindowConfig cb;
    cb.title = "browl window B";
    cb.app_id = "org.browl.b";
    cb.server_side_decorations = false;
    // Fixed size: sway floats it, and honours a client-side request only for
    // a floating window (a tiled one always gets server-side decorations).
    cb.min_size = {320, 240};
    cb.max_size = {320, 240};
    WindowSnapshot first_b;
    REQUIRE(map_window(b, cb, &first_b));
    if (db.has_decoration_manager()) {
        CHECK(first_b.decoration == DecorationMode::ClientSide);
    }
    CHECK(swaytest::pump(db, [&] {
        b.log.take(db);
        return seat_b->keyboard_focus() == b.window->id() &&
               !seat_b->selection_mime_types(Selection::Clipboard).empty();
    }, 3000, [&] { da.roundtrip(); }));
    const auto mimes = seat_b->selection_mime_types(Selection::Clipboard);
    CHECK(std::find(mimes.begin(), mimes.end(), kText) != mimes.end());
    CHECK(!seat_b->owns_selection(Selection::Clipboard));
    {
        Pump pump(da);
        CHECK_EQ(as_string(seat_b->read_selection(Selection::Clipboard, kText)), std::string("browl clipboard"));
        if (seat_b->has_selection_protocol(Selection::Primary)) {
            CHECK(swaytest::pump(db, [&] { return !seat_b->selection_mime_types(Selection::Primary).empty(); }));
            CHECK_EQ(as_string(seat_b->read_selection(Selection::Primary, kText)), std::string("browl primary"));
        }
    }

    // B copies back; A loses ownership and reads B's.
    CHECK(seat_b->set_selection(Selection::Clipboard, text_selection("from B")));
    db.roundtrip();
    CHECK(swaytest::pump(da, [&] { return !seat_a->owns_selection(Selection::Clipboard); }, 3000,
                         [&] { db.roundtrip(); }));
    // sway sends the selection to the focused client only: A gets B's offer
    // when it is focused again, which the activation below does.

    // Activation: B (focused, with a serial) asks for a token and A uses it
    // to raise its window; A gets the keyboard back.
    if (da.has_activation() && db.has_activation()) {
        const RequestId req = db.request_activation_token("org.browl.a", b.window->wl_surface_ptr(), seat_b.get(),
                                                          seat_b->last_input_serial());
        CHECK(req != 0);
        CHECK(swaytest::pump(db, [&] {
            b.log.take(db);
            return b.log.find<ActivationTokenEvent>([&](const auto& e) { return e.request == req; }) != nullptr;
        }));
        const auto* tok = b.log.find<ActivationTokenEvent>([&](const auto& e) { return e.request == req; });
        REQUIRE(tok != nullptr);
        CHECK(!tok->token.empty());
        CHECK(da.activate(tok->token, a.window->wl_surface_ptr()));
        CHECK(swaytest::pump(da, [&] { return seat_a->keyboard_focus() == a.window->id(); }, 3000,
                             [&] { db.roundtrip(); }));
        CHECK(swaytest::pump(da, [&] {
            const auto m = seat_a->selection_mime_types(Selection::Clipboard);
            return std::find(m.begin(), m.end(), kText) != m.end();
        }, 3000, [&] { db.roundtrip(); }));
        Pump pump(db);
        CHECK_EQ(as_string(seat_a->read_selection(Selection::Clipboard, kText)), std::string("from B"));
    } else {
        bstest::skip_check("activation", "this sway offers no xdg-activation");
    }

    // Unmap and map again: a new first configure for the same surface.
    const SurfaceId id_a = a.window->id();
    a.window->unmap();
    da.roundtrip();
    a.log.all.clear();
    a.window->map();
    CHECK(swaytest::pump(da, [&] {
        a.log.take(da);
        return a.log.find<WindowConfigureEvent>([&](const auto& e) { return e.surface_id == id_a; }) != nullptr;
    }));
}

}  // namespace

int main() {
    run();
    return bstest::finish("test_sway_window");
}
