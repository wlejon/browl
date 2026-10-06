// Positioner and Popup against the in-process test-double compositor
// (fake_session.h): creation on a layer surface, configure, reposition and
// dismissal. Positioning by a real compositor is checked in test_sway.
#include "browl/display.h"
#include "browl/layer_surface.h"
#include "browl/popup.h"
#include "fake_session.h"

using namespace browl;
using bstest::find_event;

namespace {

void run() {
    bstest::FakeSession s;
    REQUIRE(s.display != nullptr);
    Display& display = *s.display;

    LayerSurfaceConfig config;
    config.name_space = "panel";
    config.size = Size{1920, 48};
    auto layer = display.create_layer_surface(config);
    REQUIRE(layer != nullptr);

    auto positioner = display.create_positioner();
    REQUIRE(positioner != nullptr);
    positioner->set_size(200, 300);
    positioner->set_anchor_rect(100, 48, 50, 0);
    positioner->set_anchor(PositionerAnchor::BottomLeft);
    positioner->set_gravity(Gravity::BottomRight);
    positioner->set_constraint_adjustment(constraint_adjustment::SlideX | constraint_adjustment::FlipY);
    positioner->set_offset(0, 5);

    auto popup = layer->create_popup(*positioner);
    REQUIRE(popup != nullptr);
    CHECK(popup->wl_surface_ptr() != nullptr);
    CHECK(popup->xdg_surface_ptr() != nullptr);
    CHECK(popup->xdg_popup_ptr() != nullptr);
    CHECK(popup->id() != layer->id());
    CHECK(display.roundtrip() >= 0);
    CHECK(s.server.popup_created());

    auto snap = popup->snapshot();
    CHECK(!snap.configured);
    CHECK(!snap.dismissed);

    s.server.configure_popup(100, 53, 200, 300);
    CHECK(display.roundtrip() >= 0);
    snap = popup->snapshot();
    CHECK(snap.configured);
    CHECK(snap.geometry == (Rect{100, 53, 200, 300}));
    auto events = display.events().drain();
    const auto* conf = find_event<PopupConfigureEvent>(
        events, [&](const PopupConfigureEvent& e) { return e.surface_id == popup->id(); });
    REQUIRE(conf != nullptr);
    CHECK_EQ(conf->x, 100);
    CHECK_EQ(conf->y, 53);
    CHECK_EQ(conf->width, 200);
    CHECK_EQ(conf->height, 300);

    popup->reposition(*positioner, 1234);
    CHECK(display.roundtrip() >= 0);
    CHECK(s.server.popup_repositioned_received());
    events = display.events().drain();
    CHECK(find_event<PopupRepositionedEvent>(events, [&](const PopupRepositionedEvent& e) {
              return e.surface_id == popup->id() && e.token == 1234u;
          }) != nullptr);
    CHECK_EQ(popup->snapshot().repositioned_token, 1234u);

    s.server.send_popup_done();
    CHECK(display.roundtrip() >= 0);
    CHECK(popup->snapshot().dismissed);
    events = display.events().drain();
    CHECK(find_event<PopupDoneEvent>(events, [&](const PopupDoneEvent& e) {
              return e.surface_id == popup->id();
          }) != nullptr);
}

}  // namespace

int main() {
    run();
    return bstest::finish("test_popup");
}
