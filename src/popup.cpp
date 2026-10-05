#include "browl/popup.h"

#include "browl/display.h"
#include "browl/seat.h"
#include "xdg-shell-client-protocol.h"

#include <wayland-client.h>

namespace browl {

namespace {

static void xdg_surface_handle_configure(void* data, struct xdg_surface* /*surface*/,
                                         uint32_t serial) {
    auto* popup = static_cast<Popup*>(data);
    if (popup) {
        popup->handle_xdg_surface_configure(serial);
    }
}

static const struct xdg_surface_listener surface_listener = {
    .configure = xdg_surface_handle_configure,
};

static void popup_handle_configure(void* data, struct xdg_popup* /*popup*/,
                                   int32_t x, int32_t y, int32_t width, int32_t height) {
    auto* popup = static_cast<Popup*>(data);
    if (popup) {
        popup->handle_popup_configure(x, y, width, height);
    }
}

static void popup_handle_done(void* data, struct xdg_popup* /*popup*/) {
    auto* popup = static_cast<Popup*>(data);
    if (popup) {
        popup->handle_popup_done();
    }
}

static void popup_handle_repositioned(void* data, struct xdg_popup* /*popup*/,
                                      uint32_t token) {
    auto* popup = static_cast<Popup*>(data);
    if (popup) {
        popup->handle_repositioned(token);
    }
}

static const struct xdg_popup_listener popup_listener = {
    .configure = popup_handle_configure,
    .popup_done = popup_handle_done,
    .repositioned = popup_handle_repositioned,
};

}  // namespace

Positioner::Positioner(xdg_positioner* positioner, Display* display)
    : positioner_(positioner), display_(display) {}

Positioner::~Positioner() {
    if (positioner_) {
        xdg_positioner_destroy(positioner_);
    }
}

void Positioner::set_size(int32_t width, int32_t height) {
    if (positioner_) {
        xdg_positioner_set_size(positioner_, width, height);
    }
}

void Positioner::set_anchor_rect(int32_t x, int32_t y, int32_t width, int32_t height) {
    if (positioner_) {
        xdg_positioner_set_anchor_rect(positioner_, x, y, width, height);
    }
}

void Positioner::set_anchor_rect(const Rect& rect) {
    set_anchor_rect(rect.x, rect.y, rect.width, rect.height);
}

void Positioner::set_anchor(PositionerAnchor anchor) {
    if (positioner_) {
        xdg_positioner_set_anchor(positioner_, static_cast<uint32_t>(anchor));
    }
}

void Positioner::set_gravity(Gravity gravity) {
    if (positioner_) {
        xdg_positioner_set_gravity(positioner_, static_cast<uint32_t>(gravity));
    }
}

void Positioner::set_constraint_adjustment(uint32_t adjustment) {
    if (positioner_) {
        xdg_positioner_set_constraint_adjustment(positioner_, adjustment);
    }
}

void Positioner::set_offset(int32_t x, int32_t y) {
    if (positioner_) {
        xdg_positioner_set_offset(positioner_, x, y);
    }
}

void Positioner::set_reactive() {
    if (positioner_ &&
        xdg_positioner_get_version(positioner_) >= XDG_POSITIONER_SET_REACTIVE_SINCE_VERSION) {
        xdg_positioner_set_reactive(positioner_);
    }
}

void Positioner::set_parent_size(int32_t width, int32_t height) {
    if (positioner_ &&
        xdg_positioner_get_version(positioner_) >= XDG_POSITIONER_SET_PARENT_SIZE_SINCE_VERSION) {
        xdg_positioner_set_parent_size(positioner_, width, height);
    }
}

void Positioner::set_parent_configure(uint32_t serial) {
    if (positioner_ &&
        xdg_positioner_get_version(positioner_) >=
            XDG_POSITIONER_SET_PARENT_CONFIGURE_SINCE_VERSION) {
        xdg_positioner_set_parent_configure(positioner_, serial);
    }
}

Popup::Popup(SurfaceId id, wl_surface* surface, xdg_surface* xdg_surf,
             xdg_popup* popup, Display* display)
    : id_(id), surface_(surface), xdg_surf_(xdg_surf), popup_(popup), display_(display) {
    if (xdg_surf_) {
        xdg_surface_add_listener(xdg_surf_, &surface_listener, this);
    }
    if (popup_) {
        xdg_popup_add_listener(popup_, &popup_listener, this);
    }
}

Popup::~Popup() {
    if (popup_) {
        xdg_popup_destroy(popup_);
    }
    if (xdg_surf_) {
        xdg_surface_destroy(xdg_surf_);
    }
    if (surface_) {
        wl_surface_destroy(surface_);
    }
}

PopupSnapshot Popup::snapshot() const {
    PopupSnapshot snap;
    snap.id = id_;
    snap.geometry = geometry_;
    snap.configured = configured_;
    snap.dismissed = dismissed_;
    snap.repositioned_token = repositioned_token_;
    return snap;
}

void Popup::ack_configure(uint32_t serial) {
    last_serial_ = serial;
    if (xdg_surf_) {
        xdg_surface_ack_configure(xdg_surf_, serial);
    }
}

void Popup::reposition(const Positioner& positioner, uint32_t token) {
    if (popup_ &&
        xdg_popup_get_version(popup_) >= XDG_POPUP_REPOSITION_SINCE_VERSION) {
        xdg_popup_reposition(popup_, positioner.xdg_positioner_ptr(), token);
    }
}

void Popup::grab(Seat& seat, uint32_t serial) {
    if (popup_) {
        xdg_popup_grab(popup_, seat.wl_seat_ptr(), serial);
    }
}

void Popup::commit() {
    if (surface_) {
        wl_surface_commit(surface_);
    }
}

void Popup::attach_buffer(wl_buffer* buffer, int32_t x, int32_t y) {
    if (surface_) {
        wl_surface_attach(surface_, buffer, x, y);
    }
}

void Popup::damage(int32_t x, int32_t y, int32_t width, int32_t height) {
    if (surface_) {
        if (wl_surface_get_version(surface_) >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION) {
            wl_surface_damage_buffer(surface_, x, y, width, height);
        } else {
            wl_surface_damage(surface_, x, y, width, height);
        }
    }
}

void Popup::handle_xdg_surface_configure(uint32_t serial) {
    ack_configure(serial);
}

void Popup::handle_popup_configure(int32_t x, int32_t y, int32_t width, int32_t height) {
    geometry_ = {x, y, width, height};
    configured_ = true;

    if (display_) {
        display_->events().push(PopupConfigureEvent{id_, x, y, width, height});
    }
}

void Popup::handle_popup_done() {
    dismissed_ = true;

    if (display_) {
        display_->events().push(PopupDoneEvent{id_});
    }
}

void Popup::handle_repositioned(uint32_t token) {
    repositioned_token_ = token;

    if (display_) {
        display_->events().push(PopupRepositionedEvent{id_, token});
    }
}

}  // namespace browl
