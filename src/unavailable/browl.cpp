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

Seat::Seat(SeatId id, wl_seat* wl_seat, Display* display) : id_(id), wl_seat_(wl_seat), display_(display) {}
Seat::~Seat() = default;
SeatSnapshot Seat::snapshot() const {
    SeatSnapshot snap;
    snap.id = id_;
    return snap;
}
void Seat::handle_capabilities(uint32_t) {}
void Seat::handle_name(const char*) {}
void Seat::detach() {}

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
