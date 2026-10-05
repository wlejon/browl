#include "browl/display.h"
#include "browl/output.h"
#include "browl/screencopy.h"
#include "browl/shm_pool.h"
#include "headless_compositor.h"

#include <cassert>
#include <iostream>

using namespace browl;
using namespace browl::test;

int main() {
    std::cout << "Running test_screencopy..." << std::endl;

    HeadlessCompositor server;
    server.start();

    int client_fd = server.create_client_fd();
    assert(client_fd >= 0);

    auto display = Display::connect_to_fd(client_fd);
    assert(display != nullptr);

    auto mgr = display->screencopy_manager();
    assert(mgr != nullptr);
    assert(mgr->zwlr_manager_ptr() != nullptr);

    auto output = display->default_output();
    assert(output != nullptr);

    // 1. Successful capture flow
    auto frame = mgr->capture_output(*output, true);
    assert(frame != nullptr);
    assert(frame->zwlr_frame_ptr() != nullptr);

    display->roundtrip();
    assert(server.screencopy_frame_created());

    // Compositor advertises buffer format and size
    constexpr uint32_t kFormat = 1; // WL_SHM_FORMAT_XRGB8888
    constexpr uint32_t kWidth = 1920;
    constexpr uint32_t kHeight = 1080;
    constexpr uint32_t kStride = 1920 * 4;

    server.send_screencopy_buffer(kFormat, kWidth, kHeight, kStride);
    display->roundtrip();

    assert(frame->format() == kFormat);
    assert(frame->width() == kWidth);
    assert(frame->height() == kHeight);
    assert(frame->stride() == kStride);

    auto events = display->events().drain();
    bool found_buffer = false;
    for (const auto& ev : events) {
        if (std::holds_alternative<ScreenCopyBufferEvent>(ev)) {
            const auto& buf = std::get<ScreenCopyBufferEvent>(ev);
            if (buf.format == kFormat && buf.width == kWidth && buf.height == kHeight) {
                found_buffer = true;
            }
        }
    }
    assert(found_buffer);

    // Allocate shm buffer and copy
    auto pool = display->create_shm_pool(kStride * kHeight);
    assert(pool != nullptr);
    auto buffer = pool->allocate_buffer(kWidth, kHeight, kStride, kFormat);
    assert(buffer != nullptr);

    frame->copy(buffer->wl_buffer_ptr());
    display->roundtrip();
    assert(server.screencopy_copy_received());

    // Compositor signals frame is ready
    server.send_screencopy_ready(42, 1000000);
    display->roundtrip();

    assert(frame->is_ready());
    assert(!frame->is_failed());
    auto snap = frame->snapshot();
    assert(snap.status == ScreenCopyFrameSnapshot::Status::Ready);
    assert(snap.tv_sec == 42);
    assert(snap.tv_nsec == 1000000);

    events = display->events().drain();
    bool found_ready = false;
    for (const auto& ev : events) {
        if (std::holds_alternative<ScreenCopyReadyEvent>(ev)) {
            const auto& r = std::get<ScreenCopyReadyEvent>(ev);
            if (r.frame.tv_sec == 42) {
                found_ready = true;
            }
        }
    }
    assert(found_ready);

    // 2. Failure flow
    auto frame2 = mgr->capture_output(*output, false);
    assert(frame2 != nullptr);
    display->roundtrip();

    server.send_screencopy_failed();
    display->roundtrip();

    assert(frame2->is_failed());
    assert(!frame2->is_ready());

    events = display->events().drain();
    bool found_failed = false;
    for (const auto& ev : events) {
        if (std::holds_alternative<ScreenCopyFailedEvent>(ev)) {
            found_failed = true;
        }
    }
    assert(found_failed);

    frame2.reset();
    frame.reset();
    buffer.reset();
    pool.reset();
    display.reset();
    server.stop();

    std::cout << "test_screencopy passed!" << std::endl;
    return 0;
}
