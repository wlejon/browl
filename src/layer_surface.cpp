#include "browl/layer_surface.h"

#include "browl/display.h"

#include "app_globals.h"
#include "browl/popup.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <wayland-client.h>

namespace browl {

namespace {

static void layer_handle_configure(void* data, struct zwlr_layer_surface_v1* /*surface*/,
                                   uint32_t serial, uint32_t width, uint32_t height) {
    auto* layer = static_cast<LayerSurface*>(data);
    if (layer) {
        layer->handle_configure(serial, width, height);
    }
}

static void layer_handle_closed(void* data, struct zwlr_layer_surface_v1* /*surface*/) {
    auto* layer = static_cast<LayerSurface*>(data);
    if (layer) {
        layer->handle_closed();
    }
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
    .configure = layer_handle_configure,
    .closed = layer_handle_closed,
};

}  // namespace

LayerSurface::LayerSurface(SurfaceId id, wl_surface* surface,
                           zwlr_layer_surface_v1* layer_surf,
                           const LayerSurfaceConfig& config, Display* display)
    : id_(id), surface_(surface), layer_surf_(layer_surf), display_(display) {
    snapshot_.id = id_;
    snapshot_.layer = config.layer;
    snapshot_.anchor = config.anchor;
    snapshot_.margins = config.margins;
    snapshot_.exclusive_zone = config.exclusive_zone;
    snapshot_.keyboard_interactivity = config.keyboard_interactivity;
    if (display_) {
        display_->app_globals().register_surface(surface_, id_);
    }

    if (layer_surf_) {
        zwlr_layer_surface_v1_add_listener(layer_surf_, &layer_listener, this);

        if (config.size.width > 0 || config.size.height > 0) {
            set_size(static_cast<uint32_t>(config.size.width),
                     static_cast<uint32_t>(config.size.height));
        }
        if (config.anchor != Anchor::None) {
            set_anchor(config.anchor);
        }
        set_margin(config.margins);
        if (config.exclusive_zone != 0) {
            set_exclusive_zone(config.exclusive_zone);
        }
        if (config.keyboard_interactivity != KeyboardInteractivity::None) {
            set_keyboard_interactivity(config.keyboard_interactivity);
        }
    }
}

LayerSurface::~LayerSurface() {
    if (display_ && surface_) {
        display_->app_globals().unregister_surface(surface_);
    }
    if (layer_surf_) {
        zwlr_layer_surface_v1_destroy(layer_surf_);
    }
    if (surface_) {
        wl_surface_destroy(surface_);
    }
}

LayerSurfaceSnapshot LayerSurface::snapshot() const {
    return snapshot_;
}

void LayerSurface::set_size(uint32_t width, uint32_t height) {
    if (layer_surf_) {
        zwlr_layer_surface_v1_set_size(layer_surf_, width, height);
    }
}

void LayerSurface::set_anchor(Anchor anchor) {
    snapshot_.anchor = anchor;
    if (layer_surf_) {
        zwlr_layer_surface_v1_set_anchor(layer_surf_, static_cast<uint32_t>(anchor));
    }
}

void LayerSurface::set_margin(const Margins& margins) {
    snapshot_.margins = margins;
    if (layer_surf_) {
        zwlr_layer_surface_v1_set_margin(layer_surf_, margins.top, margins.right,
                                         margins.bottom, margins.left);
    }
}

void LayerSurface::set_exclusive_zone(int32_t zone) {
    snapshot_.exclusive_zone = zone;
    if (layer_surf_) {
        zwlr_layer_surface_v1_set_exclusive_zone(layer_surf_, zone);
    }
}

void LayerSurface::set_keyboard_interactivity(KeyboardInteractivity interactivity) {
    snapshot_.keyboard_interactivity = interactivity;
    if (layer_surf_) {
        zwlr_layer_surface_v1_set_keyboard_interactivity(layer_surf_,
                                                         static_cast<uint32_t>(interactivity));
    }
}

void LayerSurface::set_layer(Layer layer) {
    snapshot_.layer = layer;
    if (layer_surf_) {
        zwlr_layer_surface_v1_set_layer(layer_surf_, static_cast<uint32_t>(layer));
    }
}

void LayerSurface::ack_configure(uint32_t serial) {
    if (layer_surf_) {
        zwlr_layer_surface_v1_ack_configure(layer_surf_, serial);
    }
}

void LayerSurface::commit() {
    if (surface_) {
        wl_surface_commit(surface_);
    }
}

void LayerSurface::attach_buffer(wl_buffer* buffer, int32_t x, int32_t y) {
    if (surface_) {
        wl_surface_attach(surface_, buffer, x, y);
    }
}

void LayerSurface::damage(int32_t x, int32_t y, int32_t width, int32_t height) {
    if (surface_) {
        if (wl_surface_get_version(surface_) >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION) {
            wl_surface_damage_buffer(surface_, x, y, width, height);
        } else {
            wl_surface_damage(surface_, x, y, width, height);
        }
    }
}

std::unique_ptr<Popup> LayerSurface::create_popup(Positioner& positioner) {
    if (!display_ || !display_->has_xdg_shell()) {
        return nullptr;
    }

    wl_surface* popup_surface = display_->create_surface();
    if (!popup_surface) {
        return nullptr;
    }

    struct xdg_surface* xdg_surf =
        xdg_wm_base_get_xdg_surface(display_->xdg_wm_base_ptr(), popup_surface);
    if (!xdg_surf) {
        wl_surface_destroy(popup_surface);
        return nullptr;
    }

    // Pass nullptr as parent to get_popup since parent is a layer_surface,
    // which is then associated using zwlr_layer_surface_v1_get_popup.
    struct xdg_popup* popup =
        xdg_surface_get_popup(xdg_surf, nullptr, positioner.xdg_positioner_ptr());
    if (!popup) {
        xdg_surface_destroy(xdg_surf);
        wl_surface_destroy(popup_surface);
        return nullptr;
    }

    if (layer_surf_) {
        zwlr_layer_surface_v1_get_popup(layer_surf_, popup);
    }

    SurfaceId id = display_->next_surface_id();
    return std::make_unique<Popup>(id, popup_surface, xdg_surf, popup, display_);
}

void LayerSurface::handle_configure(uint32_t serial, uint32_t width, uint32_t height) {
    snapshot_.configured_serial = serial;
    snapshot_.configured_size = {static_cast<int32_t>(width), static_cast<int32_t>(height)};

    if (display_) {
        display_->events().push(LayerConfigureEvent{id_, serial, width, height});
    }
}

void LayerSurface::handle_closed() {
    snapshot_.closed = true;

    if (display_) {
        display_->events().push(LayerClosedEvent{id_});
    }
}

}  // namespace browl
