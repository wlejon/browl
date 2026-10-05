#include "browl/screencopy.h"

#include "browl/display.h"
#include "browl/output.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"

#include <wayland-client.h>

namespace browl {

namespace {

static void frame_handle_buffer(void* data, struct zwlr_screencopy_frame_v1* /*frame*/,
                                uint32_t format, uint32_t width, uint32_t height,
                                uint32_t stride) {
    auto* frame = static_cast<ScreenCopyFrame*>(data);
    if (frame) {
        frame->handle_buffer(format, width, height, stride);
    }
}

static void frame_handle_flags(void* data, struct zwlr_screencopy_frame_v1* /*frame*/,
                               uint32_t flags) {
    auto* frame = static_cast<ScreenCopyFrame*>(data);
    if (frame) {
        frame->handle_flags(flags);
    }
}

static void frame_handle_ready(void* data, struct zwlr_screencopy_frame_v1* /*frame*/,
                               uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    auto* frame = static_cast<ScreenCopyFrame*>(data);
    if (frame) {
        frame->handle_ready(tv_sec_hi, tv_sec_lo, tv_nsec);
    }
}

static void frame_handle_failed(void* data, struct zwlr_screencopy_frame_v1* /*frame*/) {
    auto* frame = static_cast<ScreenCopyFrame*>(data);
    if (frame) {
        frame->handle_failed();
    }
}

static void frame_handle_damage(void* data, struct zwlr_screencopy_frame_v1* /*frame*/,
                                uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    auto* frame = static_cast<ScreenCopyFrame*>(data);
    if (frame) {
        frame->handle_damage(x, y, width, height);
    }
}

static void frame_handle_linux_dmabuf(void* /*data*/, struct zwlr_screencopy_frame_v1* /*frame*/,
                                      uint32_t /*format*/, uint32_t /*width*/, uint32_t /*height*/) {
}

static void frame_handle_buffer_done(void* data, struct zwlr_screencopy_frame_v1* /*frame*/) {
    auto* frame = static_cast<ScreenCopyFrame*>(data);
    if (frame) {
        frame->handle_buffer_done();
    }
}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
    .buffer = frame_handle_buffer,
    .flags = frame_handle_flags,
    .ready = frame_handle_ready,
    .failed = frame_handle_failed,
    .damage = frame_handle_damage,
    .linux_dmabuf = frame_handle_linux_dmabuf,
    .buffer_done = frame_handle_buffer_done,
};

}  // namespace

ScreenCopyFrame::ScreenCopyFrame(zwlr_screencopy_frame_v1* frame, Display* display)
    : frame_(frame), display_(display) {
    if (frame_) {
        zwlr_screencopy_frame_v1_add_listener(frame_, &frame_listener, this);
    }
}

ScreenCopyFrame::~ScreenCopyFrame() {
    if (frame_) {
        zwlr_screencopy_frame_v1_destroy(frame_);
    }
}

ScreenCopyFrameSnapshot ScreenCopyFrame::snapshot() const {
    return snapshot_;
}

void ScreenCopyFrame::copy(wl_buffer* buffer) {
    if (frame_) {
        zwlr_screencopy_frame_v1_copy(frame_, buffer);
    }
}

void ScreenCopyFrame::copy_with_damage(wl_buffer* buffer) {
    if (frame_ &&
        zwlr_screencopy_frame_v1_get_version(frame_) >=
            ZWLR_SCREENCOPY_FRAME_V1_COPY_WITH_DAMAGE_SINCE_VERSION) {
        zwlr_screencopy_frame_v1_copy_with_damage(frame_, buffer);
    } else if (frame_) {
        zwlr_screencopy_frame_v1_copy(frame_, buffer);
    }
}

void ScreenCopyFrame::handle_buffer(uint32_t format, uint32_t width, uint32_t height,
                                    uint32_t stride) {
    snapshot_.format = format;
    snapshot_.width = width;
    snapshot_.height = height;
    snapshot_.stride = stride;

    if (display_) {
        display_->events().push(ScreenCopyBufferEvent{format, width, height, stride});
    }
}

void ScreenCopyFrame::handle_flags(uint32_t flags) {
    snapshot_.flags = flags;
}

void ScreenCopyFrame::handle_ready(uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    snapshot_.status = ScreenCopyFrameSnapshot::Status::Ready;
    snapshot_.tv_sec = (static_cast<uint64_t>(tv_sec_hi) << 32) | tv_sec_lo;
    snapshot_.tv_nsec = tv_nsec;

    if (display_) {
        display_->events().push(ScreenCopyReadyEvent{snapshot_});
    }
}

void ScreenCopyFrame::handle_failed() {
    snapshot_.status = ScreenCopyFrameSnapshot::Status::Failed;

    if (display_) {
        display_->events().push(ScreenCopyFailedEvent{"Compositor reported screencopy failure"});
    }
}

void ScreenCopyFrame::handle_damage(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    snapshot_.damage = {static_cast<int32_t>(x), static_cast<int32_t>(y),
                        static_cast<int32_t>(width), static_cast<int32_t>(height)};
}

void ScreenCopyFrame::handle_buffer_done() {
}

ScreenCopyManager::ScreenCopyManager(zwlr_screencopy_manager_v1* manager, Display* display)
    : manager_(manager), display_(display) {}

ScreenCopyManager::~ScreenCopyManager() {
    detach();
}

void ScreenCopyManager::detach() {
    if (manager_) {
        zwlr_screencopy_manager_v1_destroy(manager_);
        manager_ = nullptr;
    }
}

std::unique_ptr<ScreenCopyFrame> ScreenCopyManager::capture_output(Output& output,
                                                                  bool overlay_cursor) {
    if (!manager_ || !display_) {
        return nullptr;
    }

    struct zwlr_screencopy_frame_v1* frame =
        zwlr_screencopy_manager_v1_capture_output(manager_, overlay_cursor ? 1 : 0,
                                                  output.wl_output_ptr());
    if (!frame) {
        return nullptr;
    }

    return std::make_unique<ScreenCopyFrame>(frame, display_);
}

std::unique_ptr<ScreenCopyFrame> ScreenCopyManager::capture_output_region(Output& output,
                                                                         const Rect& region,
                                                                         bool overlay_cursor) {
    if (!manager_ || !display_) {
        return nullptr;
    }

    struct zwlr_screencopy_frame_v1* frame =
        zwlr_screencopy_manager_v1_capture_output_region(
            manager_, overlay_cursor ? 1 : 0, output.wl_output_ptr(),
            region.x, region.y, region.width, region.height);
    if (!frame) {
        return nullptr;
    }

    return std::make_unique<ScreenCopyFrame>(frame, display_);
}

}  // namespace browl
