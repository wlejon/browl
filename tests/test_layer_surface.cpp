// LayerSurface against the in-process test-double compositor
// (fake_session.h): configure/ack/commit, reconfiguration, and the closed
// event. What a real compositor makes of the requests is test_sway's part.
#include "browl/display.h"
#include "browl/layer_surface.h"
#include "browl/output.h"
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
    config.layer = Layer::Top;
    config.anchor = Anchor::Top | Anchor::Left | Anchor::Right;
    config.size = Size{1920, 48};
    config.exclusive_zone = 48;
    config.output = display.default_output().get();

    auto layer = display.create_layer_surface(config);
    REQUIRE(layer != nullptr);
    CHECK(layer->wl_surface_ptr() != nullptr);
    CHECK(layer->zwlr_layer_surface_ptr() != nullptr);
    CHECK(display.roundtrip() >= 0);
    CHECK(s.server.layer_surface_created());

    auto snap = layer->snapshot();
    CHECK_EQ(snap.id, layer->id());
    CHECK_EQ(snap.layer, Layer::Top);
    CHECK_EQ(snap.anchor, Anchor::Top | Anchor::Left | Anchor::Right);
    CHECK_EQ(snap.exclusive_zone, 48);
    CHECK(!snap.closed);

    s.server.configure_layer_surface(1920, 48);
    CHECK(display.roundtrip() >= 0);
    auto events = display.events().drain();
    const auto* conf = find_event<LayerConfigureEvent>(
        events, [&](const LayerConfigureEvent& e) { return e.surface_id == layer->id(); });
    REQUIRE(conf != nullptr);
    CHECK_EQ(conf->width, 1920u);
    CHECK_EQ(conf->height, 48u);
    CHECK(conf->serial != 0u);

    snap = layer->snapshot();
    CHECK_EQ(snap.configured_size.width, 1920);
    CHECK_EQ(snap.configured_size.height, 48);
    CHECK_EQ(snap.configured_serial, conf->serial);

    layer->ack_configure(conf->serial);
    layer->commit();
    CHECK(display.roundtrip() >= 0);
    CHECK_EQ(s.server.last_layer_ack_serial(), conf->serial);
    CHECK(s.server.layer_surface_committed());

    layer->set_size(1920, 60);
    layer->set_anchor(Anchor::Bottom | Anchor::Left | Anchor::Right);
    layer->set_margin(Margins{5, 6, 7, 8});
    layer->set_exclusive_zone(60);
    layer->set_keyboard_interactivity(KeyboardInteractivity::OnDemand);
    layer->set_layer(Layer::Overlay);
    layer->commit();
    CHECK(display.roundtrip() >= 0);

    snap = layer->snapshot();
    CHECK_EQ(snap.layer, Layer::Overlay);
    CHECK_EQ(snap.anchor, Anchor::Bottom | Anchor::Left | Anchor::Right);
    CHECK(snap.margins == (Margins{5, 6, 7, 8}));
    CHECK_EQ(snap.exclusive_zone, 60);
    CHECK_EQ(snap.keyboard_interactivity, KeyboardInteractivity::OnDemand);

    s.server.close_layer_surface();
    CHECK(display.roundtrip() >= 0);
    CHECK(layer->snapshot().closed);
    events = display.events().drain();
    CHECK(find_event<LayerClosedEvent>(events, [&](const LayerClosedEvent& e) {
              return e.surface_id == layer->id();
          }) != nullptr);
}

}  // namespace

int main() {
    run();
    return bstest::finish("test_layer_surface");
}
