#include "host_wl_internal.h"
#include "host_class.h"
#include "object_builder.h"
#include "arg_reader.h"
#include "browl/foreign_toplevel.h"
#include "browl/seat.h"

namespace browl::api {

namespace {

HostClass g_toplevelClass;

std::shared_ptr<browl::ForeignToplevel> unwrapToplevel(Value v) {
    void* p = g_toplevelClass.unwrap(v);
    if (!p) return nullptr;
    return *static_cast<std::shared_ptr<browl::ForeignToplevel>*>(p);
}

void initToplevelClass() {
    if (g_toplevelClass.installed()) return;

    g_toplevelClass.init("ToplevelHandle", [](ObjectBuilder& proto) {
        proto.accessor("id", [](Value self, std::span<const Value>) -> Value {
            auto top = unwrapToplevel(self);
            return top ? ev::fromDouble(static_cast<double>(top->id())) : ev::fromDouble(0.0);
        });

        proto.accessor("title", [](Value self, std::span<const Value>) -> Value {
            auto top = unwrapToplevel(self);
            return top ? ev::fromUtf8(top->title()) : ev::fromUtf8("");
        });

        auto getAppId = [](Value self, std::span<const Value>) -> Value {
            auto top = unwrapToplevel(self);
            return top ? ev::fromUtf8(top->app_id()) : ev::fromUtf8("");
        };
        proto.accessor("appId", getAppId);
        proto.accessor("app_id", getAppId);

        proto.accessor("state", [](Value self, std::span<const Value>) -> Value {
            auto top = unwrapToplevel(self);
            return top ? ev::fromDouble(static_cast<double>(top->state())) : ev::fromDouble(0.0);
        });

        auto getParentId = [](Value self, std::span<const Value>) -> Value {
            auto top = unwrapToplevel(self);
            return top ? ev::fromDouble(static_cast<double>(top->parent_id())) : ev::fromDouble(0.0);
        };
        proto.accessor("parentId", getParentId);
        proto.accessor("parent_id", getParentId);

        auto getIsActivated = [](Value self, std::span<const Value>) -> Value {
            auto top = unwrapToplevel(self);
            return ev::fromBool(top ? top->is_activated() : false);
        };
        proto.accessor("isActivated", getIsActivated);
        proto.accessor("is_activated", getIsActivated);

        auto getIsMaximized = [](Value self, std::span<const Value>) -> Value {
            auto top = unwrapToplevel(self);
            return ev::fromBool(top ? top->is_maximized() : false);
        };
        proto.accessor("isMaximized", getIsMaximized);
        proto.accessor("is_maximized", getIsMaximized);

        auto getIsMinimized = [](Value self, std::span<const Value>) -> Value {
            auto top = unwrapToplevel(self);
            return ev::fromBool(top ? top->is_minimized() : false);
        };
        proto.accessor("isMinimized", getIsMinimized);
        proto.accessor("is_minimized", getIsMinimized);

        auto getIsFullscreen = [](Value self, std::span<const Value>) -> Value {
            auto top = unwrapToplevel(self);
            return ev::fromBool(top ? top->is_fullscreen() : false);
        };
        proto.accessor("isFullscreen", getIsFullscreen);
        proto.accessor("is_fullscreen", getIsFullscreen);

        proto.accessor("outputs", [](Value self, std::span<const Value>) -> Value {
            auto top = unwrapToplevel(self);
            if (!top) return ev::makeArray(0);
            const auto& outs = top->outputs();
            ev::Persistent arr(ev::makeArray(static_cast<uint32_t>(outs.size())));
            for (uint32_t i = 0; i < outs.size(); ++i) {
                ev::Persistent idVal(ev::fromDouble(static_cast<double>(outs[i])));
                arr.set(ev::setElement(arr.get(), i, idVal.get()));
            }
            return arr.get();
        });

        // Methods
        proto.def("activate", 1, [](Value self, std::span<const Value> args) -> Value {
            auto top = unwrapToplevel(self);
            if (!top) return ev::fromBool(false);
            auto d = activeDisplay();
            if (!d) return ev::fromBool(false);
            ArgReader r(args);
            std::shared_ptr<browl::Seat> seat;
            if (r.has(0) && r.isNumber(0)) {
                seat = d->seat_by_id(r.getUint(0));
            }
            if (!seat) {
                seat = d->default_seat();
            }
            if (seat) {
                top->activate(*seat);
                d->flush();
                return ev::fromBool(true);
            }
            return ev::fromBool(false);
        });

        proto.def("close", 0, [](Value self, std::span<const Value>) -> Value {
            auto top = unwrapToplevel(self);
            if (!top) return ev::fromBool(false);
            top->close();
            auto d = activeDisplay();
            if (d) d->flush();
            return ev::fromBool(true);
        });

        proto.def("setMaximized", 0, [](Value self, std::span<const Value>) -> Value {
            auto top = unwrapToplevel(self);
            if (!top) return ev::fromBool(false);
            top->set_maximized();
            auto d = activeDisplay();
            if (d) d->flush();
            return ev::fromBool(true);
        });

        proto.def("unsetMaximized", 0, [](Value self, std::span<const Value>) -> Value {
            auto top = unwrapToplevel(self);
            if (!top) return ev::fromBool(false);
            top->unset_maximized();
            auto d = activeDisplay();
            if (d) d->flush();
            return ev::fromBool(true);
        });

        proto.def("setMinimized", 0, [](Value self, std::span<const Value>) -> Value {
            auto top = unwrapToplevel(self);
            if (!top) return ev::fromBool(false);
            top->set_minimized();
            auto d = activeDisplay();
            if (d) d->flush();
            return ev::fromBool(true);
        });

        proto.def("unsetMinimized", 0, [](Value self, std::span<const Value>) -> Value {
            auto top = unwrapToplevel(self);
            if (!top) return ev::fromBool(false);
            top->unset_minimized();
            auto d = activeDisplay();
            if (d) d->flush();
            return ev::fromBool(true);
        });

        proto.def("setFullscreen", 1, [](Value self, std::span<const Value> args) -> Value {
            auto top = unwrapToplevel(self);
            if (!top) return ev::fromBool(false);
            auto d = activeDisplay();
            ArgReader r(args);
            browl::Output* outPtr = nullptr;
            std::shared_ptr<browl::Output> outObj;
            if (d && r.has(0) && r.isNumber(0)) {
                outObj = d->output_by_id(r.getUint(0));
                outPtr = outObj.get();
            }
            top->set_fullscreen(outPtr);
            if (d) d->flush();
            return ev::fromBool(true);
        });

        proto.def("unsetFullscreen", 0, [](Value self, std::span<const Value>) -> Value {
            auto top = unwrapToplevel(self);
            if (!top) return ev::fromBool(false);
            top->unset_fullscreen();
            auto d = activeDisplay();
            if (d) d->flush();
            return ev::fromBool(true);
        });

        proto.def("snapshot", 0, [](Value self, std::span<const Value>) -> Value {
            auto top = unwrapToplevel(self);
            if (!top) return ev::createObject();
            return toplevelSnapshotToJs(top->snapshot());
        });
    });
}

} // namespace

Value createToplevelHandle(std::shared_ptr<browl::ForeignToplevel> top) {
    initToplevelClass();
    if (!top) return ev::undefined();
    auto holder = new std::shared_ptr<browl::ForeignToplevel>(std::move(top));
    return g_toplevelClass.make(holder, [](void* p) {
        delete static_cast<std::shared_ptr<browl::ForeignToplevel>*>(p);
    });
}

void installToplevelOnto(Value wlObj) {
    initToplevelClass();
    ObjectBuilder wl(wlObj);

    // bro.wl.getToplevels() -> Array<ToplevelHandle>
    wl.def("getToplevels", 0, [](Value, std::span<const Value>) -> Value {
        auto d = activeDisplay();
        if (!d) return ev::makeArray(0);
        auto mgr = d->foreign_toplevel_manager();
        if (!mgr) return ev::makeArray(0);

        auto tops = mgr->toplevels();
        ev::Persistent arr(ev::makeArray(static_cast<uint32_t>(tops.size())));
        for (uint32_t i = 0; i < tops.size(); ++i) {
            ev::Persistent h(createToplevelHandle(tops[i]));
            arr.set(ev::setElement(arr.get(), i, h.get()));
        }
        return arr.get();
    });
}

} // namespace browl::api
