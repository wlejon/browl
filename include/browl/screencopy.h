#pragma once

#include "browl/events.h"
#include "browl/types.h"

#include <memory>

struct zwlr_screencopy_manager_v1;
struct zwlr_screencopy_frame_v1;
struct wl_buffer;

namespace browl {

class Display;
class Output;
class ScreenCopyManager;

class ScreenCopyFrame {
public:
    ScreenCopyFrame(zwlr_screencopy_frame_v1* frame, Display* display);
    ~ScreenCopyFrame();

    ScreenCopyFrame(const ScreenCopyFrame&) = delete;
    ScreenCopyFrame& operator=(const ScreenCopyFrame&) = delete;

    ScreenCopyFrameSnapshot snapshot() const;
    bool is_ready() const { return snapshot_.status == ScreenCopyFrameSnapshot::Status::Ready; }
    bool is_failed() const { return snapshot_.status == ScreenCopyFrameSnapshot::Status::Failed; }

    uint32_t format() const { return snapshot_.format; }
    uint32_t width() const { return snapshot_.width; }
    uint32_t height() const { return snapshot_.height; }
    uint32_t stride() const { return snapshot_.stride; }
    uint32_t flags() const { return snapshot_.flags; }
    const Rect& damage() const { return snapshot_.damage; }

    void copy(wl_buffer* buffer);
    void copy_with_damage(wl_buffer* buffer);

    zwlr_screencopy_frame_v1* zwlr_frame_ptr() const { return frame_; }

    // Internal listener callbacks
    void handle_buffer(uint32_t format, uint32_t width, uint32_t height, uint32_t stride);
    void handle_flags(uint32_t flags);
    void handle_ready(uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec);
    void handle_failed();
    void handle_damage(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    void handle_buffer_done();

private:
    zwlr_screencopy_frame_v1* frame_ = nullptr;
    Display* display_ = nullptr;

    ScreenCopyFrameSnapshot snapshot_;
};

class ScreenCopyManager {
public:
    ScreenCopyManager(zwlr_screencopy_manager_v1* manager, Display* display);
    ~ScreenCopyManager();

    ScreenCopyManager(const ScreenCopyManager&) = delete;
    ScreenCopyManager& operator=(const ScreenCopyManager&) = delete;

    zwlr_screencopy_manager_v1* zwlr_manager_ptr() const { return manager_; }

    std::unique_ptr<ScreenCopyFrame> capture_output(Output& output, bool overlay_cursor = true);
    std::unique_ptr<ScreenCopyFrame> capture_output_region(Output& output, const Rect& region,
                                                          bool overlay_cursor = true);
    void detach();

private:
    zwlr_screencopy_manager_v1* manager_ = nullptr;
    Display* display_ = nullptr;
};

}  // namespace browl
