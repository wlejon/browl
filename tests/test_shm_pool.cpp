// ShmPool and ShmBuffer: the memory a wl_buffer describes. Oracle: the
// pool's own file. Bytes written through one buffer must be in the shared
// mapping at that buffer's offset, buffers must not overlap, and a buffer
// made before the pool grows must still be writable after (the old mapping
// stays valid). Runs against the in-process test-double compositor, which
// only needs to accept the wl_shm requests.
#include "browl/display.h"
#include "browl/shm_pool.h"
#include "fake_session.h"

#include <wayland-client.h>

#include <cstdint>
#include <cstring>

using namespace browl;

namespace {

void run() {
    bstest::FakeSession s;
    REQUIRE(s.display != nullptr);
    Display& display = *s.display;

    CHECK(display.create_shm_pool(0) == nullptr);

    constexpr size_t kInitial = 64 * 1024;
    auto pool = display.create_shm_pool(kInitial);
    REQUIRE(pool != nullptr);
    CHECK_EQ(pool->capacity(), kInitial);
    REQUIRE(pool->data() != nullptr);

    constexpr int32_t kW = 32, kH = 32, kStride = kW * 4;
    constexpr uint32_t kFormat = WL_SHM_FORMAT_ARGB8888;
    auto a = pool->allocate_buffer(kW, kH, kStride, kFormat);
    REQUIRE(a != nullptr);
    CHECK_EQ(a->width(), kW);
    CHECK_EQ(a->height(), kH);
    CHECK_EQ(a->stride(), kStride);
    CHECK_EQ(a->format(), kFormat);
    CHECK_EQ(a->size(), size_t(kStride) * kH);
    CHECK(a->wl_buffer_ptr() != nullptr);
    CHECK(!a->is_busy());

    auto b = pool->allocate_buffer(kW, kH, kStride, kFormat);
    REQUIRE(b != nullptr);
    // Sub-allocated back to back (16-byte aligned), never overlapping.
    auto* base = static_cast<uint8_t*>(pool->data());
    const auto off_a = static_cast<uint8_t*>(a->data()) - base;
    const auto off_b = static_cast<uint8_t*>(b->data()) - base;
    CHECK(off_b >= off_a + static_cast<ptrdiff_t>(a->size()));
    CHECK_EQ(off_b % 16, ptrdiff_t(0));

    std::memset(a->data(), 0xAA, a->size());
    std::memset(b->data(), 0x55, b->size());
    CHECK_EQ(base[off_a], uint8_t(0xAA));
    CHECK_EQ(base[off_a + a->size() - 1], uint8_t(0xAA));
    CHECK_EQ(base[off_b], uint8_t(0x55));

    // Growing past the capacity remaps the pool. The earlier buffers' memory
    // is the same file pages, readable at the new mapping's offsets, and
    // their old pointers stay writable.
    auto big = pool->allocate_buffer(128, 128, 128 * 4, kFormat);
    REQUIRE(big != nullptr);
    CHECK(pool->capacity() >= kInitial + size_t(128 * 4 * 128));
    auto* nbase = static_cast<uint8_t*>(pool->data());
    CHECK_EQ(nbase[off_a], uint8_t(0xAA));
    CHECK_EQ(nbase[off_b], uint8_t(0x55));
    static_cast<uint8_t*>(a->data())[0] = 0x11;  // the pre-resize pointer
    CHECK_EQ(nbase[off_a], uint8_t(0x11));
    const auto off_big = static_cast<uint8_t*>(big->data()) - nbase;
    CHECK(off_big >= off_b + static_cast<ptrdiff_t>(b->size()));

    CHECK(pool->resize(pool->capacity()));  // no-op
    CHECK(!pool->resize(size_t(INT32_MAX) + 1));

    // Out-of-range and degenerate requests are refused.
    CHECK(pool->create_buffer(pool->capacity(), 1, 1, 4, kFormat) == nullptr);
    CHECK(pool->create_buffer(0, kW, kH, -kStride, kFormat) == nullptr);
    CHECK(pool->create_buffer(0, 0, kH, kStride, kFormat) == nullptr);
    CHECK(pool->create_buffer(0, kW, kH, kStride, kFormat) != nullptr);

    // Release bookkeeping.
    a->set_busy(true);
    CHECK(a->is_busy());
    a->handle_release();
    CHECK(!a->is_busy());
    CHECK(display.roundtrip() >= 0);
}

}  // namespace

int main() {
    run();
    return bstest::finish("test_shm_pool");
}
