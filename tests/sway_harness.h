// Helpers for the tests that run against a real compositor: a private
// headless sway (wlroots) started by tests/run-headless-sway.sh, which names
// it in BROWL_TEST_WAYLAND_DISPLAY. The tests never use $WAYLAND_DISPLAY:
// they lock the session and steal focus, which must not happen to a desktop.
#pragma once

#include "browl/browl.h"
#include "check.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct wl_display;
struct wl_registry;
struct wl_compositor;
struct wl_shm;
struct wl_surface;
struct wl_buffer;
struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;

namespace swaytest {

// Connects to the private compositor, or skips the test (exit 77) when it is
// not running under run-headless-sway.sh.
std::unique_ptr<browl::Display> connect_or_skip(const char* test_name);

// Roundtrips `display` (and `also`, a second connection, when given) until
// `pred` holds or `timeout_ms` passes. Returns pred's last value.
bool pump(browl::Display& display, const std::function<bool()>& pred, int timeout_ms = 3000,
          const std::function<void()>& also = nullptr);

// Every event the display has queued so far, kept across drains.
struct EventLog {
    std::vector<browl::ShellEvent> all;
    void take(browl::Display& display);

    template <class E, class Pred>
    const E* find(Pred pred) const {
        for (const auto& ev : all) {
            if (const E* e = std::get_if<E>(&ev); e && pred(*e)) return e;
        }
        return nullptr;
    }
    template <class E>
    const E* find() const {
        return find<E>([](const E&) { return true; });
    }
};

// A width x height buffer of one ARGB8888 colour from `pool`.
std::shared_ptr<browl::ShmBuffer> solid_buffer(browl::ShmPool& pool, int32_t width, int32_t height,
                                               uint32_t argb);

// Commits `surface` with a frame callback and waits until the compositor has
// presented it (the callback fires).
bool commit_and_wait_frame(browl::Display& display, wl_surface* surface, int timeout_ms = 3000);

// A screencopy of an output (or a region of it), converted to 0xAARRGGBB
// words, top row first.
struct Capture {
    std::string error;  // empty on success
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t shm_format = 0;
    std::vector<uint32_t> pixels;
    uint32_t at(uint32_t x, uint32_t y) const { return pixels[size_t(y) * width + x]; }
};
Capture capture(browl::Display& display, browl::Output& output, const browl::Rect* region = nullptr);

// A second, plain Wayland client with one xdg_toplevel window: what a
// taskbar sees through foreign-toplevel management.
class ToplevelClient {
public:
    ~ToplevelClient();
    bool connect(const std::string& name, std::string* error);
    // Creates the window with a title and app id and maps it (waits for the
    // first configure, attaches a buffer of the configured size).
    bool map(const std::string& title, const std::string& app_id);
    void set_title(const std::string& title);
    void destroy_window();
    int roundtrip();

    bool close_requested = false;
    bool fullscreen = false;
    bool activated = false;
    int32_t width = 0;
    int32_t height = 0;
    uint32_t configure_serial = 0;
    bool configured = false;

    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    xdg_wm_base* wm_base = nullptr;

private:
    bool attach_buffer();

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_surface* surface_ = nullptr;
    xdg_surface* xdg_surface_ = nullptr;
    xdg_toplevel* toplevel_ = nullptr;
    wl_buffer* buffer_ = nullptr;
    void* map_ = nullptr;
    size_t map_size_ = 0;
};

}  // namespace swaytest
