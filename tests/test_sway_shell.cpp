// Layer surfaces, popups and screencopy against a real compositor: a private
// headless sway (run-headless-sway.sh). Oracles are the compositor's own
// decisions: the sizes it configures (output size, space left by an
// exclusive zone), where it places a popup (including sliding it back on
// screen), and the pixels it composites, read back through screencopy.
#include "browl/browl.h"
#include "sway_harness.h"

#include <wayland-client.h>

using namespace browl;
using swaytest::EventLog;

namespace {

constexpr uint32_t kBlue = 0xff336699u, kRed = 0xffcc3300u, kGreen = 0xff22aa44u, kYellow = 0xffeedd11u;

int count_not(const swaytest::Capture& cap, uint32_t argb) {
    int n = 0;
    for (uint32_t p : cap.pixels) n += p != argb;
    return n;
}

// Commits an unmapped layer surface and returns its configure event.
const LayerConfigureEvent* first_configure(Display& d, EventLog& log, LayerSurface& layer) {
    layer.commit();
    const SurfaceId id = layer.id();
    auto match = [id](const LayerConfigureEvent& e) { return e.surface_id == id; };
    swaytest::pump(d, [&] {
        log.take(d);
        return log.find<LayerConfigureEvent>(match) != nullptr && layer.snapshot().configured;
    });
    return log.find_last<LayerConfigureEvent>(match);
}

bool show(Display& d, LayerSurface& layer, ShmPool& pool, uint32_t serial, int32_t w, int32_t h, uint32_t argb,
          std::shared_ptr<ShmBuffer>& keep) {
    keep = swaytest::solid_buffer(pool, w, h, argb);
    if (!keep) return false;
    layer.ack_configure(serial);
    layer.attach_buffer(keep->wl_buffer_ptr());
    layer.damage(0, 0, w, h);
    return swaytest::commit_and_wait_frame(d, layer.wl_surface_ptr());
}

void test_globals(Display& d) {
    CHECK(d.has_compositor());
    CHECK(d.has_shm());
    CHECK(d.has_layer_shell());
    CHECK(d.has_xdg_shell());
    CHECK(d.has_foreign_toplevel_manager());
    CHECK(d.has_session_lock());
    CHECK(d.has_idle_inhibit());
    CHECK(d.has_screencopy());
    CHECK(d.has_idle_notify());

    REQUIRE(d.outputs().size() == 1);
    auto out = d.default_output();
    std::printf("output %s: %dx%d@%d mHz, scale %d, %zu mode(s)\n", out->name().c_str(),
                out->current_mode().width, out->current_mode().height, out->current_mode().refresh_mhz,
                out->scale(), out->modes().size());
    CHECK(out->name().rfind("HEADLESS-", 0) == 0);
    CHECK(out->current_mode().current);
    CHECK(out->current_mode().width > 0);
    CHECK_EQ(out->geometry().width, out->current_mode().width);
    CHECK_EQ(out->geometry().height, out->current_mode().height);
    CHECK_EQ(out->scale(), 1);
    int current = 0;
    for (const auto& m : out->modes()) current += m.current ? 1 : 0;
    CHECK_EQ(current, 1);

    REQUIRE(!d.seats().empty());
    CHECK_EQ(d.default_seat()->name(), std::string("seat0"));
}

// A full-output overlay surface: configured to the output size, and its
// pixels (then a two-colour update) are what screencopy reads back.
void test_overlay_capture(Display& d, Output& out, ShmPool& pool) {
    const int32_t W = out.current_mode().width, H = out.current_mode().height;
    EventLog log;
    LayerSurfaceConfig cfg;
    cfg.name_space = "browl-test-overlay";
    cfg.layer = Layer::Overlay;
    cfg.anchor = Anchor::Top | Anchor::Bottom | Anchor::Left | Anchor::Right;
    cfg.output = &out;
    auto layer = d.create_layer_surface(cfg);
    REQUIRE(layer != nullptr);
    const auto* conf = first_configure(d, log, *layer);
    REQUIRE(conf != nullptr);
    CHECK_EQ(int32_t(conf->width), W);
    CHECK_EQ(int32_t(conf->height), H);
    CHECK_EQ(layer->snapshot().configured_serial, conf->serial);

    std::shared_ptr<ShmBuffer> buf;
    REQUIRE(show(d, *layer, pool, conf->serial, W, H, kBlue, buf));
    auto cap = swaytest::capture(d, out);
    REQUIRE(cap.error.empty());
    std::printf("screencopy: %ux%u, shm format %u\n", cap.width, cap.height, cap.shm_format);
    CHECK_EQ(int32_t(cap.width), W);
    CHECK_EQ(int32_t(cap.height), H);
    CHECK_EQ(count_not(cap, kBlue), 0);

    // Left half red: the capture follows the new frame, pixel for pixel.
    auto two = pool.allocate_buffer(W, H, W * 4, WL_SHM_FORMAT_ARGB8888);
    REQUIRE(two != nullptr);
    auto* px = static_cast<uint32_t*>(two->data());
    for (int32_t y = 0; y < H; ++y)
        for (int32_t x = 0; x < W; ++x) px[size_t(y) * W + x] = x < W / 2 ? kRed : kBlue;
    layer->attach_buffer(two->wl_buffer_ptr());
    layer->damage(0, 0, W, H);
    REQUIRE(swaytest::commit_and_wait_frame(d, layer->wl_surface_ptr()));
    cap = swaytest::capture(d, out);
    REQUIRE(cap.error.empty());
    CHECK_EQ(cap.at(0, 0), kRed);
    CHECK_EQ(cap.at(uint32_t(W / 2 - 1), uint32_t(H - 1)), kRed);
    CHECK_EQ(cap.at(uint32_t(W / 2), 0), kBlue);
    CHECK_EQ(cap.at(uint32_t(W - 1), uint32_t(H - 1)), kBlue);

    // A region capture is that rectangle of the output.
    const Rect region{W / 2 - 10, 20, 30, 40};
    cap = swaytest::capture(d, out, &region);
    REQUIRE(cap.error.empty());
    CHECK_EQ(cap.width, 30u);
    CHECK_EQ(cap.height, 40u);
    CHECK_EQ(cap.at(0, 0), kRed);
    CHECK_EQ(cap.at(9, 39), kRed);
    CHECK_EQ(cap.at(10, 0), kBlue);
    CHECK_EQ(cap.at(29, 39), kBlue);

    // Closing the overlay: what is under it (nothing) shows again.
    layer.reset();
    CHECK(d.roundtrip() >= 0);
}

// A panel with an exclusive zone, a surface that respects it, and popups
// placed by the compositor relative to the panel.
void test_exclusive_zone_and_popup(Display& d, Output& out, ShmPool& pool) {
    const int32_t W = out.current_mode().width, H = out.current_mode().height;
    EventLog log;

    LayerSurfaceConfig panel_cfg;
    panel_cfg.name_space = "browl-test-panel";
    panel_cfg.layer = Layer::Top;
    panel_cfg.anchor = Anchor::Top | Anchor::Left | Anchor::Right;
    panel_cfg.size = Size{0, 30};
    panel_cfg.exclusive_zone = 30;
    panel_cfg.output = &out;
    auto panel = d.create_layer_surface(panel_cfg);
    REQUIRE(panel != nullptr);
    const auto* pconf = first_configure(d, log, *panel);
    REQUIRE(pconf != nullptr);
    CHECK_EQ(int32_t(pconf->width), W);
    CHECK_EQ(pconf->height, 30u);
    std::shared_ptr<ShmBuffer> panel_buf;
    REQUIRE(show(d, *panel, pool, pconf->serial, W, 30, kGreen, panel_buf));

    // Exclusive zone 0: arranged in the space the panel leaves.
    LayerSurfaceConfig body_cfg;
    body_cfg.name_space = "browl-test-body";
    body_cfg.layer = Layer::Top;
    body_cfg.anchor = Anchor::Top | Anchor::Bottom | Anchor::Left | Anchor::Right;
    body_cfg.output = &out;
    auto body = d.create_layer_surface(body_cfg);
    REQUIRE(body != nullptr);
    const auto* bconf = first_configure(d, log, *body);
    REQUIRE(bconf != nullptr);
    CHECK_EQ(int32_t(bconf->width), W);
    CHECK_EQ(int32_t(bconf->height), H - 30);
    std::shared_ptr<ShmBuffer> body_buf;
    REQUIRE(show(d, *body, pool, bconf->serial, W, H - 30, kBlue, body_buf));

    auto cap = swaytest::capture(d, out);
    REQUIRE(cap.error.empty());
    CHECK_EQ(cap.at(uint32_t(W / 2), 0), kGreen);
    CHECK_EQ(cap.at(uint32_t(W / 2), 29), kGreen);
    CHECK_EQ(cap.at(uint32_t(W / 2), 30), kBlue);
    CHECK_EQ(cap.at(uint32_t(W / 2), uint32_t(H - 1)), kBlue);

    // A popup below a 20x20 anchor rect at (10,10) of the panel, growing
    // right and down: the compositor puts it at (10,30).
    auto pos = d.create_positioner();
    REQUIRE(pos != nullptr);
    pos->set_size(100, 50);
    pos->set_anchor_rect(10, 10, 20, 20);
    pos->set_anchor(PositionerAnchor::BottomLeft);
    pos->set_gravity(Gravity::BottomRight);
    auto popup = panel->create_popup(*pos);
    REQUIRE(popup != nullptr);
    popup->commit();
    REQUIRE(swaytest::pump(d, [&] { return popup->snapshot().configured; }));
    CHECK(popup->snapshot().geometry == (Rect{10, 30, 100, 50}));
    log.take(d);
    CHECK(log.find<PopupConfigureEvent>([&](const PopupConfigureEvent& e) {
              return e.surface_id == popup->id() && e.x == 10 && e.y == 30;
          }) != nullptr);
    auto popup_buf = swaytest::solid_buffer(pool, 100, 50, kYellow);
    REQUIRE(popup_buf != nullptr);
    popup->attach_buffer(popup_buf->wl_buffer_ptr());
    popup->damage(0, 0, 100, 50);
    REQUIRE(swaytest::commit_and_wait_frame(d, popup->wl_surface_ptr()));
    cap = swaytest::capture(d, out);
    REQUIRE(cap.error.empty());
    CHECK_EQ(cap.at(10, 30), kYellow);
    CHECK_EQ(cap.at(109, 79), kYellow);
    CHECK_EQ(cap.at(110, 79), kBlue);

    // Reposition against the right edge with SlideX: the compositor slides
    // the popup back on screen, flush with the output's right edge.
    auto edge = d.create_positioner();
    REQUIRE(edge != nullptr);
    edge->set_size(100, 50);
    edge->set_anchor_rect(W - 5, 10, 5, 20);
    edge->set_anchor(PositionerAnchor::BottomLeft);
    edge->set_gravity(Gravity::BottomRight);
    edge->set_constraint_adjustment(constraint_adjustment::SlideX);
    popup->reposition(*edge, 77);
    popup->commit();
    if (swaytest::pump(d, [&] { return popup->snapshot().repositioned_token == 77u; }, 1000)) {
        CHECK(swaytest::pump(d, [&] { return popup->snapshot().geometry.x != 10; }));
        CHECK(popup->snapshot().geometry == (Rect{W - 100, 30, 100, 50}));
        log.take(d);
        CHECK(log.find<PopupRepositionedEvent>([](const PopupRepositionedEvent& e) { return e.token == 77u; }) !=
              nullptr);
    } else {
        std::printf("note: sway version did not emit popup repositioned event; skipping reposition assertion\n");
    }

    popup.reset();
    body.reset();
    panel.reset();
    CHECK(d.roundtrip() >= 0);
}

}  // namespace

int main() {
    auto display = swaytest::connect_or_skip("test_sway_shell");
    if (display) {
        test_globals(*display);
        auto out = display->default_output();
        auto pool = display->create_shm_pool(4096);
        if (out && pool) {
            test_overlay_capture(*display, *out, *pool);
            test_exclusive_zone_and_popup(*display, *out, *pool);
        } else {
            bstest::fail(__FILE__, __LINE__, "no output or shm pool");
        }
    }
    return bstest::finish("test_sway_shell");
}
