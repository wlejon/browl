#include "browl/idle_inhibit.h"
#include "browl/idle_notify.h"

#include "browl/display.h"
#include "ext-idle-notify-v1-client-protocol.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"

namespace browl {

IdleInhibitor::IdleInhibitor(zwp_idle_inhibitor_v1* inhibitor, Display* display)
    : inhibitor_(inhibitor), display_(display) {}

IdleInhibitor::~IdleInhibitor() {
    if (inhibitor_) {
        zwp_idle_inhibitor_v1_destroy(inhibitor_);
    }
}

namespace {

static void idle_handle_idled(void* data, struct ext_idle_notification_v1* /*notification*/) {
    auto* notif = static_cast<IdleNotification*>(data);
    if (notif) {
        notif->handle_idled();
    }
}

static void idle_handle_resumed(void* data, struct ext_idle_notification_v1* /*notification*/) {
    auto* notif = static_cast<IdleNotification*>(data);
    if (notif) {
        notif->handle_resumed();
    }
}

static const struct ext_idle_notification_v1_listener notification_listener = {
    .idled = idle_handle_idled,
    .resumed = idle_handle_resumed,
};

}  // namespace

IdleNotification::IdleNotification(uint64_t id, ext_idle_notification_v1* notification,
                                   Display* display)
    : id_(id), notification_(notification), display_(display) {
    if (notification_) {
        ext_idle_notification_v1_add_listener(notification_, &notification_listener, this);
    }
}

IdleNotification::~IdleNotification() {
    if (notification_) {
        ext_idle_notification_v1_destroy(notification_);
    }
}

IdleNotificationSnapshot IdleNotification::snapshot() const {
    IdleNotificationSnapshot snap;
    snap.id = id_;
    snap.idled = idled_;
    return snap;
}

void IdleNotification::handle_idled() {
    idled_ = true;
    if (display_) {
        display_->events().push(IdleNotificationIdledEvent{id_});
    }
}

void IdleNotification::handle_resumed() {
    idled_ = false;
    if (display_) {
        display_->events().push(IdleNotificationResumedEvent{id_});
    }
}

}  // namespace browl
