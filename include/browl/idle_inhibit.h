#pragma once

struct zwp_idle_inhibitor_v1;
struct wl_surface;

namespace browl {

class Display;

class IdleInhibitor {
public:
    IdleInhibitor(zwp_idle_inhibitor_v1* inhibitor, Display* display);
    ~IdleInhibitor();

    IdleInhibitor(const IdleInhibitor&) = delete;
    IdleInhibitor& operator=(const IdleInhibitor&) = delete;

    zwp_idle_inhibitor_v1* zwp_inhibitor_ptr() const { return inhibitor_; }

private:
    zwp_idle_inhibitor_v1* inhibitor_ = nullptr;
    [[maybe_unused]] Display* display_ = nullptr;
};

}  // namespace browl
