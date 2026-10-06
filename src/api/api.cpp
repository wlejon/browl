#include "api.h"
#include "host_wl_internal.h"
#include "object_builder.h"
#include "arg_reader.h"
#include "browl/browl.h"

#include <poll.h>
#include <mutex>
#include <vector>

namespace browl::api {

// Forward declarations of screencopy handlers implemented in native_screencopy.cpp
void handleScreenCopyBufferEvent(const browl::ScreenCopyBufferEvent& e);
void handleScreenCopyReadyEvent(const browl::ScreenCopyReadyEvent& e);
void handleScreenCopyFailedEvent(const browl::ScreenCopyFailedEvent& e);

namespace {

std::mutex g_display_mu;
std::shared_ptr<browl::Display> g_custom_display;
std::shared_ptr<browl::Display> g_default_display;

struct WlListener {
    uint64_t id = 0;
    std::string event_type;
    std::shared_ptr<ev::Persistent> callback;
};

std::mutex g_listeners_mu;
uint64_t g_next_listener_id = 1;
std::vector<WlListener> g_listeners;

void fireListeners(std::string_view eventType, Value payload) {
    std::vector<std::shared_ptr<ev::Persistent>> callbacks;
    {
        std::lock_guard lock(g_listeners_mu);
        for (const auto& l : g_listeners) {
            if (l.event_type == eventType) {
                callbacks.push_back(l.callback);
            }
        }
    }
    ev::Persistent payloadP(payload);
    for (const auto& cb : callbacks) {
        if (cb && ev::isFunction(cb->get())) {
            const Value arg = payloadP.get();
            ev::call(cb->get(), ev::undefined(), std::span<const Value>(&arg, 1));
        }
    }
}

} // namespace

std::shared_ptr<browl::Display> activeDisplay() {
    std::lock_guard lock(g_display_mu);
    if (g_custom_display) {
        if (g_custom_display->has_foreign_toplevel_manager()) {
            g_custom_display->foreign_toplevel_manager();
        }
        return g_custom_display;
    }
    if (!g_default_display) {
        std::string err;
        auto d = browl::Display::connect("", &err);
        if (d) {
            g_default_display = std::shared_ptr<browl::Display>(std::move(d));
            if (g_default_display->has_foreign_toplevel_manager()) {
                g_default_display->foreign_toplevel_manager();
            }
        }
    }
    return g_default_display;
}

void setDisplay(std::shared_ptr<browl::Display> display) {
    std::lock_guard lock(g_display_mu);
    g_custom_display = std::move(display);
    if (g_custom_display && g_custom_display->has_foreign_toplevel_manager()) {
        g_custom_display->foreign_toplevel_manager();
    }
}

std::shared_ptr<browl::Display> getDisplay() {
    return activeDisplay();
}

bool available(std::string* reason) {
    return browl::available(reason);
}

Value makeError(const std::string& msg) {
    ev::Persistent text(ev::fromUtf8(msg));
    auto ctor = ev::globalValue("Error");
    if (ctor.found && ev::isFunction(ctor.value)) {
        ev::Persistent c(ctor.value);
        const Value arg = text.get();
        auto r = ev::construct(c.get(), std::span<const Value>(&arg, 1));
        if (!r.thrown) return r.value;
    }
    return text.get();
}

void clearWlListeners() {
    std::lock_guard lock(g_listeners_mu);
    g_listeners.clear();
}

void installEventListenersOnto(Value wlObj) {
    ObjectBuilder wl(wlObj);

    // bro.wl.on(event, handler) -> HandlerHandle
    auto onFn = [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (reader.count() < 2 || !reader.isString(0) || !reader.isFunction(1)) {
            return ev::throwTypeError("bro.wl.on requires (string event, function handler)");
        }

        std::string event = reader.getString(0);
        uint64_t id = 0;
        {
            std::lock_guard lock(g_listeners_mu);
            id = g_next_listener_id++;
            WlListener l;
            l.id = id;
            l.event_type = event;
            l.callback = std::make_shared<ev::Persistent>(reader.get(1));
            g_listeners.push_back(std::move(l));
        }

        ObjectBuilder handle;
        handle.set("id", static_cast<double>(id));
        handle.set("event", event);
        handle.def("remove", 0, [id](Value, std::span<const Value>) -> Value {
            std::lock_guard lock(g_listeners_mu);
            std::erase_if(g_listeners, [id](const auto& l) { return l.id == id; });
            return ev::fromBool(true);
        });
        return handle.build();
    };

    wl.def("on", 2, onFn);
    wl.def("addEventListener", 2, onFn);
    wl.def("addListener", 2, onFn);

    // bro.wl.off(eventOrHandle, handler?) -> boolean
    auto offFn = [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (reader.count() == 0) return ev::fromBool(false);

        std::lock_guard lock(g_listeners_mu);
        if (reader.isObject(0)) {
            ev::Persistent objP(reader.get(0));
            double id = ArgReader::getPropDouble(objP.get(), "id", 0.0);
            if (id > 0) {
                uint64_t targetId = static_cast<uint64_t>(id);
                size_t removed = std::erase_if(g_listeners, [targetId](const auto& l) {
                    return l.id == targetId;
                });
                return ev::fromBool(removed > 0);
            }
        }

        if (reader.isString(0)) {
            std::string event = reader.getString(0);
            if (reader.isFunction(1)) {
                Value fnVal = reader.get(1);
                size_t removed = std::erase_if(g_listeners, [&](const auto& l) {
                    return l.event_type == event && l.callback && l.callback->get() == fnVal;
                });
                return ev::fromBool(removed > 0);
            }
            size_t removed = std::erase_if(g_listeners, [&](const auto& l) {
                return l.event_type == event;
            });
            return ev::fromBool(removed > 0);
        }

        return ev::fromBool(false);
    };

    wl.def("off", 2, offFn);
    wl.def("removeEventListener", 2, offFn);
    wl.def("removeListener", 2, offFn);
}

void dispatchWlShellEvent(const browl::ShellEvent& ev) {
    std::visit([](auto&& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, browl::ToplevelCreatedEvent>) {
            Value payload = toplevelSnapshotToJs(e.toplevel);
            fireListeners("toplevelAdded", payload);
            fireListeners("toplevelCreated", payload);
        } else if constexpr (std::is_same_v<T, browl::ToplevelClosedEvent>) {
            ObjectBuilder b;
            b.set("id", static_cast<double>(e.id));
            fireListeners("toplevelClosed", b.build());
        } else if constexpr (std::is_same_v<T, browl::ToplevelDoneEvent>) {
            Value payload = toplevelSnapshotToJs(e.snapshot);
            fireListeners("toplevelChanged", payload);
            fireListeners("toplevelDone", payload);
        } else if constexpr (std::is_same_v<T, browl::LayerConfigureEvent>) {
            ObjectBuilder b;
            b.set("surfaceId", static_cast<double>(e.surface_id));
            b.set("width", e.width);
            b.set("height", e.height);
            b.set("serial", e.serial);
            fireListeners("layerConfigure", b.build());
        } else if constexpr (std::is_same_v<T, browl::LayerClosedEvent>) {
            ObjectBuilder b;
            b.set("surfaceId", static_cast<double>(e.surface_id));
            fireListeners("layerClosed", b.build());
        } else if constexpr (std::is_same_v<T, browl::SessionLockedEvent>) {
            ObjectBuilder b;
            b.set("locked", true);
            fireListeners("sessionLocked", b.build());
            fireListeners("locked", b.build());
        } else if constexpr (std::is_same_v<T, browl::SessionLockFinishedEvent>) {
            ObjectBuilder b;
            b.set("finished", true);
            fireListeners("sessionLockFinished", b.build());
            fireListeners("lockFinished", b.build());
        } else if constexpr (std::is_same_v<T, browl::SessionLockSurfaceConfigureEvent>) {
            ObjectBuilder b;
            b.set("surfaceId", static_cast<double>(e.surface_id));
            b.set("outputId", static_cast<double>(e.output_id));
            b.set("width", e.width);
            b.set("height", e.height);
            b.set("serial", e.serial);
            fireListeners("sessionLockSurfaceConfigure", b.build());
        } else if constexpr (std::is_same_v<T, browl::ScreenCopyBufferEvent>) {
            handleScreenCopyBufferEvent(e);
        } else if constexpr (std::is_same_v<T, browl::ScreenCopyReadyEvent>) {
            handleScreenCopyReadyEvent(e);
        } else if constexpr (std::is_same_v<T, browl::ScreenCopyFailedEvent>) {
            handleScreenCopyFailedEvent(e);
        } else if constexpr (std::is_same_v<T, browl::IdleNotificationIdledEvent>) {
            ObjectBuilder b;
            b.set("id", static_cast<double>(e.id));
            fireListeners("idled", b.build());
            fireListeners("idle", b.build());
        } else if constexpr (std::is_same_v<T, browl::IdleNotificationResumedEvent>) {
            ObjectBuilder b;
            b.set("id", static_cast<double>(e.id));
            fireListeners("resumed", b.build());
            fireListeners("resume", b.build());
        } else if constexpr (std::is_same_v<T, browl::OutputAddedEvent>) {
            fireListeners("outputAdded", outputSnapshotToJs(e.output));
        } else if constexpr (std::is_same_v<T, browl::OutputRemovedEvent>) {
            ObjectBuilder b;
            b.set("id", static_cast<double>(e.id));
            fireListeners("outputRemoved", b.build());
        } else if constexpr (std::is_same_v<T, browl::OutputChangedEvent>) {
            fireListeners("outputChanged", outputSnapshotToJs(e.output));
        } else if constexpr (std::is_same_v<T, browl::SeatAddedEvent>) {
            fireListeners("seatAdded", seatSnapshotToJs(e.seat));
        } else if constexpr (std::is_same_v<T, browl::SeatRemovedEvent>) {
            ObjectBuilder b;
            b.set("id", static_cast<double>(e.id));
            fireListeners("seatRemoved", b.build());
        }
    }, ev);
}

void drainWlEvents() {
    auto d = activeDisplay();
    if (!d) return;

    auto events = d->events().drain();
    for (const auto& ev : events) {
        dispatchWlShellEvent(ev);
    }
}

void tickWlAsync() {
    auto d = activeDisplay();
    if (d) {
        d->flush();
        if (d->prepare_read()) {
            struct pollfd pfd = { d->fd(), POLLIN, 0 };
            int r = poll(&pfd, 1, 0);
            if (r > 0) {
                d->read_events();
            } else {
                d->cancel_read();
            }
        }
        d->dispatch_pending();
        drainWlEvents();
    }
    bronze::embed::drainMicrotasks();
}

void shutdownWlAsync() {
    clearWlListeners();
    cleanupPendingCaptures();
    cleanupActiveLocks();
    cleanupActiveInhibitors();
    {
        std::lock_guard lock(g_display_mu);
        g_custom_display.reset();
        g_default_display.reset();
    }
}

Value ensureBroWl() {
    ev::Persistent globalThisVal;
    auto gt = ev::globalValue("globalThis");
    if (gt.found && ev::isObject(gt.value)) {
        globalThisVal.set(gt.value);
    }

    ev::Persistent broP;
    auto bro = ev::globalValue("bro");
    if (bro.found && ev::isObject(bro.value)) broP.set(bro.value);
    if (!ev::isObject(broP.get()) && ev::isObject(globalThisVal.get())) {
        Value candidate = ev::getProperty(globalThisVal.get(), "bro");
        if (ev::isObject(candidate)) broP.set(candidate);
    }
    if (!ev::isObject(broP.get())) {
        broP.set(ev::createObject());
        ev::registerGlobal("bro", broP.get());
        if (ev::isObject(globalThisVal.get())) {
            globalThisVal.set(ev::setProperty(globalThisVal.get(), "bro", broP.get()));
        }
    }

    ev::Persistent wlP(ev::getProperty(broP.get(), "wl"));
    if (!ev::isObject(wlP.get())) {
        wlP.set(ev::createObject());
        broP.set(ev::setProperty(broP.get(), "wl", wlP.get()));
    }
    return wlP.get();
}

void installWl() {
    ev::Persistent wlObj(ensureBroWl());
    ObjectBuilder wl(wlObj.get());

    std::string reason;
    bool is_avail = browl::api::available(&reason);
    wl.set("available", is_avail);
    wl.set("reason", reason);

    installOutputsOnto(wlObj.get());
    installToplevelOnto(wlObj.get());
    installLayersOnto(wlObj.get());
    installScreencopyOnto(wlObj.get());
    installLockOnto(wlObj.get());
    installEventListenersOnto(wlObj.get());
}

} // namespace browl::api
