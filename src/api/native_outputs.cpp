#include "host_wl_internal.h"
#include "object_builder.h"
#include "arg_reader.h"
#include "browl/output.h"
#include "browl/seat.h"

namespace browl::api {

Value outputSnapshotToJs(const browl::OutputSnapshot& snap) {
    ObjectBuilder b;
    b.set("id", static_cast<double>(snap.id));
    b.set("name", snap.name);
    b.set("make", snap.make);
    b.set("model", snap.model);
    b.set("scale", snap.scale);

    {
        ObjectBuilder geom;
        geom.set("x", snap.geometry.x);
        geom.set("y", snap.geometry.y);
        geom.set("width", snap.geometry.width);
        geom.set("height", snap.geometry.height);
        b.set("geometry", geom.build());
    }

    {
        ObjectBuilder phys;
        phys.set("width", snap.physical_size_mm.width);
        phys.set("height", snap.physical_size_mm.height);
        b.set("physicalSize", phys.build());
    }

    ev::Persistent modesArr(ev::makeArray(static_cast<uint32_t>(snap.modes.size())));
    for (uint32_t i = 0; i < snap.modes.size(); ++i) {
        ObjectBuilder mb;
        mb.set("width", snap.modes[i].width);
        mb.set("height", snap.modes[i].height);
        mb.set("refreshMhz", snap.modes[i].refresh_mhz);
        mb.set("current", snap.modes[i].current);
        mb.set("preferred", snap.modes[i].preferred);
        ev::Persistent itemP(mb.build());
        modesArr.set(ev::setElement(modesArr.get(), i, itemP.get()));
    }
    b.set("modes", modesArr.get());

    return b.build();
}

Value seatSnapshotToJs(const browl::SeatSnapshot& snap) {
    ObjectBuilder b;
    b.set("id", static_cast<double>(snap.id));
    b.set("name", snap.name);
    b.set("hasPointer", snap.has_pointer);
    b.set("hasKeyboard", snap.has_keyboard);
    b.set("hasTouch", snap.has_touch);
    return b.build();
}

Value toplevelSnapshotToJs(const browl::ForeignToplevelSnapshot& snap) {
    ObjectBuilder b;
    b.set("id", static_cast<double>(snap.id));
    b.set("title", snap.title);
    b.set("appId", snap.app_id);
    b.set("app_id", snap.app_id);
    b.set("state", snap.state);
    b.set("parentId", static_cast<double>(snap.parent_id));
    b.set("parent_id", static_cast<double>(snap.parent_id));
    b.set("isActivated", snap.is_activated());
    b.set("is_activated", snap.is_activated());
    b.set("isMaximized", snap.is_maximized());
    b.set("is_maximized", snap.is_maximized());
    b.set("isMinimized", snap.is_minimized());
    b.set("is_minimized", snap.is_minimized());
    b.set("isFullscreen", snap.is_fullscreen());
    b.set("is_fullscreen", snap.is_fullscreen());

    ev::Persistent outArr(ev::makeArray(static_cast<uint32_t>(snap.outputs.size())));
    for (uint32_t i = 0; i < snap.outputs.size(); ++i) {
        ev::Persistent idVal(ev::fromDouble(static_cast<double>(snap.outputs[i])));
        outArr.set(ev::setElement(outArr.get(), i, idVal.get()));
    }
    b.set("outputs", outArr.get());

    return b.build();
}

Value layerSurfaceSnapshotToJs(const browl::LayerSurfaceSnapshot& snap) {
    ObjectBuilder b;
    b.set("id", static_cast<double>(snap.id));
    b.set("closed", snap.closed);
    b.set("exclusiveZone", snap.exclusive_zone);

    const char* layerName = "top";
    switch (snap.layer) {
        case Layer::Background: layerName = "background"; break;
        case Layer::Bottom: layerName = "bottom"; break;
        case Layer::Top: layerName = "top"; break;
        case Layer::Overlay: layerName = "overlay"; break;
    }
    b.set("layer", layerName);

    {
        ObjectBuilder cs;
        cs.set("width", snap.configured_size.width);
        cs.set("height", snap.configured_size.height);
        b.set("configuredSize", cs.build());
    }
    b.set("configuredSerial", snap.configured_serial);

    {
        ObjectBuilder m;
        m.set("top", snap.margins.top);
        m.set("right", snap.margins.right);
        m.set("bottom", snap.margins.bottom);
        m.set("left", snap.margins.left);
        b.set("margins", m.build());
    }

    return b.build();
}

void installOutputsOnto(Value wlObj) {
    ObjectBuilder wl(wlObj);

    // bro.wl.getOutputs() -> Array<OutputSnapshot>
    wl.def("getOutputs", 0, [](Value, std::span<const Value>) -> Value {
        auto d = activeDisplay();
        if (!d) return ev::makeArray(0);
        auto outs = d->outputs();
        ev::Persistent arr(ev::makeArray(static_cast<uint32_t>(outs.size())));
        for (uint32_t i = 0; i < outs.size(); ++i) {
            ev::Persistent item(outputSnapshotToJs(outs[i]->snapshot()));
            arr.set(ev::setElement(arr.get(), i, item.get()));
        }
        return arr.get();
    });

    // bro.wl.getSeats() -> Array<SeatSnapshot>
    wl.def("getSeats", 0, [](Value, std::span<const Value>) -> Value {
        auto d = activeDisplay();
        if (!d) return ev::makeArray(0);
        auto sts = d->seats();
        ev::Persistent arr(ev::makeArray(static_cast<uint32_t>(sts.size())));
        for (uint32_t i = 0; i < sts.size(); ++i) {
            ev::Persistent item(seatSnapshotToJs(sts[i]->snapshot()));
            arr.set(ev::setElement(arr.get(), i, item.get()));
        }
        return arr.get();
    });

    // Capability queries
    wl.def("hasCompositor", 0, [](Value, std::span<const Value>) -> Value {
        auto d = activeDisplay();
        return ev::fromBool(d ? d->has_compositor() : false);
    });

    wl.def("hasShm", 0, [](Value, std::span<const Value>) -> Value {
        auto d = activeDisplay();
        return ev::fromBool(d ? d->has_shm() : false);
    });

    wl.def("hasLayerShell", 0, [](Value, std::span<const Value>) -> Value {
        auto d = activeDisplay();
        return ev::fromBool(d ? d->has_layer_shell() : false);
    });

    wl.def("hasForeignToplevelManager", 0, [](Value, std::span<const Value>) -> Value {
        auto d = activeDisplay();
        return ev::fromBool(d ? d->has_foreign_toplevel_manager() : false);
    });

    wl.def("hasSessionLock", 0, [](Value, std::span<const Value>) -> Value {
        auto d = activeDisplay();
        return ev::fromBool(d ? d->has_session_lock() : false);
    });

    wl.def("hasIdleInhibit", 0, [](Value, std::span<const Value>) -> Value {
        auto d = activeDisplay();
        return ev::fromBool(d ? d->has_idle_inhibit() : false);
    });

    wl.def("hasScreencopy", 0, [](Value, std::span<const Value>) -> Value {
        auto d = activeDisplay();
        return ev::fromBool(d ? d->has_screencopy() : false);
    });

    wl.def("hasIdleNotify", 0, [](Value, std::span<const Value>) -> Value {
        auto d = activeDisplay();
        return ev::fromBool(d ? d->has_idle_notify() : false);
    });
}

} // namespace browl::api
