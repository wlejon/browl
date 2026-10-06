// ScreenCopyManager and ScreenCopyFrame against the in-process test-double
// compositor (fake_session.h): buffer negotiation, copy, ready (with the
// 64-bit timestamp split across two words) and failure. Real pixels are
// captured in test_sway.
#include "browl/display.h"
#include "browl/output.h"
#include "browl/screencopy.h"
#include "browl/shm_pool.h"
#include "fake_session.h"

#include <wayland-client.h>

using namespace browl;
using bstest::find_event;

namespace {

void run() {
    bstest::FakeSession s;
    REQUIRE(s.display != nullptr);
    Display& display = *s.display;

    auto mgr = display.screencopy_manager();
    REQUIRE(mgr != nullptr);
    CHECK(display.screencopy_manager() == mgr);
    CHECK(mgr->zwlr_manager_ptr() != nullptr);
    auto output = display.default_output();
    REQUIRE(output != nullptr);

    auto frame = mgr->capture_output(*output, true);
    REQUIRE(frame != nullptr);
    CHECK(frame->zwlr_frame_ptr() != nullptr);
    CHECK(!frame->is_ready());
    CHECK(!frame->is_failed());
    CHECK(display.roundtrip() >= 0);
    CHECK(s.server.screencopy_frame_created());

    constexpr uint32_t kFormat = WL_SHM_FORMAT_XRGB8888;
    constexpr uint32_t kWidth = 1920, kHeight = 1080, kStride = 1920 * 4;
    s.server.send_screencopy_buffer(kFormat, kWidth, kHeight, kStride);
    CHECK(display.roundtrip() >= 0);
    CHECK_EQ(frame->format(), kFormat);
    CHECK_EQ(frame->width(), kWidth);
    CHECK_EQ(frame->height(), kHeight);
    CHECK_EQ(frame->stride(), kStride);
    auto events = display.events().drain();
    const auto* buf = find_event<ScreenCopyBufferEvent>(events);
    REQUIRE(buf != nullptr);
    CHECK_EQ(buf->format, kFormat);
    CHECK_EQ(buf->width, kWidth);
    CHECK_EQ(buf->height, kHeight);
    CHECK_EQ(buf->stride, kStride);

    auto pool = display.create_shm_pool(size_t(kStride) * kHeight);
    REQUIRE(pool != nullptr);
    auto buffer = pool->allocate_buffer(kWidth, kHeight, kStride, kFormat);
    REQUIRE(buffer != nullptr);
    frame->copy(buffer->wl_buffer_ptr());
    CHECK(display.roundtrip() >= 0);
    CHECK(s.server.screencopy_copy_received());

    s.server.send_screencopy_ready(42, 1000000);
    CHECK(display.roundtrip() >= 0);
    CHECK(frame->is_ready());
    CHECK(!frame->is_failed());
    const auto snap = frame->snapshot();
    CHECK_EQ(snap.status, ScreenCopyFrameSnapshot::Status::Ready);
    CHECK_EQ(snap.tv_sec, uint64_t(42));
    CHECK_EQ(snap.tv_nsec, 1000000u);
    events = display.events().drain();
    const auto* ready = find_event<ScreenCopyReadyEvent>(events);
    REQUIRE(ready != nullptr);
    CHECK_EQ(ready->frame.tv_sec, uint64_t(42));
    CHECK_EQ(ready->frame.width, kWidth);

    auto frame2 = mgr->capture_output_region(*output, Rect{10, 20, 30, 40}, false);
    REQUIRE(frame2 != nullptr);
    CHECK(display.roundtrip() >= 0);
    s.server.send_screencopy_failed();
    CHECK(display.roundtrip() >= 0);
    CHECK(frame2->is_failed());
    CHECK(!frame2->is_ready());
    events = display.events().drain();
    const auto* failed = find_event<ScreenCopyFailedEvent>(events);
    REQUIRE(failed != nullptr);
    CHECK(!failed->reason.empty());
}

}  // namespace

int main() {
    run();
    return bstest::finish("test_screencopy");
}
