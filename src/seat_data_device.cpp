// A seat's selections and drag and drop: the clipboard (wl_data_device),
// the primary selection (zwp_primary_selection_device_v1), and drops onto
// this client's surfaces.
//
// Offering: a source holds the SelectionContents; each send request is
// written on a detached thread, so a slow reader never blocks the thread
// that dispatches the Display. Reading: a selection this client owns is
// answered from memory; another client's through a pipe, with a timeout.
#include "browl/display.h"

#include "app_globals.h"
#include "primary-selection-unstable-v1-client-protocol.h"
#include "seat_impl.h"

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <wayland-client.h>

#include <algorithm>
#include <cerrno>
#include <thread>

namespace browl {

namespace {

Seat::Impl* impl_of(void* data) {
    return static_cast<Seat::Impl*>(data);
}

std::vector<std::string> mimes_of(const SelectionContents& contents) {
    std::vector<std::string> out;
    for (const auto& [mime, bytes] : contents) {
        out.push_back(mime);
    }
    return out;
}

bool has_mime(const std::vector<std::string>& mimes, const std::string& mime) {
    return std::find(mimes.begin(), mimes.end(), mime) != mimes.end();
}

// Writes `bytes` to `fd` and closes it, off the dispatch thread.
void write_detached(int fd, std::shared_ptr<const SelectionContents> contents, size_t index) {
    std::thread([fd, contents = std::move(contents), index]() {
        // A reader that goes away must not kill the process with SIGPIPE:
        // the signal a write raises is directed at this thread, which blocks it.
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &set, nullptr);
        const int flags = fcntl(fd, F_GETFL);
        if (flags >= 0 && (flags & O_NONBLOCK)) {
            fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
        }
        const auto& bytes = (*contents)[index].second;
        size_t off = 0;
        while (off < bytes.size()) {
            const ssize_t n = write(fd, bytes.data() + off, bytes.size() - off);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            off += static_cast<size_t>(n);
        }
        close(fd);
    }).detach();
}

// Reads `fd` to EOF or until `timeout` passes; closes it.
std::optional<std::vector<uint8_t>> read_pipe(int fd, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::vector<uint8_t> out;
    uint8_t buf[16384];
    while (true) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            close(fd);
            return std::nullopt;
        }
        pollfd pfd{fd, POLLIN, 0};
        const int r = poll(&pfd, 1, static_cast<int>(left.count()));
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return std::nullopt;
        }
        if (r == 0) {
            continue;  // the deadline check above ends it
        }
        const ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            close(fd);
            return std::nullopt;
        }
        if (n == 0) {
            close(fd);
            return out;
        }
        out.insert(out.end(), buf, buf + n);
    }
}

// --- wl_data_offer / wl_data_device / wl_data_source --------------------------------

static void data_offer_handle_offer(void* data, struct wl_data_offer* offer, const char* mime) {
    impl_of(data)->handle_offer_mime(Selection::Clipboard, offer, mime);
}

static void data_offer_handle_source_actions(void*, struct wl_data_offer*, uint32_t) {}
static void data_offer_handle_action(void*, struct wl_data_offer*, uint32_t) {}

static const struct wl_data_offer_listener data_offer_listener = {
    .offer = data_offer_handle_offer,
    .source_actions = data_offer_handle_source_actions,
    .action = data_offer_handle_action,
};

static void data_device_handle_data_offer(void* data, struct wl_data_device* /*device*/,
                                          struct wl_data_offer* offer) {
    wl_data_offer_add_listener(offer, &data_offer_listener, data);
    impl_of(data)->handle_data_offer(Selection::Clipboard, offer);
}

static void data_device_handle_enter(void* data, struct wl_data_device* /*device*/, uint32_t serial,
                                     struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y,
                                     struct wl_data_offer* offer) {
    impl_of(data)->handle_drag_enter(serial, surface, wl_fixed_to_double(x), wl_fixed_to_double(y), offer);
}

static void data_device_handle_leave(void* data, struct wl_data_device* /*device*/) {
    impl_of(data)->handle_drag_leave();
}

static void data_device_handle_motion(void* data, struct wl_data_device* /*device*/, uint32_t time,
                                      wl_fixed_t x, wl_fixed_t y) {
    impl_of(data)->handle_drag_motion(time, wl_fixed_to_double(x), wl_fixed_to_double(y));
}

static void data_device_handle_drop(void* data, struct wl_data_device* /*device*/) {
    impl_of(data)->handle_drop();
}

static void data_device_handle_selection(void* data, struct wl_data_device* /*device*/,
                                         struct wl_data_offer* offer) {
    impl_of(data)->handle_selection(Selection::Clipboard, offer);
}

static const struct wl_data_device_listener data_device_listener = {
    .data_offer = data_device_handle_data_offer,
    .enter = data_device_handle_enter,
    .leave = data_device_handle_leave,
    .motion = data_device_handle_motion,
    .drop = data_device_handle_drop,
    .selection = data_device_handle_selection,
};

static void data_source_handle_target(void*, struct wl_data_source*, const char*) {}

static void data_source_handle_send(void* data, struct wl_data_source* /*source*/, const char* mime,
                                    int32_t fd) {
    auto* src = static_cast<SelectionSource*>(data);
    src->impl->handle_source_send(src, mime, fd);
}

static void data_source_handle_cancelled(void* data, struct wl_data_source* /*source*/) {
    auto* src = static_cast<SelectionSource*>(data);
    src->impl->handle_source_cancelled(src);
}

static void data_source_handle_dnd_drop_performed(void*, struct wl_data_source*) {}
static void data_source_handle_dnd_finished(void*, struct wl_data_source*) {}
static void data_source_handle_action(void*, struct wl_data_source*, uint32_t) {}

static const struct wl_data_source_listener data_source_listener = {
    .target = data_source_handle_target,
    .send = data_source_handle_send,
    .cancelled = data_source_handle_cancelled,
    .dnd_drop_performed = data_source_handle_dnd_drop_performed,
    .dnd_finished = data_source_handle_dnd_finished,
    .action = data_source_handle_action,
};

// --- zwp_primary_selection_* -------------------------------------------------------

static void primary_offer_handle_offer(void* data, struct zwp_primary_selection_offer_v1* offer,
                                       const char* mime) {
    impl_of(data)->handle_offer_mime(Selection::Primary, offer, mime);
}

static const struct zwp_primary_selection_offer_v1_listener primary_offer_listener = {
    .offer = primary_offer_handle_offer,
};

static void primary_device_handle_data_offer(void* data, struct zwp_primary_selection_device_v1* /*d*/,
                                             struct zwp_primary_selection_offer_v1* offer) {
    zwp_primary_selection_offer_v1_add_listener(offer, &primary_offer_listener, data);
    impl_of(data)->handle_data_offer(Selection::Primary, offer);
}

static void primary_device_handle_selection(void* data, struct zwp_primary_selection_device_v1* /*d*/,
                                            struct zwp_primary_selection_offer_v1* offer) {
    impl_of(data)->handle_selection(Selection::Primary, offer);
}

static const struct zwp_primary_selection_device_v1_listener primary_device_listener = {
    .data_offer = primary_device_handle_data_offer,
    .selection = primary_device_handle_selection,
};

static void primary_source_handle_send(void* data, struct zwp_primary_selection_source_v1* /*s*/,
                                       const char* mime, int32_t fd) {
    auto* src = static_cast<SelectionSource*>(data);
    src->impl->handle_source_send(src, mime, fd);
}

static void primary_source_handle_cancelled(void* data, struct zwp_primary_selection_source_v1* /*s*/) {
    auto* src = static_cast<SelectionSource*>(data);
    src->impl->handle_source_cancelled(src);
}

static const struct zwp_primary_selection_source_v1_listener primary_source_listener = {
    .send = primary_source_handle_send,
    .cancelled = primary_source_handle_cancelled,
};

}  // namespace

// ---- Impl ---------------------------------------------------------------------------

void Seat::Impl::bind_data_devices() {
    auto& app = display->app_globals();
    if (!data_device && app.data_device_manager) {
        data_device = wl_data_device_manager_get_data_device(app.data_device_manager, seat->wl_seat_ptr());
        wl_data_device_add_listener(data_device, &data_device_listener, this);
    }
    if (!primary_device && app.primary_selection_manager) {
        primary_device = zwp_primary_selection_device_manager_v1_get_device(app.primary_selection_manager,
                                                                            seat->wl_seat_ptr());
        zwp_primary_selection_device_v1_add_listener(primary_device, &primary_device_listener, this);
    }
}

void Seat::Impl::release_data_devices() {
    finish_drag_offer();
    for (Selection which : {Selection::Clipboard, Selection::Primary}) {
        SelectionSlot& s = slot(which);
        std::vector<void*> offers;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (auto& [offer, mimes] : s.offers) {
                offers.push_back(offer);
            }
            if (s.current_offer && !s.offers.count(s.current_offer)) {
                offers.push_back(s.current_offer);
            }
            s.current_offer = nullptr;
            s.echo_offer = nullptr;
            s.source = nullptr;
        }
        for (void* o : offers) {
            destroy_offer(which, o);
        }
    }
    while (!sources.empty()) {
        destroy_source(sources.back());
    }
    if (data_device) {
        if (wl_data_device_get_version(data_device) >= WL_DATA_DEVICE_RELEASE_SINCE_VERSION) {
            wl_data_device_release(data_device);
        } else {
            wl_data_device_destroy(data_device);
        }
        data_device = nullptr;
    }
    if (primary_device) {
        zwp_primary_selection_device_v1_destroy(primary_device);
        primary_device = nullptr;
    }
}

void Seat::Impl::destroy_offer(Selection which, void* offer) {
    if (!offer) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        slot(which).offers.erase(offer);
    }
    if (which == Selection::Primary) {
        zwp_primary_selection_offer_v1_destroy(static_cast<zwp_primary_selection_offer_v1*>(offer));
    } else {
        wl_data_offer_destroy(static_cast<wl_data_offer*>(offer));
    }
}

void Seat::Impl::destroy_source(SelectionSource* source) {
    sources.erase(std::remove(sources.begin(), sources.end(), source), sources.end());
    if (source->which == Selection::Primary) {
        zwp_primary_selection_source_v1_destroy(static_cast<zwp_primary_selection_source_v1*>(source->proxy));
    } else {
        wl_data_source_destroy(static_cast<wl_data_source*>(source->proxy));
    }
    delete source;
}

void Seat::Impl::handle_data_offer(Selection which, void* offer) {
    std::lock_guard<std::mutex> lock(mutex);
    slot(which).offers[offer];
}

void Seat::Impl::handle_offer_mime(Selection which, void* offer, const char* mime) {
    if (!mime) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    if (which == Selection::Clipboard && offer == drag_offer) {
        drag_offer_mimes.emplace_back(mime);
        return;
    }
    slot(which).offers[offer].emplace_back(mime);
}

void Seat::Impl::handle_selection(Selection which, void* offer) {
    SelectionSlot& s = slot(which);
    void* old = nullptr;
    std::vector<std::string> mimes;
    bool emit = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        old = s.current_offer != offer ? s.current_offer : nullptr;
        s.current_offer = offer;
        if (offer) {
            mimes = s.offers[offer];
        }
        if (s.source && !s.echo_offer) {
            // Our own selection coming back to us: SelectionChangedEvent went
            // out when it was set.
            s.echo_offer = offer;
        } else {
            emit = true;
        }
    }
    if (old) {
        destroy_offer(which, old);
    }
    if (emit) {
        display->events().push(SelectionChangedEvent{seat_id(), which, std::move(mimes), false});
    }
}

void Seat::Impl::handle_source_send(SelectionSource* source, const char* mime, int fd) {
    const SelectionContents& contents = *source->contents;
    for (size_t i = 0; i < contents.size(); ++i) {
        if (mime && contents[i].first == mime) {
            write_detached(fd, source->contents, i);
            return;
        }
    }
    close(fd);
}

void Seat::Impl::handle_source_cancelled(SelectionSource* source) {
    SelectionSlot& s = slot(source->which);
    const Selection which = source->which;
    bool lost = false;
    void* stale = nullptr;
    std::vector<std::string> mimes;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (s.source == source) {
            s.source = nullptr;
            lost = true;
            if (s.echo_offer && s.echo_offer == s.current_offer) {
                // The offer of our own selection is stale now.
                stale = s.current_offer;
                s.current_offer = nullptr;
            } else if (s.current_offer) {
                mimes = s.offers[s.current_offer];
            }
            s.echo_offer = nullptr;
        }
    }
    destroy_source(source);
    if (stale) {
        destroy_offer(which, stale);
    }
    if (lost) {
        display->events().push(SelectionChangedEvent{seat_id(), which, std::move(mimes), false});
    }
}

void Seat::Impl::handle_drag_enter(uint32_t serial, wl_surface* surface, double x, double y,
                                   wl_data_offer* offer) {
    finish_drag_offer();
    const SurfaceId sid = surface_id(surface);
    std::vector<std::string> mimes;
    std::string accepted;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (offer) {
            auto it = clipboard.offers.find(offer);
            if (it != clipboard.offers.end()) {
                mimes = std::move(it->second);
                clipboard.offers.erase(it);
            }
        }
        for (const auto& want : drag_mime_types) {
            if (has_mime(mimes, want)) {
                accepted = want;
                break;
            }
        }
        drag_offer = offer;
        drag_offer_mimes = mimes;
        drag_accepted_mime = accepted;
        drag_serial = serial;
        drag_surface = sid;
        drag_x = x;
        drag_y = y;
        drag_dropped = false;
    }
    if (offer) {
        wl_data_offer_accept(offer, serial, accepted.empty() ? nullptr : accepted.c_str());
        if (wl_data_offer_get_version(offer) >= WL_DATA_OFFER_SET_ACTIONS_SINCE_VERSION) {
            const uint32_t copy = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
            wl_data_offer_set_actions(offer, accepted.empty() ? 0 : copy, copy);
        }
    }
    display->events().push(DragEnterEvent{seat_id(), sid, x, y, std::move(mimes)});
}

void Seat::Impl::handle_drag_leave() {
    SurfaceId sid;
    bool dropped;
    {
        std::lock_guard<std::mutex> lock(mutex);
        sid = drag_surface;
        dropped = drag_dropped;
    }
    // A dropped offer stays until finish_drop; any other goes with the drag.
    if (!dropped) {
        finish_drag_offer();
    }
    display->events().push(DragLeaveEvent{seat_id(), sid});
}

void Seat::Impl::handle_drag_motion(uint32_t time, double x, double y) {
    SurfaceId sid;
    {
        std::lock_guard<std::mutex> lock(mutex);
        drag_x = x;
        drag_y = y;
        sid = drag_surface;
    }
    display->events().push(DragMotionEvent{seat_id(), sid, time, x, y});
}

void Seat::Impl::handle_drop() {
    std::vector<std::string> mimes;
    SurfaceId sid;
    double x, y;
    {
        std::lock_guard<std::mutex> lock(mutex);
        drag_dropped = true;
        mimes = drag_offer_mimes;
        sid = drag_surface;
        x = drag_x;
        y = drag_y;
    }
    // Where it was dropped: the last enter or motion's position.
    display->events().push(DragDropEvent{seat_id(), sid, x, y, std::move(mimes)});
}

void Seat::Impl::finish_drag_offer() {
    wl_data_offer* offer = nullptr;
    bool finish = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        offer = drag_offer;
        finish = drag_dropped && !drag_accepted_mime.empty();
        drag_offer = nullptr;
        drag_offer_mimes.clear();
        drag_accepted_mime.clear();
        drag_dropped = false;
        drag_surface = kNoSurface;
    }
    if (!offer) {
        return;
    }
    if (finish && wl_data_offer_get_version(offer) >= WL_DATA_OFFER_FINISH_SINCE_VERSION) {
        wl_data_offer_finish(offer);
    }
    wl_data_offer_destroy(offer);
}

// ---- Seat ---------------------------------------------------------------------------

bool Seat::has_selection_protocol(Selection which) const {
    return which == Selection::Primary ? impl_->primary_device != nullptr : impl_->data_device != nullptr;
}

bool Seat::set_selection(Selection which, SelectionContents contents) {
    if (!has_selection_protocol(which)) {
        return false;
    }
    const uint32_t serial = last_input_serial();
    if (serial == 0) {
        return false;
    }
    auto& app = display_->app_globals();
    auto* src = new SelectionSource;
    src->impl = impl_.get();
    src->which = which;
    src->contents = std::make_shared<const SelectionContents>(std::move(contents));
    if (which == Selection::Primary) {
        auto* p = zwp_primary_selection_device_manager_v1_create_source(app.primary_selection_manager);
        zwp_primary_selection_source_v1_add_listener(p, &primary_source_listener, src);
        for (const auto& [mime, bytes] : *src->contents) {
            zwp_primary_selection_source_v1_offer(p, mime.c_str());
        }
        src->proxy = p;
    } else {
        auto* p = wl_data_device_manager_create_data_source(app.data_device_manager);
        wl_data_source_add_listener(p, &data_source_listener, src);
        for (const auto& [mime, bytes] : *src->contents) {
            wl_data_source_offer(p, mime.c_str());
        }
        src->proxy = p;
    }
    impl_->sources.push_back(src);
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        SelectionSlot& s = impl_->slot(which);
        // A previous source of ours is cancelled by the compositor, which
        // destroys it then.
        s.source = src;
        s.echo_offer = nullptr;
    }
    if (which == Selection::Primary) {
        zwp_primary_selection_device_v1_set_selection(impl_->primary_device,
                                                      static_cast<zwp_primary_selection_source_v1*>(src->proxy),
                                                      serial);
    } else {
        wl_data_device_set_selection(impl_->data_device, static_cast<wl_data_source*>(src->proxy), serial);
    }
    display_->events().push(SelectionChangedEvent{id_, which, mimes_of(*src->contents), true});
    return true;
}

void Seat::clear_selection(Selection which) {
    if (!has_selection_protocol(which)) {
        return;
    }
    const uint32_t serial = last_input_serial();
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->slot(which).source = nullptr;
        impl_->slot(which).echo_offer = nullptr;
    }
    if (which == Selection::Primary) {
        zwp_primary_selection_device_v1_set_selection(impl_->primary_device, nullptr, serial);
    } else {
        wl_data_device_set_selection(impl_->data_device, nullptr, serial);
    }
    display_->events().push(SelectionChangedEvent{id_, which, {}, false});
}

std::vector<std::string> Seat::selection_mime_types(Selection which) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const SelectionSlot& s = impl_->slot(which);
    if (s.source) {
        return mimes_of(*s.source->contents);
    }
    if (s.current_offer) {
        auto it = s.offers.find(s.current_offer);
        if (it != s.offers.end()) {
            return it->second;
        }
    }
    return {};
}

bool Seat::owns_selection(Selection which) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->slot(which).source != nullptr;
}

std::optional<std::vector<uint8_t>> Seat::read_selection(Selection which, const std::string& mime_type,
                                                         std::chrono::milliseconds timeout) {
    int fds[2] = {-1, -1};
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        SelectionSlot& s = impl_->slot(which);
        if (s.source) {
            for (const auto& [mime, bytes] : *s.source->contents) {
                if (mime == mime_type) {
                    return bytes;
                }
            }
            return std::nullopt;
        }
        if (!s.current_offer) {
            return std::nullopt;
        }
        auto it = s.offers.find(s.current_offer);
        if (it == s.offers.end() || !has_mime(it->second, mime_type)) {
            return std::nullopt;
        }
        if (pipe2(fds, O_CLOEXEC) != 0) {
            return std::nullopt;
        }
        // Under the lock: the dispatch thread cannot destroy the offer meanwhile.
        if (which == Selection::Primary) {
            zwp_primary_selection_offer_v1_receive(
                static_cast<zwp_primary_selection_offer_v1*>(s.current_offer), mime_type.c_str(), fds[1]);
        } else {
            wl_data_offer_receive(static_cast<wl_data_offer*>(s.current_offer), mime_type.c_str(), fds[1]);
        }
    }
    close(fds[1]);
    display_->flush();
    return read_pipe(fds[0], timeout);
}

void Seat::set_drag_mime_types(std::vector<std::string> mime_types) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->drag_mime_types = std::move(mime_types);
}

std::optional<std::vector<uint8_t>> Seat::read_drop(const std::string& mime_type,
                                                    std::chrono::milliseconds timeout) {
    int fds[2] = {-1, -1};
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->drag_offer || !impl_->drag_dropped || !has_mime(impl_->drag_offer_mimes, mime_type)) {
            return std::nullopt;
        }
        if (pipe2(fds, O_CLOEXEC) != 0) {
            return std::nullopt;
        }
        wl_data_offer_receive(impl_->drag_offer, mime_type.c_str(), fds[1]);
    }
    close(fds[1]);
    display_->flush();
    return read_pipe(fds[0], timeout);
}

void Seat::finish_drop() {
    impl_->finish_drag_offer();
}

}  // namespace browl
