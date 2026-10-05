#pragma once

#include "browl/events.h"
#include "browl/types.h"

#include <memory>
#include <string>

struct wl_surface;
struct wl_buffer;
struct zwlr_layer_surface_v1;

namespace browl {

class Display;
class Output;
class Positioner;
class Popup;

struct LayerSurfaceConfig {
    std::string name_space = "browl";
    Layer layer = Layer::Top;
    Anchor anchor = Anchor::None;
    Margins margins;
    Size size = {0, 0};
    int32_t exclusive_zone = 0;
    KeyboardInteractivity keyboard_interactivity = KeyboardInteractivity::None;
    Output* output = nullptr;
};

class LayerSurface {
public:
    LayerSurface(SurfaceId id, wl_surface* surface, zwlr_layer_surface_v1* layer_surf,
                 const LayerSurfaceConfig& config, Display* display);
    ~LayerSurface();

    LayerSurface(const LayerSurface&) = delete;
    LayerSurface& operator=(const LayerSurface&) = delete;

    SurfaceId id() const { return id_; }
    LayerSurfaceSnapshot snapshot() const;

    void set_size(uint32_t width, uint32_t height);
    void set_anchor(Anchor anchor);
    void set_margin(const Margins& margins);
    void set_exclusive_zone(int32_t zone);
    void set_keyboard_interactivity(KeyboardInteractivity interactivity);
    void set_layer(Layer layer);

    void ack_configure(uint32_t serial);
    void commit();
    void attach_buffer(wl_buffer* buffer, int32_t x = 0, int32_t y = 0);
    void damage(int32_t x, int32_t y, int32_t width, int32_t height);

    std::unique_ptr<Popup> create_popup(Positioner& positioner);

    wl_surface* wl_surface_ptr() const { return surface_; }
    zwlr_layer_surface_v1* zwlr_layer_surface_ptr() const { return layer_surf_; }

    // Internal listener callbacks
    void handle_configure(uint32_t serial, uint32_t width, uint32_t height);
    void handle_closed();

private:
    SurfaceId id_ = kNoSurface;
    wl_surface* surface_ = nullptr;
    zwlr_layer_surface_v1* layer_surf_ = nullptr;
    Display* display_ = nullptr;

    LayerSurfaceSnapshot snapshot_;
};

}  // namespace browl
