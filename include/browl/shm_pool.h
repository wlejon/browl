#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct wl_shm;
struct wl_shm_pool;
struct wl_buffer;

namespace browl {

class ShmPool;

class ShmBuffer {
public:
    ShmBuffer(std::shared_ptr<ShmPool> pool, wl_buffer* buffer, void* data,
              size_t offset, size_t size, int32_t width, int32_t height,
              int32_t stride, uint32_t format);
    ~ShmBuffer();

    ShmBuffer(const ShmBuffer&) = delete;
    ShmBuffer& operator=(const ShmBuffer&) = delete;

    wl_buffer* wl_buffer_ptr() const { return buffer_; }
    void* data() const { return data_; }
    size_t size() const { return size_; }
    int32_t width() const { return width_; }
    int32_t height() const { return height_; }
    int32_t stride() const { return stride_; }
    uint32_t format() const { return format_; }

    bool is_busy() const { return busy_; }
    void set_busy(bool busy) { busy_ = busy; }

    // Internal release listener
    void handle_release();

private:
    std::shared_ptr<ShmPool> pool_;
    wl_buffer* buffer_ = nullptr;
    void* data_ = nullptr;
    size_t offset_ = 0;
    size_t size_ = 0;
    int32_t width_ = 0;
    int32_t height_ = 0;
    int32_t stride_ = 0;
    uint32_t format_ = 0;
    bool busy_ = false;
};

class ShmPool : public std::enable_shared_from_this<ShmPool> {
public:
    static std::shared_ptr<ShmPool> create(wl_shm* shm, size_t initial_size);
    ~ShmPool();

    ShmPool(const ShmPool&) = delete;
    ShmPool& operator=(const ShmPool&) = delete;

    bool resize(size_t new_size);
    size_t capacity() const { return size_; }
    void* data() const { return data_; }

    std::shared_ptr<ShmBuffer> create_buffer(size_t offset, int32_t width, int32_t height,
                                            int32_t stride, uint32_t format);

    std::shared_ptr<ShmBuffer> allocate_buffer(int32_t width, int32_t height,
                                              int32_t stride, uint32_t format);

private:
    ShmPool(wl_shm* shm, int fd, void* data, size_t size, wl_shm_pool* pool);

    wl_shm* shm_ = nullptr;
    int fd_ = -1;
    void* data_ = nullptr;
    size_t size_ = 0;
    size_t used_offset_ = 0;
    wl_shm_pool* pool_ = nullptr;
};

}  // namespace browl
