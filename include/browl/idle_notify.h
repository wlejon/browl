#pragma once

#include "browl/events.h"
#include "browl/types.h"

#include <cstdint>
#include <memory>

struct ext_idle_notification_v1;

namespace browl {

class Display;
class Seat;

class IdleNotification {
public:
    IdleNotification(uint64_t id, ext_idle_notification_v1* notification, Display* display);
    ~IdleNotification();

    IdleNotification(const IdleNotification&) = delete;
    IdleNotification& operator=(const IdleNotification&) = delete;

    uint64_t id() const { return id_; }
    bool is_idled() const { return idled_; }
    IdleNotificationSnapshot snapshot() const;

    ext_idle_notification_v1* ext_notification_ptr() const { return notification_; }

    // Internal listener callbacks
    void handle_idled();
    void handle_resumed();

private:
    uint64_t id_ = 0;
    ext_idle_notification_v1* notification_ = nullptr;
    Display* display_ = nullptr;
    bool idled_ = false;
};

}  // namespace browl
