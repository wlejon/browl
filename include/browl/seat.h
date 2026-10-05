#pragma once

#include "browl/events.h"
#include "browl/types.h"

#include <memory>
#include <string>

struct wl_seat;

namespace browl {

class Display;

class Seat {
public:
    Seat(SeatId id, wl_seat* wl_seat, Display* display);
    ~Seat();

    Seat(const Seat&) = delete;
    Seat& operator=(const Seat&) = delete;

    SeatId id() const { return id_; }
    wl_seat* wl_seat_ptr() const { return wl_seat_; }

    SeatSnapshot snapshot() const;

    const std::string& name() const { return name_; }
    bool has_pointer() const { return has_pointer_; }
    bool has_keyboard() const { return has_keyboard_; }
    bool has_touch() const { return has_touch_; }

    // Internal callbacks from wl_seat_listener
    void handle_capabilities(uint32_t capabilities);
    void handle_name(const char* name);
    void detach();

private:
    SeatId id_ = kNoSeat;
    wl_seat* wl_seat_ = nullptr;
    Display* display_ = nullptr;

    std::string name_;
    bool has_pointer_ = false;
    bool has_keyboard_ = false;
    bool has_touch_ = false;
};

}  // namespace browl
