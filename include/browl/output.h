#pragma once

#include "browl/events.h"
#include "browl/types.h"

#include <memory>
#include <string>
#include <vector>

struct wl_output;
struct zxdg_output_manager_v1;
struct zxdg_output_v1;

namespace browl {

class Display;

class Output {
public:
    Output(OutputId id, wl_output* wl_output, Display* display);
    ~Output();

    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;

    OutputId id() const { return id_; }
    wl_output* wl_output_ptr() const { return wl_output_; }

    OutputSnapshot snapshot() const;

    const std::string& name() const { return name_; }
    const std::string& make() const { return make_; }
    const std::string& model() const { return model_; }
    const Rect& geometry() const { return geometry_; }
    int32_t scale() const { return scale_; }
    OutputTransform transform() const { return transform_; }
    const Size& physical_size_mm() const { return physical_size_mm_; }
    const std::vector<OutputMode>& modes() const { return modes_; }
    const OutputMode& current_mode() const { return current_mode_; }
    const std::string& description() const { return description_; }
    /// The output's place and size in the compositor's logical space
    /// (zxdg_output_v1 when the compositor has it; else derived from the
    /// position, current mode, scale and transform).
    Rect logical() const;

    // Internal: follow the output's zxdg_output_v1 (logical geometry).
    void attach_xdg_output(zxdg_output_manager_v1* manager);
    void handle_logical_position(int32_t x, int32_t y);
    void handle_logical_size(int32_t width, int32_t height);
    void handle_xdg_done();

    // Internal callbacks from wl_output_listener
    void handle_geometry(int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
                         int32_t subpixel, const char* make, const char* model, int32_t transform);
    void handle_mode(uint32_t flags, int32_t width, int32_t height, int32_t refresh);
    void handle_done();
    void handle_scale(int32_t factor);
    void handle_name(const char* name);
    void handle_description(const char* description);
    void detach();

private:
    OutputId id_ = kNoOutput;
    wl_output* wl_output_ = nullptr;
    Display* display_ = nullptr;

    std::string name_;
    std::string make_;
    std::string model_;
    std::string description_;
    Rect geometry_;
    Size physical_size_mm_;
    int32_t subpixel_ = 0;
    OutputTransform transform_ = OutputTransform::Normal;
    int32_t scale_ = 1;
    std::vector<OutputMode> modes_;
    OutputMode current_mode_;
    zxdg_output_v1* xdg_output_ = nullptr;
    Rect logical_;
    bool has_logical_ = false;
};

}  // namespace browl
