#include "browl/output.h"

#include "browl/display.h"

#include <wayland-client.h>

namespace browl {

namespace {

static void output_handle_geometry(void* data, struct wl_output* /*wl_output*/,
                                   int32_t x, int32_t y, int32_t physical_width,
                                   int32_t physical_height, int32_t subpixel,
                                   const char* make, const char* model, int32_t transform) {
    auto* output = static_cast<Output*>(data);
    if (output) {
        output->handle_geometry(x, y, physical_width, physical_height, subpixel,
                               make, model, transform);
    }
}

static void output_handle_mode(void* data, struct wl_output* /*wl_output*/,
                               uint32_t flags, int32_t width, int32_t height,
                               int32_t refresh) {
    auto* output = static_cast<Output*>(data);
    if (output) {
        output->handle_mode(flags, width, height, refresh);
    }
}

static void output_handle_done(void* data, struct wl_output* /*wl_output*/) {
    auto* output = static_cast<Output*>(data);
    if (output) {
        output->handle_done();
    }
}

static void output_handle_scale(void* data, struct wl_output* /*wl_output*/,
                                int32_t factor) {
    auto* output = static_cast<Output*>(data);
    if (output) {
        output->handle_scale(factor);
    }
}

static void output_handle_name(void* data, struct wl_output* /*wl_output*/,
                               const char* name) {
    auto* output = static_cast<Output*>(data);
    if (output) {
        output->handle_name(name);
    }
}

static void output_handle_description(void* data, struct wl_output* /*wl_output*/,
                                      const char* description) {
    auto* output = static_cast<Output*>(data);
    if (output) {
        output->handle_description(description);
    }
}

static const struct wl_output_listener output_listener = {
    .geometry = output_handle_geometry,
    .mode = output_handle_mode,
    .done = output_handle_done,
    .scale = output_handle_scale,
    .name = output_handle_name,
    .description = output_handle_description,
};

}  // namespace

Output::Output(OutputId id, wl_output* wl_output, Display* display)
    : id_(id), wl_output_(wl_output), display_(display) {
    if (wl_output_) {
        wl_output_add_listener(wl_output_, &output_listener, this);
    }
}

Output::~Output() {
    detach();
}

void Output::detach() {
    if (wl_output_) {
        if (wl_output_get_version(wl_output_) >= WL_OUTPUT_RELEASE_SINCE_VERSION) {
            wl_output_release(wl_output_);
        } else {
            wl_output_destroy(wl_output_);
        }
        wl_output_ = nullptr;
    }
}

OutputSnapshot Output::snapshot() const {
    OutputSnapshot snap;
    snap.id = id_;
    snap.name = name_;
    snap.make = make_;
    snap.model = model_;
    snap.geometry = geometry_;
    snap.scale = scale_;
    snap.transform = transform_;
    snap.physical_size_mm = physical_size_mm_;
    snap.modes = modes_;
    snap.current_mode = current_mode_;
    return snap;
}

void Output::handle_geometry(int32_t x, int32_t y, int32_t physical_width,
                             int32_t physical_height, int32_t subpixel,
                             const char* make, const char* model, int32_t transform) {
    geometry_.x = x;
    geometry_.y = y;
    physical_size_mm_.width = physical_width;
    physical_size_mm_.height = physical_height;
    subpixel_ = subpixel;
    if (make) {
        make_ = make;
    }
    if (model) {
        model_ = model;
    }
    transform_ = static_cast<OutputTransform>(transform);
}

void Output::handle_mode(uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    OutputMode mode;
    mode.width = width;
    mode.height = height;
    mode.refresh_mhz = refresh;
    mode.current = (flags & WL_OUTPUT_MODE_CURRENT) != 0;
    mode.preferred = (flags & WL_OUTPUT_MODE_PREFERRED) != 0;

    if (mode.current) {
        current_mode_ = mode;
        geometry_.width = width;
        geometry_.height = height;
    }

    modes_.push_back(mode);
}

void Output::handle_done() {
    if (display_) {
        display_->events().push(OutputChangedEvent{snapshot()});
    }
}

void Output::handle_scale(int32_t factor) {
    scale_ = factor;
}

void Output::handle_name(const char* name) {
    if (name) {
        name_ = name;
    }
}

void Output::handle_description(const char* description) {
    if (description) {
        description_ = description;
    }
}

}  // namespace browl
