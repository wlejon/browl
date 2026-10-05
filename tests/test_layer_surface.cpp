#include "browl/display.h"
#include "browl/layer_surface.h"
#include "browl/output.h"
#include "headless_compositor.h"

#include <cassert>
#include <iostream>

using namespace browl;
using namespace browl::test;

int main() {
    std::cout << "Running test_layer_surface..." << std::endl;

    HeadlessCompositor server;
    server.start();

    int client_fd = server.create_client_fd();
    assert(client_fd >= 0);

    auto display = Display::connect_to_fd(client_fd);
    assert(display != nullptr);

    LayerSurfaceConfig config;
    config.name_space = "panel";
    config.layer = Layer::Top;
    config.anchor = Anchor::Top | Anchor::Left | Anchor::Right;
    config.margins = Margins{0, 0, 0, 0};
    config.size = Size{1920, 48};
    config.exclusive_zone = 48;
    config.keyboard_interactivity = KeyboardInteractivity::None;
    config.output = display->default_output().get();

    auto layer = display->create_layer_surface(config);
    assert(layer != nullptr);
    assert(layer->wl_surface_ptr() != nullptr);
    assert(layer->zwlr_layer_surface_ptr() != nullptr);

    display->roundtrip();
    assert(server.layer_surface_created());

    // Initial snapshot check
    auto snap = layer->snapshot();
    assert(snap.layer == Layer::Top);
    assert((snap.anchor & Anchor::Top) == Anchor::Top);
    assert(snap.exclusive_zone == 48);
    assert(!snap.closed);

    // Compositor configures layer surface
    server.configure_layer_surface(1920, 48);
    display->roundtrip();

    // Check configure event in queue
    auto events = display->events().drain();
    bool found_configure = false;
    uint32_t configured_serial = 0;
    for (const auto& ev : events) {
        if (std::holds_alternative<LayerConfigureEvent>(ev)) {
            const auto& conf = std::get<LayerConfigureEvent>(ev);
            if (conf.surface_id == layer->id()) {
                found_configure = true;
                configured_serial = conf.serial;
                assert(conf.width == 1920);
                assert(conf.height == 48);
            }
        }
    }
    assert(found_configure);

    // Snapshot reflects configured properties
    snap = layer->snapshot();
    assert(snap.configured_size.width == 1920);
    assert(snap.configured_size.height == 48);
    assert(snap.configured_serial == configured_serial);

    // Ack configure and commit
    layer->ack_configure(configured_serial);
    layer->commit();
    display->roundtrip();

    assert(server.last_layer_ack_serial() == configured_serial);
    assert(server.layer_surface_committed());

    // Dynamic reconfiguration
    layer->set_size(1920, 60);
    layer->set_anchor(Anchor::Bottom | Anchor::Left | Anchor::Right);
    layer->set_margin(Margins{5, 5, 5, 5});
    layer->set_exclusive_zone(60);
    layer->set_keyboard_interactivity(KeyboardInteractivity::OnDemand);
    layer->set_layer(Layer::Overlay);
    layer->commit();
    display->roundtrip();

    snap = layer->snapshot();
    assert(snap.layer == Layer::Overlay);
    assert(snap.exclusive_zone == 60);
    assert(snap.keyboard_interactivity == KeyboardInteractivity::OnDemand);
    assert(snap.margins.top == 5);

    // Server closes layer surface
    server.close_layer_surface();
    display->roundtrip();

    snap = layer->snapshot();
    assert(snap.closed);

    events = display->events().drain();
    bool found_closed = false;
    for (const auto& ev : events) {
        if (std::holds_alternative<LayerClosedEvent>(ev)) {
            if (std::get<LayerClosedEvent>(ev).surface_id == layer->id()) {
                found_closed = true;
            }
        }
    }
    assert(found_closed);

    layer.reset();
    display.reset();
    server.stop();

    std::cout << "test_layer_surface passed!" << std::endl;
    return 0;
}
