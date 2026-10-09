// Window and the application-window globals against the in-process test
// double (fake_session.h): what browl requests for an xdg_toplevel
// (title, app id, size limits, decoration mode, viewport, icon), the
// configure sequence acked and turned into a WindowConfigureEvent, close,
// the scale precedence (fractional > preferred buffer scale > outputs),
// unmap / map, hidden creation, presentation feedback (also requested from
// another thread), activation tokens, xdg-output logical geometry.
#include "browl/browl.h"
#include "fake_session.h"

#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <ctime>
#include <thread>

using namespace browl;
using bstest::find_event;

namespace {

std::vector<ShellEvent> settle(Display& d) {
    // Two roundtrips: one for the events, one for the requests browl made
    // while handling them (acks) to reach the compositor.
    d.roundtrip();
    d.roundtrip();
    return d.events().drain();
}

void test_globals_and_output(bstest::FakeSession& s) {
    Display& d = *s.display;
    CHECK(d.has_decoration_manager());
    CHECK(d.has_viewporter());
    CHECK(d.has_fractional_scale());
    CHECK(d.has_presentation());
    CHECK(d.has_activation());
    CHECK(d.has_cursor_shape());
    CHECK(d.has_data_device());
    CHECK(d.has_primary_selection());
    CHECK(d.has_text_input());
    CHECK(d.has_toplevel_icon());
    CHECK(d.has_xdg_output());
    CHECK(!d.has_pointer_constraints());
    CHECK(!d.has_relative_pointer());
    CHECK_EQ(d.presentation_clock_id(), int(CLOCK_MONOTONIC));

    auto out = d.default_output();
    REQUIRE(out != nullptr);
    auto snap = out->snapshot();
    CHECK_EQ(snap.description, std::string("Test Output DP-1"));
    // No logical geometry from xdg-output yet: derived from mode and scale.
    CHECK(snap.logical == (Rect{0, 0, 1920, 1080}));
    s.server.send_xdg_output_logical(0, 0, 1280, 720);
    auto events = settle(d);
    CHECK(out->snapshot().logical == (Rect{0, 0, 1280, 720}));
    CHECK(find_event<OutputChangedEvent>(events, [](const OutputChangedEvent& e) {
              return e.output.logical == Rect{0, 0, 1280, 720};
          }) != nullptr);
}

void test_window(bstest::FakeSession& s) {
    Display& d = *s.display;
    WindowConfig config;
    config.title = "browl window";
    config.app_id = "org.browl.test";
    config.min_size = {200, 100};
    auto window = d.create_window(config);
    REQUIRE(window != nullptr);
    CHECK(window->mapped());
    CHECK(window->wl_surface_ptr() != nullptr);
    CHECK_EQ(d.surface_id_of(window->wl_surface_ptr()), window->id());
    CHECK_EQ(d.surface_id_of(nullptr), kNoSurface);
    settle(d);

    auto st = s.server.app_state();
    CHECK_EQ(st.toplevels_created, 1);
    CHECK_EQ(st.title, std::string("browl window"));
    CHECK_EQ(st.app_id, std::string("org.browl.test"));
    CHECK_EQ(st.min_width, 200);
    CHECK_EQ(st.min_height, 100);
    CHECK_EQ(st.decoration_mode_requested, uint32_t(ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE));
    CHECK(st.commits >= 1);  // the initial commit
    CHECK(st.viewport_created);
    CHECK(st.fractional_created);
    CHECK(!window->snapshot().configured);

    // One configure sequence: acked, then reported with the new state.
    const uint32_t serial = s.server.configure_window(
        800, 600, {XDG_TOPLEVEL_STATE_ACTIVATED, XDG_TOPLEVEL_STATE_MAXIMIZED}, 1900, 1000,
        {XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE, XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN},
        ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    REQUIRE(serial != 0);
    auto events = settle(d);
    CHECK_EQ(s.server.app_state().last_ack_serial, serial);
    const auto* conf = find_event<WindowConfigureEvent>(events);
    REQUIRE(conf != nullptr);
    CHECK_EQ(conf->surface_id, window->id());
    CHECK_EQ(conf->serial, serial);
    CHECK(conf->snapshot.configured);
    CHECK(conf->snapshot.configured_size == (Size{800, 600}));
    CHECK(conf->snapshot.bounds == (Size{1900, 1000}));
    CHECK_EQ(conf->snapshot.states, window_state::Activated | window_state::Maximized);
    CHECK_EQ(conf->snapshot.wm_capabilities, wm_capability::Maximize | wm_capability::Fullscreen);
    CHECK(conf->snapshot.decoration == DecorationMode::ServerSide);
    CHECK(window->snapshot() == conf->snapshot);

    // A configure that only resizes: the states it names are the states.
    s.server.configure_window(1024, 768, {});
    events = settle(d);
    conf = find_event<WindowConfigureEvent>(events);
    REQUIRE(conf != nullptr);
    CHECK(conf->snapshot.configured_size == (Size{1024, 768}));
    CHECK_EQ(conf->snapshot.states, 0u);

    s.server.close_window();
    events = settle(d);
    CHECK(find_event<WindowCloseEvent>(events, [&](const WindowCloseEvent& e) {
              return e.surface_id == window->id();
          }) != nullptr);
    CHECK(window->snapshot().close_requested);

    // Scale: the output (scale 1) changes nothing; the preferred buffer
    // scale does; a fractional scale wins over it.
    auto out = d.default_output();
    s.server.send_surface_enter();
    events = settle(d);
    const auto* outs = find_event<WindowOutputsEvent>(events);
    REQUIRE(outs != nullptr);
    CHECK(outs->outputs == std::vector<OutputId>{out->id()});
    CHECK(find_event<WindowScaleEvent>(events) == nullptr);
    s.server.send_preferred_buffer_scale(2);
    events = settle(d);
    const auto* scale = find_event<WindowScaleEvent>(events);
    REQUIRE(scale != nullptr);
    CHECK_EQ(scale->scale120, 240u);
    s.server.send_fractional_scale(180);
    events = settle(d);
    scale = find_event<WindowScaleEvent>(events);
    REQUIRE(scale != nullptr);
    CHECK_EQ(scale->scale120, 180u);
    s.server.send_preferred_buffer_scale(3);
    events = settle(d);
    CHECK(find_event<WindowScaleEvent>(events) == nullptr);
    CHECK_EQ(window->snapshot().scale120, 180u);
    s.server.send_surface_leave();
    events = settle(d);
    outs = find_event<WindowOutputsEvent>(events);
    REQUIRE(outs != nullptr);
    CHECK(outs->outputs.empty());

    // Viewport destination, buffer scale, window geometry.
    CHECK(window->set_logical_size(640, 360));
    window->set_buffer_scale(2);
    window->set_window_geometry(Rect{0, 0, 600, 300});
    window->commit();
    settle(d);
    st = s.server.app_state();
    CHECK_EQ(st.viewport_width, 640);
    CHECK_EQ(st.viewport_height, 360);
    CHECK_EQ(st.buffer_scale, 2);
    CHECK_EQ(st.geometry_width, 600);
    CHECK(window->set_logical_size(0, 0));
    settle(d);
    CHECK_EQ(s.server.app_state().viewport_width, -1);

    // Requests on the toplevel.
    window->set_title("renamed");
    window->set_max_size(3000, 2000);
    window->set_fullscreen(true, out.get());
    window->set_minimized();
    window->set_maximized(false);
    window->set_server_side_decorations(false);
    settle(d);
    st = s.server.app_state();
    CHECK_EQ(st.title, std::string("renamed"));
    CHECK_EQ(st.max_width, 3000);
    CHECK(st.fullscreen);
    CHECK(st.minimized);
    CHECK(!st.maximized);
    CHECK_EQ(st.decoration_mode_requested, uint32_t(ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE));
    window->set_fullscreen(false);
    window->set_server_side_decorations(true);
    settle(d);
    CHECK(!s.server.app_state().fullscreen);

    // Icon: straight RGBA in, premultiplied ARGB8888 on the wire.
    CHECK(window->has_icon_protocol());
    std::vector<uint8_t> rgba(16 * 16 * 4);
    for (size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = 255;      // R
        rgba[i + 3] = 128;  // A (half)
    }
    CHECK(window->set_icon(16, 16, rgba.data()));
    CHECK(!window->set_icon(16, 8, rgba.data()));
    settle(d);
    st = s.server.app_state();
    CHECK_EQ(st.icon_buffers_added, 1);
    CHECK_EQ(st.icon_buffer_size, 16);
    CHECK_EQ(st.icons_set, 1);
    CHECK_EQ(st.icon_first_pixel, 0x80800000u);
    CHECK(window->set_icon(0, 0, nullptr));
    settle(d);
    CHECK_EQ(s.server.app_state().icons_cleared, 1);

    // Presentation feedback for the next commit, from this thread and another.
    const RequestId fb1 = d.request_presentation_feedback(window->wl_surface_ptr());
    CHECK(fb1 != 0);
    window->commit();
    settle(d);
    CHECK_EQ(s.server.app_state().feedbacks_requested, 1);
    s.server.send_presented(5'000'000'123ull, 16'666'666, 42, presentation_flags::Vsync | presentation_flags::HwClock);
    events = settle(d);
    const auto* pf = find_event<PresentationFeedbackEvent>(events);
    REQUIRE(pf != nullptr);
    CHECK_EQ(pf->request, fb1);
    CHECK_EQ(pf->surface_id, window->id());
    CHECK(pf->presented);
    CHECK_EQ(pf->time_ns, 5'000'000'123ull);
    CHECK_EQ(pf->refresh_ns, 16'666'666u);
    CHECK_EQ(pf->sequence, 42ull);
    CHECK_EQ(pf->flags, presentation_flags::Vsync | presentation_flags::HwClock);
    CHECK_EQ(pf->output, out->id());

    RequestId fb2 = 0;
    std::thread other([&] { fb2 = d.request_presentation_feedback(window->wl_surface_ptr()); });
    other.join();
    CHECK(fb2 != 0 && fb2 != fb1);
    window->commit();
    settle(d);
    CHECK_EQ(s.server.app_state().feedbacks_requested, 2);
    s.server.send_discarded();
    events = settle(d);
    pf = find_event<PresentationFeedbackEvent>(events);
    REQUIRE(pf != nullptr);
    CHECK_EQ(pf->request, fb2);
    CHECK(!pf->presented);
    CHECK_EQ(d.request_presentation_feedback(nullptr), RequestId(0));

    // Activation: a token for this window, then activating with it.
    auto seat = d.default_seat();
    REQUIRE(seat != nullptr);
    const RequestId tok = d.request_activation_token("org.browl.test", window->wl_surface_ptr(), seat.get(), 77);
    CHECK(tok != 0);
    events = settle(d);
    const auto* te = find_event<ActivationTokenEvent>(events);
    REQUIRE(te != nullptr);
    CHECK_EQ(te->request, tok);
    CHECK_EQ(te->token, std::string("token-1"));
    st = s.server.app_state();
    CHECK_EQ(st.token_app_id, std::string("org.browl.test"));
    CHECK_EQ(st.token_serial, 77u);
    CHECK(st.token_has_surface);
    CHECK(d.activate(te->token, window->wl_surface_ptr()));
    CHECK(!d.activate("", window->wl_surface_ptr()));
    settle(d);
    CHECK_EQ(s.server.app_state().activated_token, std::string("token-1"));

    // Unmap keeps the surface and drops the role; map brings it back with
    // what the client asked for.
    const int commits_before = s.server.app_state().commits;
    window->unmap();
    CHECK(!window->mapped());
    CHECK(window->xdg_toplevel_ptr() == nullptr);
    CHECK(window->wl_surface_ptr() != nullptr);
    CHECK(!window->snapshot().configured);
    settle(d);
    st = s.server.app_state();
    CHECK_EQ(st.toplevels_destroyed, 1);
    CHECK(st.null_attaches >= 1);
    CHECK(st.commits > commits_before);
    window->map();
    window->map();  // a no-op when mapped
    CHECK(window->mapped());
    settle(d);
    st = s.server.app_state();
    CHECK_EQ(st.toplevels_created, 2);
    CHECK(st.null_attaches >= 2);
    CHECK_EQ(st.title, std::string("renamed"));
    CHECK_EQ(st.decorations_created, 2);
    CHECK_EQ(st.decoration_mode_requested, uint32_t(ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE));
    const uint32_t serial2 = s.server.configure_window(500, 400, {XDG_TOPLEVEL_STATE_ACTIVATED});
    events = settle(d);
    CHECK_EQ(s.server.app_state().last_ack_serial, serial2);
    CHECK(window->snapshot().configured);
    CHECK(window->snapshot().configured_size == (Size{500, 400}));

    window.reset();
    settle(d);
    CHECK_EQ(s.server.app_state().toplevels_destroyed, 2);
}

void test_hidden_and_borderless(bstest::FakeSession& s) {
    Display& d = *s.display;
    const int surfaces_before = s.server.app_state().xdg_surfaces_created;

    // Hidden: a wl_surface with no role until map().
    WindowConfig hidden;
    hidden.title = "hidden";
    hidden.mapped = false;
    auto window = d.create_window(hidden);
    REQUIRE(window != nullptr);
    CHECK(!window->mapped());
    CHECK(window->wl_surface_ptr() != nullptr);
    CHECK(window->xdg_surface_ptr() == nullptr);
    CHECK_EQ(d.surface_id_of(window->wl_surface_ptr()), window->id());
    settle(d);
    CHECK_EQ(s.server.app_state().xdg_surfaces_created, surfaces_before);
    window->unmap();  // a no-op when unmapped
    window->map();
    settle(d);
    CHECK_EQ(s.server.app_state().xdg_surfaces_created, surfaces_before + 1);
    CHECK_EQ(s.server.app_state().title, std::string("hidden"));
    window.reset();

    // Borderless: asks for client-side decorations.
    WindowConfig borderless;
    borderless.server_side_decorations = false;
    window = d.create_window(borderless);
    REQUIRE(window != nullptr);
    settle(d);
    CHECK_EQ(s.server.app_state().decoration_mode_requested,
             uint32_t(ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE));
    s.server.configure_window(0, 0, {}, 0, 0, {}, ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
    auto events = settle(d);
    const auto* conf = find_event<WindowConfigureEvent>(events);
    REQUIRE(conf != nullptr);
    CHECK(conf->snapshot.decoration == DecorationMode::ClientSide);
    CHECK(conf->snapshot.configured_size == (Size{0, 0}));  // "choose"
    // No wm_capabilities sent: all of them.
    CHECK_EQ(conf->snapshot.wm_capabilities, wm_capability::All);
}

}  // namespace

int main() {
    bstest::FakeSession s;
    if (s.display) {
        test_globals_and_output(s);
        test_window(s);
        test_hidden_and_borderless(s);
    }
    return bstest::finish("test_window");
}
