#include "browl/output.h"

#include "browl/display.h"

#include "app_globals.h"
#include "xdg-output-unstable-v1-client-protocol.h"

#include <wayland-client.h>

#include <utility>

namespace browl {

namespace {

static void xdg_output_handle_logical_position(void* data, struct zxdg_output_v1* /*xdg_output*/,
                                               int32_t x, int32_t y) {
    auto* output = static_cast<Output*>(data);
    if (output) {
        output->handle_logical_position(x, y);
    }
}

static void xdg_output_handle_logical_size(void* data, struct zxdg_output_v1* /*xdg_output*/,
                                           int32_t width, int32_t height) {
    auto* output = static_cast<Output*>(data);
    if (output) {
        output->handle_logical_size(width, height);
    }
}

static void xdg_output_handle_done(void* data, struct zxdg_output_v1* /*xdg_output*/) {
    auto* output = static_cast<Output*>(data);
    if (output) {
        output->handle_xdg_done();
    }
}

static void xdg_output_handle_name(void* /*data*/, struct zxdg_output_v1* /*xdg_output*/,
                                   const char* /*name*/) {}

static void xdg_output_handle_description(void* /*data*/, struct zxdg_output_v1* /*xdg_output*/,
                                          const char* /*description*/) {}

static const struct zxdg_output_v1_listener xdg_output_listener = {
    .logical_position = xdg_output_handle_logical_position,
    .logical_size = xdg_output_handle_logical_size,
    .done = xdg_output_handle_done,
    .name = xdg_output_handle_name,
    .description = xdg_output_handle_description,
};

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
    if (xdg_output_) {
        zxdg_output_v1_destroy(xdg_output_);
        xdg_output_ = nullptr;
    }
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
    snap.description = description_;
    snap.logical = logical();
    return snap;
}

Rect Output::logical() const {
    if (has_logical_) {
        return logical_;
    }
    Rect r;
    r.x = geometry_.x;
    r.y = geometry_.y;
    int32_t w = current_mode_.width;
    int32_t h = current_mode_.height;
    // A transform that turns the output sideways swaps its logical axes.
    const auto t = static_cast<uint32_t>(transform_);
    if (t == 1 || t == 3 || t == 5 || t == 7) {
        std::swap(w, h);
    }
    const int32_t s = scale_ > 0 ? scale_ : 1;
    r.width = w / s;
    r.height = h / s;
    return r;
}

void Output::attach_xdg_output(zxdg_output_manager_v1* manager) {
    if (xdg_output_ || !manager || !wl_output_) {
        return;
    }
    xdg_output_ = zxdg_output_manager_v1_get_xdg_output(manager, wl_output_);
    if (xdg_output_) {
        zxdg_output_v1_add_listener(xdg_output_, &xdg_output_listener, this);
    }
}

void Output::handle_logical_position(int32_t x, int32_t y) {
    logical_.x = x;
    logical_.y = y;
    has_logical_ = true;
}

void Output::handle_logical_size(int32_t width, int32_t height) {
    logical_.width = width;
    logical_.height = height;
    has_logical_ = true;
}

void Output::handle_xdg_done() {
    // zxdg_output_v1 v3 sends no done of its own (wl_output.done covers it);
    // an older one's done is the end of an update like wl_output.done.
    if (xdg_output_ && zxdg_output_v1_get_version(xdg_output_) < 3) {
        handle_done();
    }
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
        for (auto& m : modes_) {
            m.current = false;
        }
    }

    // A compositor re-sends a mode when it becomes current: update the known
    // entry rather than listing the mode twice.
    for (auto& m : modes_) {
        if (m.width == width && m.height == height && m.refresh_mhz == refresh) {
            m.current = mode.current;
            m.preferred = m.preferred || mode.preferred;
            return;
        }
    }
    modes_.push_back(mode);
}

void Output::handle_done() {
    if (display_) {
        display_->events().push(OutputChangedEvent{snapshot()});
        // A window on this output may take its scale from it.
        display_->app_globals().output_changed(wl_output_);
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
