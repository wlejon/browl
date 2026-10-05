#include "browl/display.h"
#include "browl/layer_surface.h"
#include "browl/popup.h"
#include "headless_compositor.h"

#include <cassert>
#include <iostream>

using namespace browl;
using namespace browl::test;

int main() {
    std::cout << "Running test_popup..." << std::endl;

    HeadlessCompositor server;
    server.start();

    int client_fd = server.create_client_fd();
    assert(client_fd >= 0);

    auto display = Display::connect_to_fd(client_fd);
    assert(display != nullptr);

    LayerSurfaceConfig config;
    config.name_space = "panel";
    config.layer = Layer::Top;
    config.size = Size{1920, 48};
    auto layer = display->create_layer_surface(config);
    assert(layer != nullptr);

    auto positioner = display->create_positioner();
    assert(positioner != nullptr);
    positioner->set_size(200, 300);
    positioner->set_anchor_rect(100, 48, 50, 0);
    positioner->set_anchor(PositionerAnchor::BottomLeft);
    positioner->set_gravity(Gravity::BottomRight);
    positioner->set_constraint_adjustment(constraint_adjustment::SlideX |
                                          constraint_adjustment::FlipY);
    positioner->set_offset(0, 5);

    auto popup = layer->create_popup(*positioner);
    assert(popup != nullptr);
    assert(popup->wl_surface_ptr() != nullptr);
    assert(popup->xdg_surface_ptr() != nullptr);
    assert(popup->xdg_popup_ptr() != nullptr);

    display->roundtrip();
    assert(server.popup_created());

    // Initial snapshot
    auto snap = popup->snapshot();
    assert(!snap.configured);
    assert(!snap.dismissed);

    // Compositor configures popup
    server.configure_popup(100, 53, 200, 300);
    display->roundtrip();

    snap = popup->snapshot();
    assert(snap.configured);
    assert(snap.geometry.x == 100);
    assert(snap.geometry.y == 53);
    assert(snap.geometry.width == 200);
    assert(snap.geometry.height == 300);

    auto events = display->events().drain();
    bool found_popup_configure = false;
    for (const auto& ev : events) {
        if (std::holds_alternative<PopupConfigureEvent>(ev)) {
            const auto& conf = std::get<PopupConfigureEvent>(ev);
            if (conf.surface_id == popup->id()) {
                found_popup_configure = true;
                assert(conf.x == 100);
                assert(conf.y == 53);
                assert(conf.width == 200);
                assert(conf.height == 300);
            }
        }
    }
    assert(found_popup_configure);

    // Reposition
    popup->reposition(*positioner, 1234);
    display->roundtrip();
    assert(server.popup_repositioned_received());

    events = display->events().drain();
    bool found_repositioned = false;
    for (const auto& ev : events) {
        if (std::holds_alternative<PopupRepositionedEvent>(ev)) {
            if (std::get<PopupRepositionedEvent>(ev).token == 1234) {
                found_repositioned = true;
            }
        }
    }
    assert(found_repositioned);

    // Popup done / dismissal
    server.send_popup_done();
    display->roundtrip();

    snap = popup->snapshot();
    assert(snap.dismissed);

    events = display->events().drain();
    bool found_popup_done = false;
    for (const auto& ev : events) {
        if (std::holds_alternative<PopupDoneEvent>(ev)) {
            if (std::get<PopupDoneEvent>(ev).surface_id == popup->id()) {
                found_popup_done = true;
            }
        }
    }
    assert(found_popup_done);

    popup.reset();
    positioner.reset();
    layer.reset();
    display.reset();
    server.stop();

    std::cout << "test_popup passed!" << std::endl;
    return 0;
}
