#pragma once

#include "browl/events.h"
#include "browl/types.h"

#include <memory>
#include <string>
#include <vector>

struct zwlr_foreign_toplevel_manager_v1;
struct zwlr_foreign_toplevel_handle_v1;
struct wl_surface;
struct wl_output;
struct wl_array;

namespace browl {

class Display;
class Seat;
class Output;
class ForeignToplevelManager;

class ForeignToplevel : public std::enable_shared_from_this<ForeignToplevel> {
public:
    ForeignToplevel(ToplevelId id, zwlr_foreign_toplevel_handle_v1* handle,
                    ForeignToplevelManager* manager, Display* display);
    ~ForeignToplevel();

    ForeignToplevel(const ForeignToplevel&) = delete;
    ForeignToplevel& operator=(const ForeignToplevel&) = delete;

    ToplevelId id() const { return id_; }
    zwlr_foreign_toplevel_handle_v1* zwlr_handle_ptr() const { return handle_; }

    ForeignToplevelSnapshot snapshot() const;

    const std::string& title() const { return current_.title; }
    const std::string& app_id() const { return current_.app_id; }
    uint32_t state() const { return current_.state; }
    ToplevelId parent_id() const { return current_.parent_id; }
    const std::vector<OutputId>& outputs() const { return current_.outputs; }

    bool is_activated() const { return current_.is_activated(); }
    bool is_maximized() const { return current_.is_maximized(); }
    bool is_minimized() const { return current_.is_minimized(); }
    bool is_fullscreen() const { return current_.is_fullscreen(); }

    void activate(Seat& seat);
    void close();
    void set_maximized();
    void unset_maximized();
    void set_minimized();
    void unset_minimized();
    void set_fullscreen(Output* output = nullptr);
    void unset_fullscreen();
    void set_rectangle(wl_surface* surface, int32_t x, int32_t y, int32_t width, int32_t height);
    void detach();

    // Internal listener callbacks
    void handle_title(const char* title);
    void handle_app_id(const char* app_id);
    void handle_output_enter(wl_output* output);
    void handle_output_leave(wl_output* output);
    void handle_state(struct wl_array* state);
    void handle_done();
    void handle_closed();
    void handle_parent(zwlr_foreign_toplevel_handle_v1* parent);

private:
    ToplevelId id_ = kNoToplevel;
    zwlr_foreign_toplevel_handle_v1* handle_ = nullptr;
    ForeignToplevelManager* manager_ = nullptr;
    Display* display_ = nullptr;

    ForeignToplevelSnapshot pending_;
    ForeignToplevelSnapshot current_;
};

class ForeignToplevelManager {
public:
    ForeignToplevelManager(zwlr_foreign_toplevel_manager_v1* manager, Display* display);
    ~ForeignToplevelManager();

    ForeignToplevelManager(const ForeignToplevelManager&) = delete;
    ForeignToplevelManager& operator=(const ForeignToplevelManager&) = delete;

    zwlr_foreign_toplevel_manager_v1* zwlr_manager_ptr() const { return manager_; }

    std::vector<std::shared_ptr<ForeignToplevel>> toplevels() const;
    std::shared_ptr<ForeignToplevel> find_toplevel(ToplevelId id) const;
    std::vector<ForeignToplevelSnapshot> snapshots() const;
    void detach();

    // Internal listener callbacks
    void handle_toplevel(zwlr_foreign_toplevel_handle_v1* handle);
    void handle_finished();
    void remove_toplevel(ToplevelId id);

private:
    zwlr_foreign_toplevel_manager_v1* manager_ = nullptr;
    Display* display_ = nullptr;

    std::vector<std::shared_ptr<ForeignToplevel>> toplevels_;
};

}  // namespace browl
