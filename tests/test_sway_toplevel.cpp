// Foreign toplevel management against a real compositor: a private headless
// sway (run-headless-sway.sh) and a second client in this process with one
// ordinary xdg_toplevel window. browl must see the window sway reports (title,
// app id, output, activation), and requests made through browl must reach the
// window as the compositor's own configure and close events.
#include "browl/browl.h"
#include "sway_harness.h"

using namespace browl;
using swaytest::EventLog;

namespace {

std::shared_ptr<ForeignToplevel> find_by_app_id(ForeignToplevelManager& mgr, const std::string& app_id) {
    for (auto& t : mgr.toplevels()) {
        if (t->app_id() == app_id) return t;
    }
    return nullptr;
}

void run(Display& d) {
    auto mgr = d.foreign_toplevel_manager();
    REQUIRE(mgr != nullptr);
    CHECK(d.roundtrip() >= 0);
    CHECK(mgr->toplevels().empty());
    auto out = d.default_output();
    REQUIRE(out != nullptr);
    EventLog log;

    swaytest::ToplevelClient win;
    std::string error;
    REQUIRE(win.connect(bstest::env("BROWL_TEST_WAYLAND_DISPLAY"), &error));
    REQUIRE(win.map("browl test window", "org.browl.test"));
    auto pump_both = [&](const std::function<bool()>& pred, int ms = 3000) {
        return swaytest::pump(d, pred, ms, [&] { win.roundtrip(); });
    };

    // The window appears, as sway describes it.
    REQUIRE(pump_both([&] { return find_by_app_id(*mgr, "org.browl.test") != nullptr; }));
    auto top = find_by_app_id(*mgr, "org.browl.test");
    CHECK_EQ(top->title(), std::string("browl test window"));
    CHECK(pump_both([&] { return top->is_activated(); }));  // sway focuses a new window
    CHECK(pump_both([&] { return !top->outputs().empty(); }));
    REQUIRE(!top->outputs().empty());
    CHECK_EQ(top->outputs().front(), out->id());
    CHECK(!top->is_fullscreen());
    log.take(d);
    const auto* created = log.find<ToplevelCreatedEvent>(
        [&](const ToplevelCreatedEvent& e) { return e.toplevel.id == top->id(); });
    REQUIRE(created != nullptr);
    CHECK_EQ(created->toplevel.app_id, std::string("org.browl.test"));

    // The window renames itself: one done-delimited update.
    win.set_title("renamed by the client");
    CHECK(pump_both([&] { return top->title() == "renamed by the client"; }));
    log.take(d);
    CHECK(log.find<ToplevelDoneEvent>([&](const ToplevelDoneEvent& e) {
              return e.id == top->id() && e.snapshot.title == "renamed by the client";
          }) != nullptr);

    // browl asks for fullscreen: the window is configured fullscreen at the
    // output's size, and browl sees the state come back.
    top->set_fullscreen(out.get());
    CHECK(pump_both([&] { return win.fullscreen; }));
    CHECK_EQ(win.width, out->current_mode().width);
    CHECK_EQ(win.height, out->current_mode().height);
    CHECK(pump_both([&] { return top->is_fullscreen(); }));
    top->unset_fullscreen();
    CHECK(pump_both([&] { return !win.fullscreen; }));
    CHECK(pump_both([&] { return !top->is_fullscreen(); }));

    // browl asks the window to close: the window gets xdg_toplevel.close.
    CHECK(!win.close_requested);
    top->close();
    CHECK(pump_both([&] { return win.close_requested; }));

    // The window goes away: browl reports it closed and forgets it.
    const ToplevelId id = top->id();
    top.reset();
    win.destroy_window();
    CHECK(pump_both([&] { return mgr->find_toplevel(id) == nullptr; }));
    log.take(d);
    CHECK(log.find<ToplevelClosedEvent>([&](const ToplevelClosedEvent& e) { return e.id == id; }) != nullptr);
}

}  // namespace

int main() {
    auto display = swaytest::connect_or_skip("test_sway_toplevel");
    if (display) run(*display);
    return bstest::finish("test_sway_toplevel");
}
