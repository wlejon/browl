#pragma once

// An application window: a wl_surface with the xdg_toplevel role, plus what
// a desktop application wants around it (server-side decorations, fractional
// scale through a viewport, an icon). The client draws into surface() however
// it likes — attach_buffer with shm, or a Vulkan swapchain on the surface
// (VK_KHR_wayland_surface), which then does the attach and commit itself.
//
// Configure handling: browl acks every xdg_surface.configure as it arrives
// and then queues a WindowConfigureEvent; the client's next commit must show
// the configured state (its new size), as xdg-shell requires.
//
// Scale: snapshot().scale120 is the scale the compositor wants the buffers
// at. With wp_viewporter, set_logical_size(w, h) makes whatever buffer size
// the client attaches (or the swapchain presents) show at w x h logical px,
// so a fractional scale renders crisp; without it, buffer scale 1 is used
// and the compositor scales.

#include "browl/events.h"
#include "browl/types.h"

#include <memory>
#include <string>

struct wl_surface;
struct wl_buffer;
struct xdg_surface;
struct xdg_toplevel;

namespace browl {

class Display;
class Output;
class Seat;

struct WindowConfig {
    std::string title;
    /// The desktop's name for the application (xdg_toplevel.set_app_id): the
    /// desktop entry's basename. Empty leaves it unset.
    std::string app_id;
    /// Ask the compositor to draw the title bar and borders. False asks for
    /// client-side decorations, which browl never draws: a borderless window.
    bool server_side_decorations = true;
    Size min_size;  // 0 = no limit
    Size max_size;  // 0 = no limit
    bool maximized = false;
    bool fullscreen = false;
    /// False creates the window hidden: its wl_surface exists (a Vulkan
    /// surface can be made on it) but it has no xdg_toplevel role until map().
    bool mapped = true;
};

class Window {
public:
    Window(SurfaceId id, wl_surface* surface, xdg_surface* xdg_surf, xdg_toplevel* toplevel,
           const WindowConfig& config, Display* display);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    SurfaceId id() const { return id_; }
    wl_surface* wl_surface_ptr() const { return surface_; }
    xdg_surface* xdg_surface_ptr() const { return xdg_surf_; }
    xdg_toplevel* xdg_toplevel_ptr() const { return toplevel_; }

    /// The compositor's last word on this window (thread-safe copy).
    WindowSnapshot snapshot() const;

    /// Give the surface its xdg_toplevel role again (or for the first time,
    /// after mapped = false): attaches a null buffer and commits (so a buffer
    /// attached while hidden cannot be an unconfigured buffer), re-creates
    /// the role with the title, app id, size limits, decoration wish, icon
    /// and maximized / fullscreen wishes, and makes the initial commit; the
    /// first configure arrives as usual. No-op when mapped.
    void map();
    /// Hide the window: attach a null buffer, commit, and destroy the role
    /// objects (decoration, icon, xdg_toplevel, xdg_surface). The wl_surface,
    /// viewport and fractional-scale objects stay. No-op when unmapped.
    void unmap();
    bool mapped() const;

    void set_title(const std::string& title);
    void set_app_id(const std::string& app_id);
    void set_min_size(int32_t width, int32_t height);
    void set_max_size(int32_t width, int32_t height);
    void set_maximized(bool maximized);
    void set_fullscreen(bool fullscreen, Output* output = nullptr);
    void set_minimized();
    /// Ask for server-side (true) or client-side (false) decorations; the
    /// answer arrives in snapshot().decoration with the next configure.
    /// Without the decoration protocol this only records the wish.
    void set_server_side_decorations(bool server_side);
    /// The window's icon (xdg_toplevel_icon_v1): straight-alpha RGBA8,
    /// stride width*4, square. False when the compositor has no icon
    /// protocol or the image is unusable. An empty image (0x0) clears it.
    bool set_icon(int32_t width, int32_t height, const uint8_t* rgba);
    bool has_icon_protocol() const;

    /// Show buffers at width x height logical px whatever their pixel size
    /// (wp_viewport.set_destination); 0x0 unsets it. False without
    /// wp_viewporter.
    bool set_logical_size(int32_t width, int32_t height);
    void set_buffer_scale(int32_t scale);
    /// Which part of the surface is the window proper (xdg_surface.set_window_geometry).
    void set_window_geometry(const Rect& rect);

    /// Interactive move / resize, from a pointer press with `serial`.
    void start_move(Seat& seat, uint32_t serial);
    void start_resize(Seat& seat, uint32_t serial, ResizeEdge edges);
    void show_window_menu(Seat& seat, uint32_t serial, int32_t x, int32_t y);

    void attach_buffer(wl_buffer* buffer, int32_t x = 0, int32_t y = 0);
    void damage(int32_t x, int32_t y, int32_t width, int32_t height);
    void commit();

    // Internal protocol listeners
    void handle_toplevel_configure(int32_t width, int32_t height, const uint32_t* states, size_t count);
    void handle_toplevel_close();
    void handle_configure_bounds(int32_t width, int32_t height);
    void handle_wm_capabilities(const uint32_t* caps, size_t count);
    void handle_xdg_surface_configure(uint32_t serial);
    void handle_decoration_mode(uint32_t mode);
    void handle_preferred_scale(uint32_t scale120);
    void handle_preferred_buffer_scale(int32_t factor);
    void handle_surface_enter(void* wl_output);
    void handle_surface_leave(void* wl_output);
    void detach();

private:
    struct Impl;
    void attach_role();
    void destroy_role();
    bool apply_icon();
    void publish_outputs();
    void update_scale();

    SurfaceId id_ = kNoSurface;
    wl_surface* surface_ = nullptr;
    xdg_surface* xdg_surf_ = nullptr;
    xdg_toplevel* toplevel_ = nullptr;
    Display* display_ = nullptr;
    std::unique_ptr<Impl> impl_;
};

}  // namespace browl
