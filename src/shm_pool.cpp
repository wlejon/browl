#include "browl/shm_pool.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-client.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>

namespace browl {

namespace {

static void buffer_release_callback(void* data, struct wl_buffer* /*wl_buffer*/) {
    auto* buffer = static_cast<ShmBuffer*>(data);
    if (buffer) {
        buffer->handle_release();
    }
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release_callback,
};

static int create_shm_fd(size_t size) {
    int fd = -1;
#if defined(MFD_CLOEXEC) && defined(MFD_ALLOW_SEALING)
    fd = memfd_create("browl-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
#endif
    if (fd < 0) {
        std::string name = "/browl-shm-" + std::to_string(getpid()) + "-" +
                           std::to_string(reinterpret_cast<uintptr_t>(&size));
        fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name.c_str());
        }
    }

    if (fd < 0) {
        return -1;
    }

    if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

}  // namespace

ShmBuffer::ShmBuffer(std::shared_ptr<ShmPool> pool, wl_buffer* buffer, void* data,
                     size_t offset, size_t size, int32_t width, int32_t height,
                     int32_t stride, uint32_t format)
    : pool_(std::move(pool)),
      buffer_(buffer),
      data_(data),
      offset_(offset),
      size_(size),
      width_(width),
      height_(height),
      stride_(stride),
      format_(format) {}

ShmBuffer::~ShmBuffer() {
    if (buffer_) {
        wl_buffer_destroy(buffer_);
    }
}

void ShmBuffer::handle_release() {
    busy_ = false;
}

ShmPool::ShmPool(wl_shm* shm, int fd, void* data, size_t size, wl_shm_pool* pool)
    : shm_(shm), fd_(fd), data_(data), size_(size), pool_(pool) {}

ShmPool::~ShmPool() {
    if (pool_) {
        wl_shm_pool_destroy(pool_);
    }
    if (data_ && data_ != MAP_FAILED) {
        munmap(data_, size_);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
}

std::shared_ptr<ShmPool> ShmPool::create(wl_shm* shm, size_t initial_size) {
    if (!shm || initial_size == 0) {
        return nullptr;
    }

    int fd = create_shm_fd(initial_size);
    if (fd < 0) {
        return nullptr;
    }

    void* data = mmap(nullptr, initial_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return nullptr;
    }

    wl_shm_pool* pool = wl_shm_create_pool(shm, fd, static_cast<int32_t>(initial_size));
    if (!pool) {
        munmap(data, initial_size);
        close(fd);
        return nullptr;
    }

    return std::shared_ptr<ShmPool>(new ShmPool(shm, fd, data, initial_size, pool));
}

bool ShmPool::resize(size_t new_size) {
    if (new_size <= size_) {
        return true;
    }

    if (ftruncate(fd_, static_cast<off_t>(new_size)) < 0) {
        return false;
    }

    void* new_data = mmap(nullptr, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (new_data == MAP_FAILED) {
        return false;
    }

    if (data_ && data_ != MAP_FAILED) {
        munmap(data_, size_);
    }

    data_ = new_data;
    size_ = new_size;
    wl_shm_pool_resize(pool_, static_cast<int32_t>(new_size));
    return true;
}

std::shared_ptr<ShmBuffer> ShmPool::create_buffer(size_t offset, int32_t width, int32_t height,
                                                  int32_t stride, uint32_t format) {
    size_t req_bytes = static_cast<size_t>(stride) * static_cast<size_t>(height);
    if (offset + req_bytes > size_) {
        return nullptr;
    }

    wl_buffer* wl_buf = wl_shm_pool_create_buffer(
        pool_, static_cast<int32_t>(offset), width, height, stride, format);
    if (!wl_buf) {
        return nullptr;
    }

    uint8_t* ptr = static_cast<uint8_t*>(data_) + offset;
    auto buf = std::make_shared<ShmBuffer>(
        shared_from_this(), wl_buf, ptr, offset, req_bytes, width, height, stride, format);

    wl_buffer_add_listener(wl_buf, &buffer_listener, buf.get());
    return buf;
}

std::shared_ptr<ShmBuffer> ShmPool::allocate_buffer(int32_t width, int32_t height,
                                                    int32_t stride, uint32_t format) {
    size_t req_bytes = static_cast<size_t>(stride) * static_cast<size_t>(height);
    size_t aligned_offset = (used_offset_ + 15) & ~static_cast<size_t>(15);

    if (aligned_offset + req_bytes > size_) {
        size_t needed = std::max(size_ * 2, aligned_offset + req_bytes);
        if (!resize(needed)) {
            return nullptr;
        }
    }

    used_offset_ = aligned_offset + req_bytes;
    return create_buffer(aligned_offset, width, height, stride, format);
}

}  // namespace browl
