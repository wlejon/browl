#include "sway_harness.h"

#include "xdg-shell-client-protocol.h"

#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace swaytest {

std::unique_ptr<browl::Display> connect_or_skip(const char* test_name) {
    const std::string name = bstest::env("BROWL_TEST_WAYLAND_DISPLAY");
    if (name.empty()) {
        bstest::skip(test_name,
                     "BROWL_TEST_WAYLAND_DISPLAY is unset: run through tests/run-headless-sway.sh, which "
                     "starts a private headless sway (never the desktop's compositor)");
    }
    std::string error;
    auto display = browl::Display::connect(name, &error);
    if (!display) bstest::fail(__FILE__, __LINE__, "connect to the headless sway: " + error);
    return display;
}

bool pump(browl::Display& display, const std::function<bool()>& pred, int timeout_ms,
          const std::function<void()>& also) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        if (pred()) return true;
        if (display.roundtrip() < 0) return pred();
        if (also) also();
        if (pred()) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void EventLog::take(browl::Display& display) {
    for (auto& ev : display.events().drain()) all.push_back(std::move(ev));
}

std::shared_ptr<browl::ShmBuffer> solid_buffer(browl::ShmPool& pool, int32_t width, int32_t height,
                                               uint32_t argb) {
    auto buffer = pool.allocate_buffer(width, height, width * 4, WL_SHM_FORMAT_ARGB8888);
    if (!buffer) return nullptr;
    auto* px = static_cast<uint32_t*>(buffer->data());
    std::fill(px, px + size_t(width) * height, argb);
    return buffer;
}

namespace {

void frame_done(void* data, wl_callback* cb, uint32_t) {
    *static_cast<bool*>(data) = true;
    wl_callback_destroy(cb);
}
const wl_callback_listener frame_listener = {.done = frame_done};

}  // namespace

bool commit_and_wait_frame(browl::Display& display, wl_surface* surface, int timeout_ms) {
    bool done = false;
    wl_callback* cb = wl_surface_frame(surface);
    wl_callback_add_listener(cb, &frame_listener, &done);
    wl_surface_commit(surface);
    if (pump(display, [&] { return done; }, timeout_ms)) return true;
    wl_callback_destroy(cb);  // `done` goes out of scope
    return false;
}

Capture capture(browl::Display& display, browl::Output& output, const browl::Rect* region) {
    Capture out;
    auto mgr = display.screencopy_manager();
    if (!mgr) {
        out.error = "no screencopy manager";
        return out;
    }
    auto frame = region ? mgr->capture_output_region(output, *region, false) : mgr->capture_output(output, false);
    if (!frame) {
        out.error = "capture request failed";
        return out;
    }
    if (!pump(display, [&] { return frame->width() > 0 || frame->is_failed(); }) || frame->is_failed()) {
        out.error = "the compositor offered no shm buffer";
        return out;
    }
    const uint32_t w = frame->width(), h = frame->height(), stride = frame->stride();
    auto pool = display.create_shm_pool(size_t(stride) * h);
    auto buffer = pool ? pool->create_buffer(0, int32_t(w), int32_t(h), int32_t(stride), frame->format()) : nullptr;
    if (!buffer) {
        out.error = "could not allocate the capture buffer";
        return out;
    }
    frame->copy(buffer->wl_buffer_ptr());
    if (!pump(display, [&] { return frame->is_ready() || frame->is_failed(); }, 5000) || !frame->is_ready()) {
        out.error = frame->is_failed() ? "the compositor reported the copy failed" : "no ready event";
        return out;
    }

    constexpr uint32_t kAbgr8888 = 0x34324241, kXbgr8888 = 0x34324258;  // DRM fourcc 'AB24', 'XB24'
    const uint32_t fmt = frame->format();
    const bool xrgb = fmt == WL_SHM_FORMAT_XRGB8888 || fmt == kXbgr8888;
    const bool swap_rb = fmt == kAbgr8888 || fmt == kXbgr8888;
    if (fmt != WL_SHM_FORMAT_ARGB8888 && fmt != WL_SHM_FORMAT_XRGB8888 && !swap_rb) {
        out.error = "unexpected shm format " + std::to_string(fmt);
        return out;
    }
    const bool y_invert = (frame->flags() & browl::screencopy_flags::YInverted) != 0;
    out.width = w;
    out.height = h;
    out.shm_format = fmt;
    out.pixels.resize(size_t(w) * h);
    const auto* base = static_cast<const uint8_t*>(buffer->data());
    for (uint32_t y = 0; y < h; ++y) {
        const auto* row = reinterpret_cast<const uint32_t*>(base + size_t(y_invert ? h - 1 - y : y) * stride);
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t p = row[x];
            if (swap_rb) p = (p & 0xff00ff00u) | ((p & 0xffu) << 16) | ((p >> 16) & 0xffu);
            if (xrgb) p |= 0xff000000u;
            out.pixels[size_t(y) * w + x] = p;
        }
    }
    return out;
}

// ---- ToplevelClient ---------------------------------------------------------

namespace {

void wm_ping(void*, xdg_wm_base* wm, uint32_t serial) { xdg_wm_base_pong(wm, serial); }
const xdg_wm_base_listener wm_listener = {.ping = wm_ping};

void registry_global(void* data, wl_registry* registry, uint32_t name, const char* iface, uint32_t version) {
    auto* c = static_cast<ToplevelClient*>(data);
    if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
        c->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
    } else if (std::strcmp(iface, wl_shm_interface.name) == 0) {
        c->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (std::strcmp(iface, xdg_wm_base_interface.name) == 0) {
        c->wm_base = static_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, 5u)));
        xdg_wm_base_add_listener(c->wm_base, &wm_listener, c);
    }
}
void registry_remove(void*, wl_registry*, uint32_t) {}
const wl_registry_listener registry_listener = {.global = registry_global, .global_remove = registry_remove};

}  // namespace

struct ToplevelClientListeners {
    static void toplevel_configure(void* data, xdg_toplevel*, int32_t w, int32_t h, wl_array* states) {
        auto* c = static_cast<ToplevelClient*>(data);
        c->width = w;
        c->height = h;
        c->fullscreen = false;
        c->activated = false;
        const auto* s = static_cast<const uint32_t*>(states->data);
        for (size_t i = 0; i < states->size / sizeof(uint32_t); ++i) {
            if (s[i] == XDG_TOPLEVEL_STATE_FULLSCREEN) c->fullscreen = true;
            if (s[i] == XDG_TOPLEVEL_STATE_ACTIVATED) c->activated = true;
        }
    }
    static void toplevel_close(void* data, xdg_toplevel*) { static_cast<ToplevelClient*>(data)->close_requested = true; }
    static void toplevel_bounds(void*, xdg_toplevel*, int32_t, int32_t) {}
    static void toplevel_caps(void*, xdg_toplevel*, wl_array*) {}
    static void surface_configure(void* data, xdg_surface* surface, uint32_t serial) {
        auto* c = static_cast<ToplevelClient*>(data);
        xdg_surface_ack_configure(surface, serial);
        c->configure_serial = serial;
        c->configured = true;
    }
};

namespace {
const xdg_toplevel_listener toplevel_listener = {
    .configure = ToplevelClientListeners::toplevel_configure,
    .close = ToplevelClientListeners::toplevel_close,
    .configure_bounds = ToplevelClientListeners::toplevel_bounds,
    .wm_capabilities = ToplevelClientListeners::toplevel_caps,
};
const xdg_surface_listener xdg_surface_listener_impl = {.configure = ToplevelClientListeners::surface_configure};
}  // namespace

ToplevelClient::~ToplevelClient() {
    destroy_window();
    if (wm_base) xdg_wm_base_destroy(wm_base);
    if (shm) wl_shm_destroy(shm);
    if (compositor) wl_compositor_destroy(compositor);
    if (registry_) wl_registry_destroy(registry_);
    if (display_) wl_display_disconnect(display_);
}

bool ToplevelClient::connect(const std::string& name, std::string* error) {
    display_ = wl_display_connect(name.c_str());
    if (!display_) {
        *error = "wl_display_connect(" + name + ") failed";
        return false;
    }
    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &registry_listener, this);
    wl_display_roundtrip(display_);
    if (!compositor || !shm || !wm_base) {
        *error = "the compositor lacks wl_compositor, wl_shm or xdg_wm_base";
        return false;
    }
    return true;
}

int ToplevelClient::roundtrip() {
    return display_ ? wl_display_roundtrip(display_) : -1;
}

bool ToplevelClient::attach_buffer() {
    const int32_t w = width > 0 ? width : 320, h = height > 0 ? height : 240;
    if (buffer_) wl_buffer_destroy(buffer_);
    if (map_) munmap(map_, map_size_);
    buffer_ = nullptr;
    map_ = nullptr;
    map_size_ = size_t(w) * h * 4;
    const int fd = memfd_create("browl-test-window", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, off_t(map_size_)) < 0) return false;
    map_ = mmap(nullptr, map_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map_ == MAP_FAILED) {
        map_ = nullptr;
        close(fd);
        return false;
    }
    std::fill(static_cast<uint32_t*>(map_), static_cast<uint32_t*>(map_) + size_t(w) * h, 0xff20c040u);
    wl_shm_pool* pool = wl_shm_create_pool(shm, fd, int32_t(map_size_));
    buffer_ = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_surface_attach(surface_, buffer_, 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, w, h);
    wl_surface_commit(surface_);
    return true;
}

bool ToplevelClient::map(const std::string& title, const std::string& app_id) {
    surface_ = wl_compositor_create_surface(compositor);
    xdg_surface_ = xdg_wm_base_get_xdg_surface(wm_base, surface_);
    xdg_surface_add_listener(xdg_surface_, &xdg_surface_listener_impl, this);
    toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
    xdg_toplevel_add_listener(toplevel_, &toplevel_listener, this);
    xdg_toplevel_set_title(toplevel_, title.c_str());
    xdg_toplevel_set_app_id(toplevel_, app_id.c_str());
    wl_surface_commit(surface_);
    for (int i = 0; i < 100 && !configured; ++i) {
        if (roundtrip() < 0) return false;
    }
    if (!configured) return false;
    return attach_buffer() && roundtrip() >= 0;
}

void ToplevelClient::set_title(const std::string& title) {
    if (toplevel_) xdg_toplevel_set_title(toplevel_, title.c_str());
    if (surface_) wl_surface_commit(surface_);
    roundtrip();
}

void ToplevelClient::destroy_window() {
    if (toplevel_) xdg_toplevel_destroy(toplevel_);
    if (xdg_surface_) xdg_surface_destroy(xdg_surface_);
    if (surface_) wl_surface_destroy(surface_);
    if (buffer_) wl_buffer_destroy(buffer_);
    if (map_) munmap(map_, map_size_);
    toplevel_ = nullptr;
    xdg_surface_ = nullptr;
    surface_ = nullptr;
    buffer_ = nullptr;
    map_ = nullptr;
    if (display_) wl_display_roundtrip(display_);
}

}  // namespace swaytest
