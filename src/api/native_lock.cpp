#include "host_wl_internal.h"
#include "host_class.h"
#include "object_builder.h"
#include "arg_reader.h"
#include "browl/session_lock.h"
#include "browl/idle_inhibit.h"
#include "browl/idle_notify.h"
#include "browl/output.h"
#include "browl/seat.h"

#include <wayland-client.h>
#include <mutex>
#include <vector>

namespace browl::api {

namespace {

HostClass g_sessionLockClass;
HostClass g_sessionLockSurfaceClass;
HostClass g_idleInhibitorClass;
HostClass g_idleNotificationClass;

std::mutex g_locks_mu;
std::vector<std::shared_ptr<browl::SessionLock>> g_active_locks;

struct InhibitorHolder {
    std::unique_ptr<browl::IdleInhibitor> inhibitor;
    wl_surface* owned_surface = nullptr;
    bool active = true;

    void release() {
        if (!active) return;
        inhibitor.reset();
        if (owned_surface) {
            wl_surface_destroy(owned_surface);
            owned_surface = nullptr;
        }
        active = false;
    }

    ~InhibitorHolder() {
        release();
    }
};

std::mutex g_inhibitors_mu;
std::vector<std::shared_ptr<InhibitorHolder>> g_active_inhibitors;

std::shared_ptr<browl::SessionLock> unwrapLock(Value v) {
    void* p = g_sessionLockClass.unwrap(v);
    if (!p) return nullptr;
    return *static_cast<std::shared_ptr<browl::SessionLock>*>(p);
}

std::shared_ptr<browl::SessionLockSurface> unwrapLockSurface(Value v) {
    void* p = g_sessionLockSurfaceClass.unwrap(v);
    if (!p) return nullptr;
    return *static_cast<std::shared_ptr<browl::SessionLockSurface>*>(p);
}

std::shared_ptr<InhibitorHolder> unwrapInhibitor(Value v) {
    void* p = g_idleInhibitorClass.unwrap(v);
    if (!p) return nullptr;
    return *static_cast<std::shared_ptr<InhibitorHolder>*>(p);
}

std::shared_ptr<browl::IdleNotification> unwrapNotification(Value v) {
    void* p = g_idleNotificationClass.unwrap(v);
    if (!p) return nullptr;
    return *static_cast<std::shared_ptr<browl::IdleNotification>*>(p);
}

Value createSessionLockSurfaceHandle(std::shared_ptr<browl::SessionLockSurface> surf) {
    if (!surf) return ev::undefined();
    auto holder = new std::shared_ptr<browl::SessionLockSurface>(std::move(surf));
    return g_sessionLockSurfaceClass.make(holder, [](void* p) {
        delete static_cast<std::shared_ptr<browl::SessionLockSurface>*>(p);
    });
}

void initLockClasses() {
    if (!g_sessionLockSurfaceClass.installed()) {
        g_sessionLockSurfaceClass.init("SessionLockSurfaceHandle", [](ObjectBuilder& proto) {
            proto.accessor("id", [](Value self, std::span<const Value>) -> Value {
                auto s = unwrapLockSurface(self);
                return s ? ev::fromDouble(static_cast<double>(s->id())) : ev::fromDouble(0.0);
            });

            proto.accessor("outputId", [](Value self, std::span<const Value>) -> Value {
                auto s = unwrapLockSurface(self);
                return s ? ev::fromDouble(static_cast<double>(s->output_id())) : ev::fromDouble(0.0);
            });

            proto.accessor("configuredSize", [](Value self, std::span<const Value>) -> Value {
                auto s = unwrapLockSurface(self);
                ObjectBuilder b;
                if (s) {
                    b.set("width", s->configured_size().width);
                    b.set("height", s->configured_size().height);
                } else {
                    b.set("width", 0);
                    b.set("height", 0);
                }
                return b.build();
            });

            proto.accessor("configuredSerial", [](Value self, std::span<const Value>) -> Value {
                auto s = unwrapLockSurface(self);
                return s ? ev::fromDouble(static_cast<double>(s->configured_serial())) : ev::fromDouble(0.0);
            });

            proto.def("ackConfigure", 1, [](Value self, std::span<const Value> args) -> Value {
                auto s = unwrapLockSurface(self);
                if (!s) return ev::fromBool(false);
                ArgReader r(args);
                s->ack_configure(r.getUint(0));
                return ev::fromBool(true);
            });

            proto.def("commit", 0, [](Value self, std::span<const Value>) -> Value {
                auto s = unwrapLockSurface(self);
                if (!s) return ev::fromBool(false);
                s->commit();
                auto d = activeDisplay();
                if (d) d->flush();
                return ev::fromBool(true);
            });
        });
    }

    if (!g_sessionLockClass.installed()) {
        g_sessionLockClass.init("SessionLockHandle", [](ObjectBuilder& proto) {
            proto.accessor("isLocked", [](Value self, std::span<const Value>) -> Value {
                auto l = unwrapLock(self);
                return ev::fromBool(l ? l->is_locked() : false);
            });

            proto.accessor("isFinished", [](Value self, std::span<const Value>) -> Value {
                auto l = unwrapLock(self);
                return ev::fromBool(l ? l->is_finished() : false);
            });

            proto.def("unlock", 0, [](Value self, std::span<const Value>) -> Value {
                auto l = unwrapLock(self);
                if (!l) return ev::fromBool(false);
                l->unlock_and_destroy();
                auto d = activeDisplay();
                if (d) d->flush();
                return ev::fromBool(true);
            });

            proto.def("createSurface", 1, [](Value self, std::span<const Value> args) -> Value {
                auto l = unwrapLock(self);
                if (!l) return ev::throwError("Invalid session lock");
                auto d = activeDisplay();
                if (!d) return ev::throwError("No Wayland display available");

                ArgReader r(args);
                std::shared_ptr<browl::Output> outObj;
                if (r.has(0) && r.isNumber(0)) {
                    outObj = d->output_by_id(r.getUint(0));
                }
                if (!outObj) {
                    outObj = d->default_output();
                }
                if (!outObj) {
                    return ev::throwError("No output available for lock surface");
                }

                auto surf = l->create_surface(*outObj);
                if (!surf) {
                    return ev::throwError("Failed to create lock surface");
                }
                d->flush();
                return createSessionLockSurfaceHandle(std::shared_ptr<browl::SessionLockSurface>(std::move(surf)));
            });

            proto.def("snapshot", 0, [](Value self, std::span<const Value>) -> Value {
                auto l = unwrapLock(self);
                ObjectBuilder b;
                b.set("locked", l ? l->is_locked() : false);
                b.set("finished", l ? l->is_finished() : false);
                return b.build();
            });
        });
    }

    if (!g_idleInhibitorClass.installed()) {
        g_idleInhibitorClass.init("IdleInhibitHandle", [](ObjectBuilder& proto) {
            proto.accessor("active", [](Value self, std::span<const Value>) -> Value {
                auto h = unwrapInhibitor(self);
                return ev::fromBool(h ? h->active : false);
            });

            auto releaseFn = [](Value self, std::span<const Value>) -> Value {
                auto h = unwrapInhibitor(self);
                if (!h) return ev::fromBool(false);
                h->release();
                auto d = activeDisplay();
                if (d) d->flush();
                return ev::fromBool(true);
            };
            proto.def("release", 0, releaseFn);
            proto.def("restore", 0, releaseFn);
            proto.def("destroy", 0, releaseFn);
        });
    }

    if (!g_idleNotificationClass.installed()) {
        g_idleNotificationClass.init("IdleNotificationHandle", [](ObjectBuilder& proto) {
            proto.accessor("id", [](Value self, std::span<const Value>) -> Value {
                auto notif = unwrapNotification(self);
                return notif ? ev::fromDouble(static_cast<double>(notif->id())) : ev::fromDouble(0.0);
            });

            proto.accessor("isIdled", [](Value self, std::span<const Value>) -> Value {
                auto notif = unwrapNotification(self);
                return ev::fromBool(notif ? notif->is_idled() : false);
            });

            proto.def("destroy", 0, [](Value self, std::span<const Value>) -> Value {
                void* p = g_idleNotificationClass.unwrap(self);
                if (!p) return ev::fromBool(false);
                auto* sp = static_cast<std::shared_ptr<browl::IdleNotification>*>(p);
                sp->reset();
                auto d = activeDisplay();
                if (d) d->flush();
                return ev::fromBool(true);
            });
        });
    }
}

} // namespace

void cleanupActiveLocks() {
    std::vector<std::shared_ptr<browl::SessionLock>> toClean;
    {
        std::lock_guard lock(g_locks_mu);
        toClean.swap(g_active_locks);
    }
    for (auto& l : toClean) {
        if (l && l->is_locked()) {
            l->unlock_and_destroy();
        }
    }
}

void cleanupActiveInhibitors() {
    std::vector<std::shared_ptr<InhibitorHolder>> toClean;
    {
        std::lock_guard lock(g_inhibitors_mu);
        toClean.swap(g_active_inhibitors);
    }
    for (auto& inh : toClean) {
        if (inh) {
            inh->release();
        }
    }
}

void installLockOnto(Value wlObj) {
    initLockClasses();
    ObjectBuilder wl(wlObj);

    // bro.wl.acquireSessionLock() -> SessionLockHandle
    wl.def("acquireSessionLock", 0, [](Value, std::span<const Value>) -> Value {
        auto d = activeDisplay();
        if (!d) return ev::throwError("No Wayland display available");
        auto lock = d->create_session_lock();
        if (!lock) return ev::throwError("Session lock manager unavailable or lock failed");
        d->flush();

        auto shared = std::shared_ptr<browl::SessionLock>(std::move(lock));
        {
            std::lock_guard g(g_locks_mu);
            g_active_locks.push_back(shared);
        }

        auto holder = new std::shared_ptr<browl::SessionLock>(shared);
        return g_sessionLockClass.make(holder, [](void* p) {
            delete static_cast<std::shared_ptr<browl::SessionLock>*>(p);
        });
    });

    // bro.wl.inhibitIdle(windowHandle?) -> IdleInhibitHandle
    wl.def("inhibitIdle", 1, [](Value, std::span<const Value> /*args*/) -> Value {
        auto d = activeDisplay();
        if (!d) return ev::throwError("No Wayland display available");

        wl_surface* surface = d->create_surface();
        if (!surface) return ev::throwError("Failed to create surface for idle inhibition");

        auto inh = d->create_idle_inhibitor(surface);
        if (!inh) {
            wl_surface_destroy(surface);
            return ev::throwError("Failed to create idle inhibitor");
        }
        d->flush();

        auto holderObj = std::make_shared<InhibitorHolder>();
        holderObj->inhibitor = std::move(inh);
        holderObj->owned_surface = surface;
        holderObj->active = true;

        {
            std::lock_guard g(g_inhibitors_mu);
            g_active_inhibitors.push_back(holderObj);
        }

        auto ptr = new std::shared_ptr<InhibitorHolder>(holderObj);
        return g_idleInhibitorClass.make(ptr, [](void* p) {
            delete static_cast<std::shared_ptr<InhibitorHolder>*>(p);
        });
    });

    // bro.wl.createIdleNotification(timeoutMs, seatId?) -> IdleNotificationHandle
    wl.def("createIdleNotification", 2, [](Value, std::span<const Value> args) -> Value {
        auto d = activeDisplay();
        if (!d) return ev::throwError("No Wayland display available");

        ArgReader r(args);
        uint32_t timeoutMs = r.getUint(0, 30000);
        browl::Seat* seatPtr = nullptr;
        std::shared_ptr<browl::Seat> seatObj;
        if (r.has(1) && r.isNumber(1)) {
            seatObj = d->seat_by_id(r.getUint(1));
            seatPtr = seatObj.get();
        }
        if (!seatPtr) {
            seatObj = d->default_seat();
            seatPtr = seatObj.get();
        }
        if (!seatPtr) {
            return ev::throwError("No seat available for idle notification");
        }

        auto notif = d->create_idle_notification(timeoutMs, seatPtr);
        if (!notif) return ev::throwError("Idle notification unavailable");
        d->flush();

        auto shared = std::shared_ptr<browl::IdleNotification>(std::move(notif));
        auto ptr = new std::shared_ptr<browl::IdleNotification>(shared);
        return g_idleNotificationClass.make(ptr, [](void* p) {
            delete static_cast<std::shared_ptr<browl::IdleNotification>*>(p);
        });
    });
}

} // namespace browl::api
