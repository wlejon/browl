#include "browl/display.h"
#include "browl/shm_pool.h"
#include "headless_compositor.h"

#include <cassert>
#include <cstring>
#include <iostream>

using namespace browl;
using namespace browl::test;

int main() {
    std::cout << "Running test_shm_pool..." << std::endl;

    HeadlessCompositor server;
    server.start();

    int client_fd = server.create_client_fd();
    assert(client_fd >= 0);

    auto display = Display::connect_to_fd(client_fd);
    assert(display != nullptr);

    constexpr size_t kInitialSize = 64 * 1024;
    auto pool = display->create_shm_pool(kInitialSize);
    assert(pool != nullptr);
    assert(pool->capacity() == kInitialSize);
    assert(pool->data() != nullptr);

    // Allocate buffer
    constexpr int32_t kWidth = 32;
    constexpr int32_t kHeight = 32;
    constexpr int32_t kStride = kWidth * 4;
    constexpr uint32_t kFormat = 0; // WL_SHM_FORMAT_ARGB8888

    auto buffer = pool->allocate_buffer(kWidth, kHeight, kStride, kFormat);
    assert(buffer != nullptr);
    assert(buffer->width() == kWidth);
    assert(buffer->height() == kHeight);
    assert(buffer->stride() == kStride);
    assert(buffer->data() != nullptr);
    assert(buffer->wl_buffer_ptr() != nullptr);
    assert(!buffer->is_busy());

    // Write pixel test pattern
    std::memset(buffer->data(), 0xAA, buffer->size());
    auto* bytes = static_cast<uint8_t*>(buffer->data());
    assert(bytes[0] == 0xAA);
    assert(bytes[buffer->size() - 1] == 0xAA);

    // Buffer release state
    buffer->set_busy(true);
    assert(buffer->is_busy());
    buffer->handle_release();
    assert(!buffer->is_busy());

    // Pool resize
    constexpr size_t kExpandedSize = 128 * 1024;
    assert(pool->resize(kExpandedSize));
    assert(pool->capacity() == kExpandedSize);

    // Allocate another buffer after resize
    auto buffer2 = pool->allocate_buffer(64, 64, 64 * 4, kFormat);
    assert(buffer2 != nullptr);
    assert(buffer2->width() == 64);
    assert(buffer2->height() == 64);

    buffer2.reset();
    buffer.reset();
    pool.reset();
    display.reset();
    server.stop();

    std::cout << "test_shm_pool passed!" << std::endl;
    return 0;
}
