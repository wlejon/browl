#pragma once

#include "browl/events.h"
#include "browl/types.h"

#include <memory>

struct xdg_positioner;
struct xdg_surface;
struct xdg_popup;
struct wl_surface;
struct wl_buffer;

namespace browl {

class Display;
class Seat;

class Positioner {
public:
    Positioner(xdg_positioner* positioner, Display* display);
    ~Positioner();

    Positioner(const Positioner&) = delete;
    Positioner& operator=(const Positioner&) = delete;

    xdg_positioner* xdg_positioner_ptr() const { return positioner_; }

    void set_size(int32_t width, int32_t height);
    void set_anchor_rect(int32_t x, int32_t y, int32_t width, int32_t height);
    void set_anchor_rect(const Rect& rect);
    void set_anchor(PositionerAnchor anchor);
    void set_gravity(Gravity gravity);
    void set_constraint_adjustment(uint32_t adjustment);
    void set_offset(int32_t x, int32_t y);
    void set_reactive();
    void set_parent_size(int32_t width, int32_t height);
    void set_parent_configure(uint32_t serial);

private:
    xdg_positioner* positioner_ = nullptr;
    Display* display_ = nullptr;
};

class Popup {
public:
    Popup(SurfaceId id, wl_surface* surface, xdg_surface* xdg_surf,
          xdg_popup* popup, Display* display);
    ~Popup();

    Popup(const Popup&) = delete;
    Popup& operator=(const Popup&) = delete;

    SurfaceId id() const { return id_; }
    PopupSnapshot snapshot() const;

    void ack_configure(uint32_t serial);
    void reposition(const Positioner& positioner, uint32_t token);
    void grab(Seat& seat, uint32_t serial);
    void commit();
    void attach_buffer(wl_buffer* buffer, int32_t x = 0, int32_t y = 0);
    void damage(int32_t x, int32_t y, int32_t width, int32_t height);

    wl_surface* wl_surface_ptr() const { return surface_; }
    xdg_surface* xdg_surface_ptr() const { return xdg_surf_; }
    xdg_popup* xdg_popup_ptr() const { return popup_; }

    // Internal protocol listeners
    void handle_xdg_surface_configure(uint32_t serial);
    void handle_popup_configure(int32_t x, int32_t y, int32_t width, int32_t height);
    void handle_popup_done();
    void handle_repositioned(uint32_t token);

private:
    SurfaceId id_ = kNoSurface;
    wl_surface* surface_ = nullptr;
    xdg_surface* xdg_surf_ = nullptr;
    xdg_popup* popup_ = nullptr;
    Display* display_ = nullptr;

    Rect geometry_;
    bool configured_ = false;
    bool dismissed_ = false;
    uint32_t repositioned_token_ = 0;
    uint32_t last_serial_ = 0;
};

}  // namespace browl
