#include "browl/foreign_toplevel.h"

#include "browl/display.h"
#include "browl/output.h"
#include "browl/seat.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#include <wayland-client.h>

#include <algorithm>

namespace browl {

namespace {

static void toplevel_handle_title(void* data, struct zwlr_foreign_toplevel_handle_v1* /*handle*/,
                                  const char* title) {
    auto* toplevel = static_cast<ForeignToplevel*>(data);
    if (toplevel) {
        toplevel->handle_title(title);
    }
}

static void toplevel_handle_app_id(void* data, struct zwlr_foreign_toplevel_handle_v1* /*handle*/,
                                   const char* app_id) {
    auto* toplevel = static_cast<ForeignToplevel*>(data);
    if (toplevel) {
        toplevel->handle_app_id(app_id);
    }
}

static void toplevel_handle_output_enter(void* data,
                                        struct zwlr_foreign_toplevel_handle_v1* /*handle*/,
                                        struct wl_output* output) {
    auto* toplevel = static_cast<ForeignToplevel*>(data);
    if (toplevel) {
        toplevel->handle_output_enter(output);
    }
}

static void toplevel_handle_output_leave(void* data,
                                        struct zwlr_foreign_toplevel_handle_v1* /*handle*/,
                                        struct wl_output* output) {
    auto* toplevel = static_cast<ForeignToplevel*>(data);
    if (toplevel) {
        toplevel->handle_output_leave(output);
    }
}

static void toplevel_handle_state(void* data, struct zwlr_foreign_toplevel_handle_v1* /*handle*/,
                                  struct wl_array* state) {
    auto* toplevel = static_cast<ForeignToplevel*>(data);
    if (toplevel) {
        toplevel->handle_state(state);
    }
}

static void toplevel_handle_done(void* data, struct zwlr_foreign_toplevel_handle_v1* /*handle*/) {
    auto* toplevel = static_cast<ForeignToplevel*>(data);
    if (toplevel) {
        toplevel->handle_done();
    }
}

static void toplevel_handle_closed(void* data, struct zwlr_foreign_toplevel_handle_v1* /*handle*/) {
    auto* toplevel = static_cast<ForeignToplevel*>(data);
    if (toplevel) {
        toplevel->handle_closed();
    }
}

static void toplevel_handle_parent(void* data, struct zwlr_foreign_toplevel_handle_v1* /*handle*/,
                                   struct zwlr_foreign_toplevel_handle_v1* parent) {
    auto* toplevel = static_cast<ForeignToplevel*>(data);
    if (toplevel) {
        toplevel->handle_parent(parent);
    }
}

static const struct zwlr_foreign_toplevel_handle_v1_listener toplevel_listener = {
    .title = toplevel_handle_title,
    .app_id = toplevel_handle_app_id,
    .output_enter = toplevel_handle_output_enter,
    .output_leave = toplevel_handle_output_leave,
    .state = toplevel_handle_state,
    .done = toplevel_handle_done,
    .closed = toplevel_handle_closed,
    .parent = toplevel_handle_parent,
};

static void manager_handle_toplevel(void* data,
                                    struct zwlr_foreign_toplevel_manager_v1* /*manager*/,
                                    struct zwlr_foreign_toplevel_handle_v1* toplevel) {
    auto* mgr = static_cast<ForeignToplevelManager*>(data);
    if (mgr) {
        mgr->handle_toplevel(toplevel);
    }
}

static void manager_handle_finished(void* data,
                                    struct zwlr_foreign_toplevel_manager_v1* /*manager*/) {
    auto* mgr = static_cast<ForeignToplevelManager*>(data);
    if (mgr) {
        mgr->handle_finished();
    }
}

static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
    .toplevel = manager_handle_toplevel,
    .finished = manager_handle_finished,
};

}  // namespace

ForeignToplevel::ForeignToplevel(ToplevelId id, zwlr_foreign_toplevel_handle_v1* handle,
                                 ForeignToplevelManager* manager, Display* display)
    : id_(id), handle_(handle), manager_(manager), display_(display) {
    pending_.id = id;
    if (handle_) {
        zwlr_foreign_toplevel_handle_v1_add_listener(handle_, &toplevel_listener, this);
    }
}

ForeignToplevel::~ForeignToplevel() {
    detach();
}

void ForeignToplevel::detach() {
    if (handle_) {
        zwlr_foreign_toplevel_handle_v1_destroy(handle_);
        handle_ = nullptr;
    }
}

ForeignToplevelSnapshot ForeignToplevel::snapshot() const {
    return current_;
}

void ForeignToplevel::activate(Seat& seat) {
    if (handle_) {
        zwlr_foreign_toplevel_handle_v1_activate(handle_, seat.wl_seat_ptr());
    }
}

void ForeignToplevel::close() {
    if (handle_) {
        zwlr_foreign_toplevel_handle_v1_close(handle_);
    }
}

void ForeignToplevel::set_maximized() {
    if (handle_) {
        zwlr_foreign_toplevel_handle_v1_set_maximized(handle_);
    }
}

void ForeignToplevel::unset_maximized() {
    if (handle_) {
        zwlr_foreign_toplevel_handle_v1_unset_maximized(handle_);
    }
}

void ForeignToplevel::set_minimized() {
    if (handle_) {
        zwlr_foreign_toplevel_handle_v1_set_minimized(handle_);
    }
}

void ForeignToplevel::unset_minimized() {
    if (handle_) {
        zwlr_foreign_toplevel_handle_v1_unset_minimized(handle_);
    }
}

void ForeignToplevel::set_fullscreen(Output* output) {
    if (handle_) {
        zwlr_foreign_toplevel_handle_v1_set_fullscreen(
            handle_, output ? output->wl_output_ptr() : nullptr);
    }
}

void ForeignToplevel::unset_fullscreen() {
    if (handle_) {
        zwlr_foreign_toplevel_handle_v1_unset_fullscreen(handle_);
    }
}

void ForeignToplevel::set_rectangle(wl_surface* surface, int32_t x, int32_t y,
                                    int32_t width, int32_t height) {
    if (handle_) {
        zwlr_foreign_toplevel_handle_v1_set_rectangle(handle_, surface, x, y, width, height);
    }
}

void ForeignToplevel::handle_title(const char* title) {
    pending_.title = title ? title : "";
    if (display_) {
        display_->events().push(ToplevelTitleEvent{id_, pending_.title});
    }
}

void ForeignToplevel::handle_app_id(const char* app_id) {
    pending_.app_id = app_id ? app_id : "";
    if (display_) {
        display_->events().push(ToplevelAppIdEvent{id_, pending_.app_id});
    }
}

void ForeignToplevel::handle_output_enter(wl_output* output) {
    OutputId out_id = kNoOutput;
    if (display_) {
        for (const auto& out : display_->outputs()) {
            if (out->wl_output_ptr() == output) {
                out_id = out->id();
                break;
            }
        }
    }

    if (std::find(pending_.outputs.begin(), pending_.outputs.end(), out_id) ==
        pending_.outputs.end()) {
        pending_.outputs.push_back(out_id);
    }

    if (display_) {
        display_->events().push(ToplevelOutputEnterEvent{id_, out_id});
    }
}

void ForeignToplevel::handle_output_leave(wl_output* output) {
    OutputId out_id = kNoOutput;
    if (display_) {
        for (const auto& out : display_->outputs()) {
            if (out->wl_output_ptr() == output) {
                out_id = out->id();
                break;
            }
        }
    }

    auto it = std::remove(pending_.outputs.begin(), pending_.outputs.end(), out_id);
    pending_.outputs.erase(it, pending_.outputs.end());

    if (display_) {
        display_->events().push(ToplevelOutputLeaveEvent{id_, out_id});
    }
}

void ForeignToplevel::handle_state(struct wl_array* state) {
    uint32_t st = 0;
    if (state && state->data) {
        auto* entries = static_cast<const uint32_t*>(state->data);
        size_t count = state->size / sizeof(uint32_t);
        for (size_t i = 0; i < count; ++i) {
            switch (entries[i]) {
            case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED:
                st |= toplevel_state::Maximized;
                break;
            case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED:
                st |= toplevel_state::Minimized;
                break;
            case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED:
                st |= toplevel_state::Activated;
                break;
            case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN:
                st |= toplevel_state::Fullscreen;
                break;
            default:
                break;
            }
        }
    }
    pending_.state = st;
    if (display_) {
        display_->events().push(ToplevelStateEvent{id_, pending_.state});
    }
}

void ForeignToplevel::handle_done() {
    bool is_new = (current_.id == kNoToplevel);
    current_ = pending_;
    if (display_) {
        if (is_new) {
            display_->events().push(ToplevelCreatedEvent{current_});
        } else {
            display_->events().push(ToplevelDoneEvent{id_, current_});
        }
    }
}

void ForeignToplevel::handle_closed() {
    if (display_) {
        display_->events().push(ToplevelClosedEvent{id_});
    }
    detach();
    if (manager_) {
        manager_->remove_toplevel(id_);
    }
}

void ForeignToplevel::handle_parent(zwlr_foreign_toplevel_handle_v1* parent) {
    ToplevelId pid = kNoToplevel;
    if (parent && manager_) {
        for (const auto& top : manager_->toplevels()) {
            if (top->zwlr_handle_ptr() == parent) {
                pid = top->id();
                break;
            }
        }
    }
    pending_.parent_id = pid;
    if (display_) {
        display_->events().push(ToplevelParentEvent{id_, pid});
    }
}

ForeignToplevelManager::ForeignToplevelManager(zwlr_foreign_toplevel_manager_v1* manager,
                                             Display* display)
    : manager_(manager), display_(display) {
    if (manager_) {
        zwlr_foreign_toplevel_manager_v1_add_listener(manager_, &manager_listener, this);
    }
}

ForeignToplevelManager::~ForeignToplevelManager() {
    detach();
}

void ForeignToplevelManager::detach() {
    for (auto& top : toplevels_) {
        top->detach();
    }
    toplevels_.clear();
    if (manager_) {
        zwlr_foreign_toplevel_manager_v1_destroy(manager_);
        manager_ = nullptr;
    }
}

std::vector<std::shared_ptr<ForeignToplevel>> ForeignToplevelManager::toplevels() const {
    return toplevels_;
}

std::shared_ptr<ForeignToplevel> ForeignToplevelManager::find_toplevel(ToplevelId id) const {
    for (const auto& top : toplevels_) {
        if (top->id() == id) {
            return top;
        }
    }
    return nullptr;
}

std::vector<ForeignToplevelSnapshot> ForeignToplevelManager::snapshots() const {
    std::vector<ForeignToplevelSnapshot> list;
    list.reserve(toplevels_.size());
    for (const auto& top : toplevels_) {
        list.push_back(top->snapshot());
    }
    return list;
}

void ForeignToplevelManager::handle_toplevel(zwlr_foreign_toplevel_handle_v1* handle) {
    ToplevelId id = display_ ? display_->next_surface_id() : 1;
    auto top = std::make_shared<ForeignToplevel>(id, handle, this, display_);
    toplevels_.push_back(top);
}

void ForeignToplevelManager::handle_finished() {
    toplevels_.clear();
}

void ForeignToplevelManager::remove_toplevel(ToplevelId id) {
    auto it = std::remove_if(toplevels_.begin(), toplevels_.end(),
                             [id](const std::shared_ptr<ForeignToplevel>& top) {
                                 return top->id() == id;
                             });
    toplevels_.erase(it, toplevels_.end());
}

}  // namespace browl
