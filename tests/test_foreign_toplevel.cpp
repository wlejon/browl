// ForeignToplevelManager against the in-process test-double compositor
// (fake_session.h): toplevels announced, updated in done-delimited batches,
// requests sent, and closed. test_sway does the same with real windows.
#include "browl/display.h"
#include "browl/foreign_toplevel.h"
#include "browl/seat.h"
#include "fake_session.h"

using namespace browl;
using bstest::find_event;

namespace {

void run() {
    bstest::FakeSession s;
    REQUIRE(s.display != nullptr);
    Display& display = *s.display;

    auto mgr = display.foreign_toplevel_manager();
    REQUIRE(mgr != nullptr);
    CHECK(display.foreign_toplevel_manager() == mgr);  // one per display
    CHECK(mgr->zwlr_manager_ptr() != nullptr);
    CHECK(mgr->toplevels().empty());

    // The fake takes a browl::toplevel_state mask and sends the wire enum.
    auto* comp_top = s.server.create_foreign_toplevel("Terminal", "org.bro.terminal", toplevel_state::Activated);
    REQUIRE(comp_top != nullptr);
    CHECK(display.roundtrip() >= 0);

    auto tops = mgr->toplevels();
    REQUIRE(tops.size() == 1);
    auto top = tops.front();
    CHECK(mgr->find_toplevel(top->id()) == top);
    CHECK_EQ(top->title(), std::string("Terminal"));
    CHECK_EQ(top->app_id(), std::string("org.bro.terminal"));
    CHECK(top->is_activated());
    CHECK(!top->is_maximized());
    CHECK(!top->is_minimized());
    CHECK(!top->is_fullscreen());
    const auto snaps = mgr->snapshots();
    REQUIRE(snaps.size() == 1);
    CHECK_EQ(snaps.front().app_id, std::string("org.bro.terminal"));

    auto events = display.events().drain();
    const auto* created = find_event<ToplevelCreatedEvent>(events);
    REQUIRE(created != nullptr);
    CHECK_EQ(created->toplevel.id, top->id());
    CHECK_EQ(created->toplevel.title, std::string("Terminal"));

    // Changes apply at done, as one snapshot.
    s.server.update_toplevel_title(comp_top, "Terminal - vim");
    s.server.update_toplevel_state(comp_top, toplevel_state::Maximized | toplevel_state::Activated);
    CHECK(display.roundtrip() >= 0);
    CHECK_EQ(top->title(), std::string("Terminal - vim"));
    CHECK(top->is_maximized());
    CHECK(top->is_activated());
    events = display.events().drain();
    const auto* done = find_event<ToplevelDoneEvent>(events);
    REQUIRE(done != nullptr);
    CHECK_EQ(done->snapshot.title, std::string("Terminal - vim"));
    CHECK(find_event<ToplevelTitleEvent>(events) != nullptr);
    CHECK(find_event<ToplevelCreatedEvent>(events) == nullptr);

    auto seat = display.default_seat();
    REQUIRE(seat != nullptr);
    top->activate(*seat);
    top->set_minimized();
    top->close();
    CHECK(display.roundtrip() >= 0);
    CHECK(comp_top->activated);
    CHECK(comp_top->minimized);
    CHECK(comp_top->closed);

    s.server.close_toplevel(comp_top);
    CHECK(display.roundtrip() >= 0);
    CHECK(mgr->toplevels().empty());
    CHECK(mgr->snapshots().empty());
    events = display.events().drain();
    CHECK(find_event<ToplevelClosedEvent>(events, [&](const ToplevelClosedEvent& e) {
              return e.id == top->id();
          }) != nullptr);
}

}  // namespace

int main() {
    run();
    return bstest::finish("test_foreign_toplevel");
}
