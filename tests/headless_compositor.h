#pragma once

#include <wayland-server.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace browl::test {

struct CompositorToplevelState {
    struct wl_resource* resource = nullptr;
    std::string title;
    std::string app_id;
    uint32_t state = 0;
    bool activated = false;
    bool closed = false;
    bool minimized = false;
    bool maximized = false;
    bool fullscreen = false;
};

// What browl asked of the application-window globals (windows, decorations,
// viewport, presentation, activation, icon, cursor, text input). Read it
// through HeadlessCompositor::app_state(), a copy taken under the lock.
struct CompositorAppState {
    // xdg_toplevel (the newest window)
    int xdg_surfaces_created = 0;
    int toplevels_created = 0;
    int toplevels_destroyed = 0;
    std::string title;
    std::string app_id;
    int32_t min_width = 0, min_height = 0, max_width = 0, max_height = 0;
    bool maximized = false;
    bool fullscreen = false;
    bool minimized = false;
    uint32_t last_ack_serial = 0;
    int32_t geometry_width = 0, geometry_height = 0;
    // wl_surface
    int commits = 0;
    int null_attaches = 0;
    int32_t buffer_scale = 1;
    // zxdg_toplevel_decoration_v1: the mode browl asked for (0: none yet)
    uint32_t decoration_mode_requested = 0;
    int decorations_created = 0;
    // wp_viewport
    bool viewport_created = false;
    int32_t viewport_width = 0, viewport_height = 0;
    bool fractional_created = false;
    // wp_presentation
    int feedbacks_requested = 0;
    // xdg_activation_v1
    int tokens_requested = 0;
    std::string token_app_id;
    uint32_t token_serial = 0;
    bool token_has_surface = false;
    std::string activated_token;
    // xdg_toplevel_icon_v1
    int icon_buffers_added = 0;
    int32_t icon_buffer_size = 0;
    uint32_t icon_first_pixel = 0;  // the first ARGB word of the last icon buffer
    int icons_set = 0;
    int icons_cleared = 0;
    // cursor
    uint32_t cursor_shape = 0;
    uint32_t cursor_shape_serial = 0;
    int cursor_hidden = 0;  // wl_pointer.set_cursor with a null surface
    // wl_pointer.set_cursor with a surface (no cursor-shape: an XCursor image)
    int cursor_surfaces_set = 0;
    int32_t cursor_hotspot_x = 0, cursor_hotspot_y = 0;
    int32_t cursor_buffer_width = 0;  // the shm buffer attached to that surface
    int cursor_commits = 0;
    // wl_data_device.start_drag (the newest drag)
    int drag_starts = 0;
    uint32_t drag_serial = 0;
    bool drag_origin_is_window = false;
    std::vector<std::string> drag_mimes;
    uint32_t drag_source_actions = 0;
    bool drag_has_icon = false;
    int32_t drag_icon_width = 0;
    uint32_t drag_icon_first_pixel = 0;
    int32_t drag_icon_offset_x = 0, drag_icon_offset_y = 0;
    int drag_icon_commits = 0;
    int drag_icons_destroyed = 0;
    // zwp_text_input_v3
    bool text_input_enabled = false;
    uint32_t text_input_purpose = 0;
    uint32_t text_input_hints = 0;
    int text_input_commits = 0;
};

// One selection source a client offered (clipboard or primary).
struct CompositorSource {
    struct wl_resource* resource = nullptr;  // null once the client destroyed it
    std::vector<std::string> mimes;
    bool primary = false;
    uint32_t actions = 0;  // wl_data_source.set_actions
};

class HeadlessCompositor {
public:
    HeadlessCompositor();
    ~HeadlessCompositor();

    HeadlessCompositor(const HeadlessCompositor&) = delete;
    HeadlessCompositor& operator=(const HeadlessCompositor&) = delete;

    int create_client_fd();
    const char* add_socket_auto();
    int add_socket(const char* name);
    void start();
    void stop();
    void flush();
    void step_loop(int timeout_ms = 10);

    // Compositor & display actions
    void send_output_done();
    // A mode event followed by done, as a compositor sends on a mode change.
    void send_output_mode(uint32_t flags, int32_t width, int32_t height, int32_t refresh_mhz);
    void send_seat_caps(uint32_t caps, const char* name = "seat0");

    // Layer shell actions
    void configure_layer_surface(uint32_t width, uint32_t height);
    void close_layer_surface();
    bool layer_surface_created() const;
    uint32_t last_layer_ack_serial() const;
    bool layer_surface_committed() const;

    // XDG popup actions
    void configure_popup(int32_t x, int32_t y, int32_t width, int32_t height);
    void send_popup_done();
    void send_popup_repositioned(uint32_t token);
    bool popup_created() const;
    bool popup_repositioned_received() const;

    // Foreign toplevel actions
    CompositorToplevelState* create_foreign_toplevel(const std::string& title,
                                                   const std::string& app_id,
                                                   uint32_t state);
    void update_toplevel_title(CompositorToplevelState* top, const std::string& title);
    void update_toplevel_app_id(CompositorToplevelState* top, const std::string& app_id);
    void update_toplevel_state(CompositorToplevelState* top, uint32_t state);
    void close_toplevel(CompositorToplevelState* top);

    // Session lock actions
    void send_session_locked();
    void send_session_lock_finished();
    void configure_lock_surface(uint32_t width, uint32_t height);
    bool session_lock_created() const;
    bool session_unlock_received() const;
    bool lock_surface_created() const;
    uint32_t last_lock_surface_ack_serial() const;

    // Idle actions
    bool idle_inhibitor_created() const;
    bool idle_inhibitor_destroyed() const;
    void fire_idle();
    void fire_resume();

    // Screencopy actions
    void send_screencopy_buffer(uint32_t format, uint32_t width, uint32_t height,
                               uint32_t stride);
    void send_screencopy_ready(uint32_t tv_sec, uint32_t tv_nsec);
    void send_screencopy_failed();
    bool screencopy_frame_created() const;
    bool screencopy_copy_received() const;

    // Application windows (headless_compositor_app.cpp). configure_window
    // sends the whole sequence (bounds, capabilities, decoration mode when
    // `decoration_mode` != 0, toplevel configure, xdg_surface configure) and
    // returns the serial.
    uint32_t configure_window(int32_t width, int32_t height, const std::vector<uint32_t>& states,
                              int32_t bounds_width = 0, int32_t bounds_height = 0,
                              const std::vector<uint32_t>& wm_caps = {}, uint32_t decoration_mode = 0);
    void close_window();
    void send_surface_enter();  // the window's surface enters the output
    void send_surface_leave();
    void send_preferred_buffer_scale(int32_t scale);
    void send_fractional_scale(uint32_t scale120);
    void send_presented(uint64_t time_ns, uint32_t refresh_ns, uint64_t seq, uint32_t flags);
    void send_discarded();
    void send_xdg_output_logical(int32_t x, int32_t y, int32_t width, int32_t height);
    CompositorAppState app_state() const;

    // Input (headless_compositor_input.cpp). Events go to the newest
    // pointer / keyboard / touch / text input, about the window's surface.
    void send_keymap(const std::string& layout, const std::string& variant = "");
    void send_keyboard_enter(const std::vector<uint32_t>& keys = {});
    void send_keyboard_leave();
    void send_key(uint32_t key, bool pressed, uint32_t time_ms = 0);
    void send_modifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
    void send_repeat_info(int32_t rate, int32_t delay_ms);
    void send_pointer_enter(double x, double y);
    void send_pointer_leave();
    void send_pointer_motion(double x, double y, uint32_t time_ms = 0);
    void send_pointer_button(uint32_t button, bool pressed);
    // Withdraw wp_cursor_shape_manager_v1 (before the client binds): a
    // compositor without it, which gets cursors as surfaces.
    void remove_cursor_shape();
    // A drag offering `mimes` enters the newest window at (x, y), moves to
    // (to_x, to_y) and is dropped there.
    void send_drag(const std::vector<std::string>& mimes, double x, double y, double to_x, double to_y);
    // The drag a client started (wl_data_device.start_drag), from the
    // compositor's side: what a target would take, the action chosen, the
    // drop, the target finishing, a cancel; and reading what it carries
    // (returns the pipe's read end; the client writes it as it dispatches).
    void drag_source_target(const char* mime);
    void drag_source_action(uint32_t action);
    void drag_source_dropped();
    void drag_source_finished();
    void drag_source_cancelled();
    int drag_source_receive(const std::string& mime);
    bool drag_source_alive() const;  // the client has not destroyed the drag's source
    // axis_source, value120 + axis on each nonzero axis, then frame.
    void send_pointer_scroll(int32_t value120_x, int32_t value120_y, double dx, double dy,
                             uint32_t source);
    void send_touch_down(int32_t id, double x, double y);
    void send_touch_motion(int32_t id, double x, double y);
    void send_touch_up(int32_t id);
    void send_text_input_enter();
    void send_text_input_done(const std::string& preedit, int32_t begin, int32_t end,
                              const std::string& commit, uint32_t delete_before, uint32_t delete_after);
    uint32_t last_serial() const;
    bool input_bound() const;  // pointer and keyboard were requested

    // Inspection getters
    struct wl_display* server_display() const { return display_; }

public:
    void init_globals();
    void init_shell_globals();
    void init_service_globals();
    void init_app_globals();
    void init_input_globals();

    // Application-window and input state (the _app / _input files).
    CompositorAppState app_;
    struct wl_resource* window_surface_ = nullptr;
    struct wl_resource* xdg_surface_resource_ = nullptr;
    struct wl_resource* toplevel_resource_ = nullptr;
    struct wl_resource* decoration_resource_ = nullptr;
    struct wl_resource* fractional_resource_ = nullptr;
    struct wl_resource* xdg_output_resource_ = nullptr;
    std::vector<struct wl_resource*> feedbacks_;
    struct wl_resource* pointer_resource_ = nullptr;
    struct wl_resource* keyboard_resource_ = nullptr;
    struct wl_resource* touch_resource_ = nullptr;
    struct wl_resource* text_input_resource_ = nullptr;
    std::vector<struct wl_resource*> data_devices_;
    std::vector<struct wl_resource*> primary_devices_;
    std::vector<std::unique_ptr<CompositorSource>> sources_;
    CompositorSource* drag_source_ = nullptr;
    struct wl_resource* drag_icon_surface_ = nullptr;
    CompositorSource* clipboard_selection_ = nullptr;
    CompositorSource* primary_selection_ = nullptr;
    int next_token_ = 1;

    struct wl_display* display_ = nullptr;
    struct wl_client* client_ = nullptr;
    struct wl_event_loop* loop_ = nullptr;

    std::thread worker_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;

    // Globals
    struct wl_global* compositor_global_ = nullptr;
    struct wl_global* output_global_ = nullptr;
    struct wl_global* seat_global_ = nullptr;
    struct wl_global* layer_shell_global_ = nullptr;
    struct wl_global* xdg_wm_base_global_ = nullptr;
    struct wl_global* foreign_toplevel_global_ = nullptr;
    struct wl_global* session_lock_global_ = nullptr;
    struct wl_global* idle_inhibit_global_ = nullptr;
    struct wl_global* screencopy_global_ = nullptr;
    struct wl_global* idle_notify_global_ = nullptr;
    struct wl_global* cursor_shape_global_ = nullptr;
    struct wl_resource* cursor_surface_ = nullptr;

    // Active resources
    struct wl_resource* output_resource_ = nullptr;
    struct wl_resource* seat_resource_ = nullptr;
    struct wl_resource* layer_surface_resource_ = nullptr;
    struct wl_resource* surface_resource_ = nullptr;
    struct wl_resource* popup_resource_ = nullptr;
    struct wl_resource* positioner_resource_ = nullptr;
    struct wl_resource* toplevel_manager_resource_ = nullptr;
    struct wl_resource* session_lock_resource_ = nullptr;
    struct wl_resource* lock_surface_resource_ = nullptr;
    struct wl_resource* idle_notification_resource_ = nullptr;
    struct wl_resource* screencopy_frame_resource_ = nullptr;

    // State flags
    uint32_t next_serial_ = 1;
    uint32_t last_layer_ack_serial_ = 0;
    bool layer_surface_committed_ = false;
    bool popup_repositioned_received_ = false;
    bool session_unlock_received_ = false;
    uint32_t last_lock_surface_ack_serial_ = 0;
    bool idle_inhibitor_created_ = false;
    bool idle_inhibitor_destroyed_ = false;
    bool screencopy_copy_received_ = false;

    std::vector<std::unique_ptr<CompositorToplevelState>> toplevels_;
};

}  // namespace browl::test
