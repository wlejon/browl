// browl off Linux: the same API, no Wayland. Display::connect() and
// connect_to_fd() fail with unavailable_reason(), so no object below is ever
// handed out; the rest is defined only so code written against browl links
// on every platform. Everything is inert: setters do nothing, queries report
// nothing, factories return nullptr.
#include "browl/browl.h"

namespace browl {

std::string unavailable_reason() {
#if defined(_WIN32)
    return "Wayland is a Linux display protocol; Windows has no Wayland compositor to connect to";
#elif defined(__APPLE__)
    return "Wayland is a Linux display protocol; macOS has no Wayland compositor to connect to";
#else
    return "browl's Wayland client is built for Linux only";
#endif
}

// ---- Display ----------------------------------------------------------------

Display::Display(wl_display* display) : display_(display) {}
struct Display::AppGlobals {};

Display::~Display() = default;

std::unique_ptr<Display> Display::connect(const std::string&, std::string* error) {
    if (error) *error = unavailable_reason();
    return nullptr;
}

std::unique_ptr<Display> Display::connect_to_fd(int, std::string* error) {
    if (error) *error = unavailable_reason();
    return nullptr;
}

bool Display::init_registry(std::string* error) {
    if (error) *error = unavailable_reason();
    return false;
}

int Display::fd() const { return -1; }
int Display::dispatch() { return -1; }
int Display::dispatch_pending() { return -1; }
int Display::flush() { return -1; }
int Display::roundtrip() { return -1; }
bool Display::prepare_read() { return false; }
void Display::cancel_read() {}
int Display::read_events() { return -1; }

bool Display::has_compositor() const { return false; }
bool Display::has_shm() const { return false; }
bool Display::has_layer_shell() const { return false; }
bool Display::has_xdg_shell() const { return false; }
bool Display::has_foreign_toplevel_manager() const { return false; }
bool Display::has_session_lock() const { return false; }
bool Display::has_idle_inhibit() const { return false; }
bool Display::has_screencopy() const { return false; }
bool Display::has_idle_notify() const { return false; }

std::unique_ptr<LayerSurface> Display::create_layer_surface(const LayerSurfaceConfig&) { return nullptr; }
std::shared_ptr<ForeignToplevelManager> Display::foreign_toplevel_manager() { return nullptr; }
std::unique_ptr<SessionLock> Display::create_session_lock() { return nullptr; }
std::unique_ptr<IdleInhibitor> Display::create_idle_inhibitor(wl_surface*) { return nullptr; }
std::shared_ptr<ScreenCopyManager> Display::screencopy_manager() { return nullptr; }
std::unique_ptr<IdleNotification> Display::create_idle_notification(uint32_t, Seat*) { return nullptr; }
std::shared_ptr<ShmPool> Display::create_shm_pool(size_t) { return nullptr; }
std::unique_ptr<Positioner> Display::create_positioner() { return nullptr; }
wl_surface* Display::create_surface() { return nullptr; }

std::vector<std::shared_ptr<Output>> Display::outputs() const { return {}; }
std::shared_ptr<Output> Display::output_by_id(OutputId) const { return nullptr; }
std::shared_ptr<Output> Display::default_output() const { return nullptr; }
std::vector<std::shared_ptr<Seat>> Display::seats() const { return {}; }
std::shared_ptr<Seat> Display::seat_by_id(SeatId) const { return nullptr; }
std::shared_ptr<Seat> Display::default_seat() const { return nullptr; }

bool Display::has_decoration_manager() const { return false; }
bool Display::has_viewporter() const { return false; }
bool Display::has_fractional_scale() const { return false; }
bool Display::has_presentation() const { return false; }
bool Display::has_activation() const { return false; }
bool Display::has_cursor_shape() const { return false; }
bool Display::has_pointer_constraints() const { return false; }
bool Display::has_relative_pointer() const { return false; }
bool Display::has_data_device() const { return false; }
bool Display::has_primary_selection() const { return false; }
bool Display::has_text_input() const { return false; }
bool Display::has_toplevel_icon() const { return false; }
bool Display::has_xdg_output() const { return false; }
void Display::enable_input() { input_enabled_ = true; }
std::unique_ptr<Window> Display::create_window(const WindowConfig&) { return nullptr; }
SurfaceId Display::surface_id_of(wl_surface*) const { return kNoSurface; }
RequestId Display::request_activation_token(const std::string&, wl_surface*, Seat*, uint32_t) { return 0; }
bool Display::activate(const std::string&, wl_surface*) { return false; }
RequestId Display::request_presentation_feedback(wl_surface*) { return 0; }
int Display::presentation_clock_id() const { return -1; }

SurfaceId Display::next_surface_id() { return next_surface_id_++; }
uint64_t Display::next_notification_id() { return next_notification_id_++; }
void Display::handle_global(uint32_t, const char*, uint32_t) {}
void Display::handle_global_remove(uint32_t) {}

// ---- Output, Seat -----------------------------------------------------------

Output::Output(OutputId id, wl_output* wl_output, Display* display)
    : id_(id), wl_output_(wl_output), display_(display) {}
Output::~Output() = default;
OutputSnapshot Output::snapshot() const {
    OutputSnapshot snap;
    snap.id = id_;
    return snap;
}
void Output::handle_geometry(int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {}
void Output::handle_mode(uint32_t, int32_t, int32_t, int32_t) {}
void Output::handle_done() {}
void Output::handle_scale(int32_t) {}
void Output::handle_name(const char*) {}
void Output::handle_description(const char*) {}
void Output::detach() {}
Rect Output::logical() const { return {}; }
void Output::attach_xdg_output(zxdg_output_manager_v1*) {}
void Output::handle_logical_position(int32_t, int32_t) {}
void Output::handle_logical_size(int32_t, int32_t) {}
void Output::handle_xdg_done() {}

SelectionContents text_selection(const std::string& utf8) {
    std::vector<uint8_t> bytes(utf8.begin(), utf8.end());
    SelectionContents contents;
    for (const char* mime : {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "TEXT", "STRING"}) {
        contents.emplace_back(mime, bytes);
    }
    return contents;
}

struct Seat::Impl {};

Seat::Seat(SeatId id, wl_seat* wl_seat, Display* display) : id_(id), wl_seat_(wl_seat), display_(display) {}
Seat::~Seat() = default;
SeatSnapshot Seat::snapshot() const {
    SeatSnapshot snap;
    snap.id = id_;
    return snap;
}
void Seat::handle_capabilities(uint32_t) {}
void Seat::handle_name(const char*) {}
void Seat::enable_input() {}
void Seat::detach() {}
std::shared_ptr<const Keymap> Seat::keymap() const { return nullptr; }
uint32_t Seat::modifiers() const { return 0; }
int32_t Seat::repeat_rate() const { return 25; }
int32_t Seat::repeat_delay_ms() const { return 600; }
uint32_t Seat::last_input_serial() const { return 0; }
uint32_t Seat::pointer_enter_serial() const { return 0; }
SurfaceId Seat::pointer_focus() const { return kNoSurface; }
SurfaceId Seat::keyboard_focus() const { return kNoSurface; }
void Seat::set_cursor(CursorShape) {}
CursorShape Seat::cursor() const { return CursorShape::Default; }
bool Seat::lock_pointer(wl_surface*) { return false; }
void Seat::unlock_pointer() {}
bool Seat::pointer_locked() const { return false; }
void Seat::warp_pointer(wl_surface*, double, double) {}
bool Seat::set_selection(Selection, SelectionContents) { return false; }
void Seat::clear_selection(Selection) {}
std::vector<std::string> Seat::selection_mime_types(Selection) const { return {}; }
bool Seat::owns_selection(Selection) const { return false; }
bool Seat::has_selection_protocol(Selection) const { return false; }
std::optional<std::vector<uint8_t>> Seat::read_selection(Selection, const std::string&, std::chrono::milliseconds) {
    return std::nullopt;
}
void Seat::set_drag_mime_types(std::vector<std::string>) {}
std::optional<std::vector<uint8_t>> Seat::read_drop(const std::string&, std::chrono::milliseconds) {
    return std::nullopt;
}
void Seat::finish_drop() {}
bool Seat::has_text_input() const { return false; }
void Seat::enable_text_input(uint32_t, ContentPurpose) {}
void Seat::disable_text_input() {}
bool Seat::text_input_enabled() const { return false; }
void Seat::set_text_input_cursor_rect(const Rect&) {}
void Seat::set_surrounding_text(const std::string&, int32_t, int32_t) {}

// ---- Keymap -----------------------------------------------------------------

struct Keymap::Impl {};

Keymap::Keymap(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Keymap::~Keymap() = default;
std::shared_ptr<const Keymap> Keymap::from_string(const std::string&) { return nullptr; }
std::shared_ptr<const Keymap> Keymap::from_names(const std::string&, const std::string&, const std::string&) {
    return nullptr;
}
uint32_t Keymap::keysym(uint32_t, uint32_t, uint32_t) const { return 0; }
std::string Keymap::utf8(uint32_t, uint32_t, uint32_t) const { return {}; }
bool Keymap::key_for_keysym(uint32_t, uint32_t*, uint32_t*, uint32_t) const { return false; }
bool Keymap::repeats(uint32_t) const { return false; }
uint32_t Keymap::layout_count() const { return 0; }
std::string Keymap::layout_name(uint32_t) const { return {}; }
uint32_t Keymap::decode_modifiers(uint32_t, uint32_t, uint32_t, uint32_t) const { return 0; }
uint32_t Keymap::keysym_to_utf32(uint32_t) { return 0; }
std::string Keymap::keysym_name(uint32_t) { return {}; }
void* Keymap::xkb_keymap_ptr() const { return nullptr; }

// ---- Window -----------------------------------------------------------------

struct Window::Impl {};

Window::Window(SurfaceId id, wl_surface* surface, xdg_surface* xdg_surf, xdg_toplevel* toplevel,
               const WindowConfig&, Display* display)
    : id_(id), surface_(surface), xdg_surf_(xdg_surf), toplevel_(toplevel), display_(display) {}
Window::~Window() = default;
WindowSnapshot Window::snapshot() const {
    WindowSnapshot snap;
    snap.id = id_;
    return snap;
}
void Window::map() {}
void Window::unmap() {}
bool Window::mapped() const { return false; }
void Window::set_title(const std::string&) {}
void Window::set_app_id(const std::string&) {}
void Window::set_min_size(int32_t, int32_t) {}
void Window::set_max_size(int32_t, int32_t) {}
void Window::set_maximized(bool) {}
void Window::set_fullscreen(bool, Output*) {}
void Window::set_minimized() {}
void Window::set_server_side_decorations(bool) {}
bool Window::set_icon(int32_t, int32_t, const uint8_t*) { return false; }
bool Window::has_icon_protocol() const { return false; }
bool Window::set_logical_size(int32_t, int32_t) { return false; }
void Window::set_buffer_scale(int32_t) {}
void Window::set_window_geometry(const Rect&) {}
void Window::start_move(Seat&, uint32_t) {}
void Window::start_resize(Seat&, uint32_t, ResizeEdge) {}
void Window::show_window_menu(Seat&, uint32_t, int32_t, int32_t) {}
void Window::attach_buffer(wl_buffer*, int32_t, int32_t) {}
void Window::damage(int32_t, int32_t, int32_t, int32_t) {}
void Window::commit() {}
void Window::handle_toplevel_configure(int32_t, int32_t, const uint32_t*, size_t) {}
void Window::handle_toplevel_close() {}
void Window::handle_configure_bounds(int32_t, int32_t) {}
void Window::handle_wm_capabilities(const uint32_t*, size_t) {}
void Window::handle_xdg_surface_configure(uint32_t) {}
void Window::handle_decoration_mode(uint32_t) {}
void Window::handle_preferred_scale(uint32_t) {}
void Window::handle_preferred_buffer_scale(int32_t) {}
void Window::handle_surface_enter(void*) {}
void Window::handle_surface_leave(void*) {}
void Window::detach() {}

// ---- Shared memory ----------------------------------------------------------

ShmBuffer::ShmBuffer(std::shared_ptr<ShmPool> pool, wl_buffer* buffer, void* data, size_t offset, size_t size,
                     int32_t width, int32_t height, int32_t stride, uint32_t format)
    : pool_(std::move(pool)),
      buffer_(buffer),
      data_(data),
      offset_(offset),
      size_(size),
      width_(width),
      height_(height),
      stride_(stride),
      format_(format) {}
ShmBuffer::~ShmBuffer() = default;
void ShmBuffer::handle_release() { busy_ = false; }

ShmPool::ShmPool(wl_shm* shm, int fd, void* data, size_t size, wl_shm_pool* pool)
    : shm_(shm), fd_(fd), data_(data), size_(size), pool_(pool) {}
ShmPool::~ShmPool() = default;
std::shared_ptr<ShmPool> ShmPool::create(wl_shm*, size_t) { return nullptr; }
bool ShmPool::resize(size_t) { return false; }
std::shared_ptr<ShmBuffer> ShmPool::create_buffer(size_t, int32_t, int32_t, int32_t, uint32_t) { return nullptr; }
std::shared_ptr<ShmBuffer> ShmPool::allocate_buffer(int32_t, int32_t, int32_t, uint32_t) { return nullptr; }

// ---- Layer surfaces, popups -------------------------------------------------

LayerSurface::LayerSurface(SurfaceId id, wl_surface* surface, zwlr_layer_surface_v1* layer_surf,
                           const LayerSurfaceConfig&, Display* display)
    : id_(id), surface_(surface), layer_surf_(layer_surf), display_(display) {
    snapshot_.id = id;
}
LayerSurface::~LayerSurface() = default;
LayerSurfaceSnapshot LayerSurface::snapshot() const { return snapshot_; }
void LayerSurface::set_size(uint32_t, uint32_t) {}
void LayerSurface::set_anchor(Anchor) {}
void LayerSurface::set_margin(const Margins&) {}
void LayerSurface::set_exclusive_zone(int32_t) {}
void LayerSurface::set_keyboard_interactivity(KeyboardInteractivity) {}
void LayerSurface::set_layer(Layer) {}
void LayerSurface::ack_configure(uint32_t) {}
void LayerSurface::commit() {}
void LayerSurface::attach_buffer(wl_buffer*, int32_t, int32_t) {}
void LayerSurface::damage(int32_t, int32_t, int32_t, int32_t) {}
std::unique_ptr<Popup> LayerSurface::create_popup(Positioner&) { return nullptr; }
void LayerSurface::handle_configure(uint32_t, uint32_t, uint32_t) {}
void LayerSurface::handle_closed() {}

Positioner::Positioner(xdg_positioner* positioner, Display* display) : positioner_(positioner), display_(display) {}
Positioner::~Positioner() = default;
void Positioner::set_size(int32_t, int32_t) {}
void Positioner::set_anchor_rect(int32_t, int32_t, int32_t, int32_t) {}
void Positioner::set_anchor_rect(const Rect&) {}
void Positioner::set_anchor(PositionerAnchor) {}
void Positioner::set_gravity(Gravity) {}
void Positioner::set_constraint_adjustment(uint32_t) {}
void Positioner::set_offset(int32_t, int32_t) {}
void Positioner::set_reactive() {}
void Positioner::set_parent_size(int32_t, int32_t) {}
void Positioner::set_parent_configure(uint32_t) {}

Popup::Popup(SurfaceId id, wl_surface* surface, xdg_surface* xdg_surf, xdg_popup* popup, Display* display)
    : id_(id), surface_(surface), xdg_surf_(xdg_surf), popup_(popup), display_(display) {}
Popup::~Popup() = default;
PopupSnapshot Popup::snapshot() const {
    PopupSnapshot snap;
    snap.id = id_;
    return snap;
}
void Popup::ack_configure(uint32_t) {}
void Popup::reposition(const Positioner&, uint32_t) {}
void Popup::grab(Seat&, uint32_t) {}
void Popup::commit() {}
void Popup::attach_buffer(wl_buffer*, int32_t, int32_t) {}
void Popup::damage(int32_t, int32_t, int32_t, int32_t) {}
void Popup::handle_xdg_surface_configure(uint32_t) {}
void Popup::handle_popup_configure(int32_t, int32_t, int32_t, int32_t) {}
void Popup::handle_popup_done() {}
void Popup::handle_repositioned(uint32_t) {}

// ---- Foreign toplevels ------------------------------------------------------

ForeignToplevel::ForeignToplevel(ToplevelId id, zwlr_foreign_toplevel_handle_v1* handle,
                                 ForeignToplevelManager* manager, Display* display)
    : id_(id), handle_(handle), manager_(manager), display_(display) {
    pending_.id = id;
}
ForeignToplevel::~ForeignToplevel() = default;
ForeignToplevelSnapshot ForeignToplevel::snapshot() const { return current_; }
void ForeignToplevel::activate(Seat&) {}
void ForeignToplevel::close() {}
void ForeignToplevel::set_maximized() {}
void ForeignToplevel::unset_maximized() {}
void ForeignToplevel::set_minimized() {}
void ForeignToplevel::unset_minimized() {}
void ForeignToplevel::set_fullscreen(Output*) {}
void ForeignToplevel::unset_fullscreen() {}
void ForeignToplevel::set_rectangle(wl_surface*, int32_t, int32_t, int32_t, int32_t) {}
void ForeignToplevel::detach() {}
void ForeignToplevel::handle_title(const char*) {}
void ForeignToplevel::handle_app_id(const char*) {}
void ForeignToplevel::handle_output_enter(wl_output*) {}
void ForeignToplevel::handle_output_leave(wl_output*) {}
void ForeignToplevel::handle_state(struct wl_array*) {}
void ForeignToplevel::handle_done() {}
void ForeignToplevel::handle_closed() {}
void ForeignToplevel::handle_parent(zwlr_foreign_toplevel_handle_v1*) {}

ForeignToplevelManager::ForeignToplevelManager(zwlr_foreign_toplevel_manager_v1* manager, Display* display)
    : manager_(manager), display_(display) {}
ForeignToplevelManager::~ForeignToplevelManager() = default;
std::vector<std::shared_ptr<ForeignToplevel>> ForeignToplevelManager::toplevels() const { return {}; }
std::shared_ptr<ForeignToplevel> ForeignToplevelManager::find_toplevel(ToplevelId) const { return nullptr; }
std::vector<ForeignToplevelSnapshot> ForeignToplevelManager::snapshots() const { return {}; }
void ForeignToplevelManager::detach() {}
void ForeignToplevelManager::handle_toplevel(zwlr_foreign_toplevel_handle_v1*) {}
void ForeignToplevelManager::handle_finished() {}
void ForeignToplevelManager::remove_toplevel(ToplevelId) {}

// ---- Session lock -----------------------------------------------------------

SessionLock::SessionLock(ext_session_lock_v1* lock, Display* display) : lock_(lock), display_(display) {}
SessionLock::~SessionLock() = default;
SessionLockSnapshot SessionLock::snapshot() const { return {}; }
std::unique_ptr<SessionLockSurface> SessionLock::create_surface(Output&) { return nullptr; }
void SessionLock::unlock_and_destroy() {}
void SessionLock::handle_locked() {}
void SessionLock::handle_finished() {}

SessionLockSurface::SessionLockSurface(SurfaceId id, OutputId output_id, wl_surface* surface,
                                       ext_session_lock_surface_v1* lock_surface, Display* display)
    : id_(id), output_id_(output_id), surface_(surface), lock_surface_(lock_surface), display_(display) {}
SessionLockSurface::~SessionLockSurface() = default;
SessionLockSurfaceSnapshot SessionLockSurface::snapshot() const {
    SessionLockSurfaceSnapshot snap;
    snap.id = id_;
    snap.output_id = output_id_;
    return snap;
}
void SessionLockSurface::ack_configure(uint32_t) {}
void SessionLockSurface::commit() {}
void SessionLockSurface::attach_buffer(wl_buffer*, int32_t, int32_t) {}
void SessionLockSurface::damage(int32_t, int32_t, int32_t, int32_t) {}
void SessionLockSurface::handle_configure(uint32_t, uint32_t, uint32_t) {}

// ---- Idle -------------------------------------------------------------------

IdleInhibitor::IdleInhibitor(zwp_idle_inhibitor_v1* inhibitor, Display* display)
    : inhibitor_(inhibitor), display_(display) {}
IdleInhibitor::~IdleInhibitor() = default;

IdleNotification::IdleNotification(uint64_t id, ext_idle_notification_v1* notification, Display* display)
    : id_(id), notification_(notification), display_(display) {}
IdleNotification::~IdleNotification() = default;
IdleNotificationSnapshot IdleNotification::snapshot() const {
    IdleNotificationSnapshot snap;
    snap.id = id_;
    return snap;
}
void IdleNotification::handle_idled() {}
void IdleNotification::handle_resumed() {}

// ---- Screencopy -------------------------------------------------------------

ScreenCopyFrame::ScreenCopyFrame(zwlr_screencopy_frame_v1* frame, Display* display)
    : frame_(frame), display_(display) {}
ScreenCopyFrame::~ScreenCopyFrame() = default;
ScreenCopyFrameSnapshot ScreenCopyFrame::snapshot() const { return snapshot_; }
void ScreenCopyFrame::copy(wl_buffer*) {}
void ScreenCopyFrame::copy_with_damage(wl_buffer*) {}
void ScreenCopyFrame::handle_buffer(uint32_t, uint32_t, uint32_t, uint32_t) {}
void ScreenCopyFrame::handle_flags(uint32_t) {}
void ScreenCopyFrame::handle_ready(uint32_t, uint32_t, uint32_t) {}
void ScreenCopyFrame::handle_failed() {}
void ScreenCopyFrame::handle_damage(uint32_t, uint32_t, uint32_t, uint32_t) {}
void ScreenCopyFrame::handle_buffer_done() {}

ScreenCopyManager::ScreenCopyManager(zwlr_screencopy_manager_v1* manager, Display* display)
    : manager_(manager), display_(display) {}
ScreenCopyManager::~ScreenCopyManager() = default;
std::unique_ptr<ScreenCopyFrame> ScreenCopyManager::capture_output(Output&, bool) { return nullptr; }
std::unique_ptr<ScreenCopyFrame> ScreenCopyManager::capture_output_region(Output&, const Rect&, bool) {
    return nullptr;
}
void ScreenCopyManager::detach() {}

}  // namespace browl
