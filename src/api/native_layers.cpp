#include "host_wl_internal.h"
#include "host_class.h"
#include "object_builder.h"
#include "arg_reader.h"
#include "browl/layer_surface.h"
#include "browl/output.h"

namespace browl::api {

namespace {

HostClass g_layerSurfaceClass;

std::shared_ptr<browl::LayerSurface> unwrapLayerSurface(Value v) {
    void* p = g_layerSurfaceClass.unwrap(v);
    if (!p) return nullptr;
    return *static_cast<std::shared_ptr<browl::LayerSurface>*>(p);
}

browl::Anchor parseAnchor(Value val) {
    if (ev::isNumber(val)) {
        return static_cast<browl::Anchor>(static_cast<uint32_t>(ev::toDouble(val)));
    }
    browl::Anchor a = browl::Anchor::None;
    if (ev::isObject(val)) {
        ev::Persistent valP(val);
        ev::Persistent lenProp(ev::getProperty(valP.get(), "length"));
        if (ev::isNumber(lenProp.get())) {
            uint32_t len = static_cast<uint32_t>(ev::toDouble(lenProp.get()));
            for (uint32_t i = 0; i < len; ++i) {
                ev::Persistent elem(ev::getElement(valP.get(), i));
                if (ev::isString(elem.get())) {
                    std::string s = ev::toUtf8(elem.get());
                    if (s == "top") a |= browl::Anchor::Top;
                    else if (s == "bottom") a |= browl::Anchor::Bottom;
                    else if (s == "left") a |= browl::Anchor::Left;
                    else if (s == "right") a |= browl::Anchor::Right;
                }
            }
            return a;
        }
    }
    if (ev::isString(val)) {
        std::string s = ev::toUtf8(val);
        if (s.find("top") != std::string::npos) a |= browl::Anchor::Top;
        if (s.find("bottom") != std::string::npos) a |= browl::Anchor::Bottom;
        if (s.find("left") != std::string::npos) a |= browl::Anchor::Left;
        if (s.find("right") != std::string::npos) a |= browl::Anchor::Right;
    }
    return a;
}

browl::Layer parseLayer(std::string_view s) {
    if (s == "bottom") return browl::Layer::Bottom;
    if (s == "overlay") return browl::Layer::Overlay;
    if (s == "background") return browl::Layer::Background;
    return browl::Layer::Top;
}

browl::Margins parseMargins(Value obj) {
    browl::Margins m;
    if (!ev::isObject(obj)) return m;
    m.top = ArgReader::getPropInt(obj, "top", 0);
    m.right = ArgReader::getPropInt(obj, "right", 0);
    m.bottom = ArgReader::getPropInt(obj, "bottom", 0);
    m.left = ArgReader::getPropInt(obj, "left", 0);
    return m;
}

void initLayerSurfaceClass() {
    if (g_layerSurfaceClass.installed()) return;

    g_layerSurfaceClass.init("LayerSurfaceHandle", [](ObjectBuilder& proto) {
        proto.accessor("id", [](Value self, std::span<const Value>) -> Value {
            auto l = unwrapLayerSurface(self);
            return l ? ev::fromDouble(static_cast<double>(l->id())) : ev::fromDouble(0.0);
        });

        proto.accessor("layer", [](Value self, std::span<const Value>) -> Value {
            auto l = unwrapLayerSurface(self);
            if (!l) return ev::fromUtf8("top");
            const char* name = "top";
            switch (l->snapshot().layer) {
                case Layer::Background: name = "background"; break;
                case Layer::Bottom: name = "bottom"; break;
                case Layer::Top: name = "top"; break;
                case Layer::Overlay: name = "overlay"; break;
            }
            return ev::fromUtf8(name);
        });

        proto.accessor("exclusiveZone", [](Value self, std::span<const Value>) -> Value {
            auto l = unwrapLayerSurface(self);
            return l ? ev::fromDouble(static_cast<double>(l->snapshot().exclusive_zone)) : ev::fromDouble(0.0);
        });

        proto.accessor("closed", [](Value self, std::span<const Value>) -> Value {
            auto l = unwrapLayerSurface(self);
            return ev::fromBool(l ? l->snapshot().closed : false);
        });

        proto.accessor("configuredSize", [](Value self, std::span<const Value>) -> Value {
            auto l = unwrapLayerSurface(self);
            ObjectBuilder cs;
            if (l) {
                cs.set("width", l->snapshot().configured_size.width);
                cs.set("height", l->snapshot().configured_size.height);
            } else {
                cs.set("width", 0);
                cs.set("height", 0);
            }
            return cs.build();
        });

        proto.accessor("configuredSerial", [](Value self, std::span<const Value>) -> Value {
            auto l = unwrapLayerSurface(self);
            return l ? ev::fromDouble(static_cast<double>(l->snapshot().configured_serial)) : ev::fromDouble(0.0);
        });

        // Methods
        proto.def("setSize", 2, [](Value self, std::span<const Value> args) -> Value {
            auto l = unwrapLayerSurface(self);
            if (!l) return ev::fromBool(false);
            ArgReader r(args);
            l->set_size(r.getUint(0), r.getUint(1));
            return ev::fromBool(true);
        });

        proto.def("setAnchor", 1, [](Value self, std::span<const Value> args) -> Value {
            auto l = unwrapLayerSurface(self);
            if (!l) return ev::fromBool(false);
            ArgReader r(args);
            l->set_anchor(parseAnchor(r.get(0)));
            return ev::fromBool(true);
        });

        proto.def("setMargin", 1, [](Value self, std::span<const Value> args) -> Value {
            auto l = unwrapLayerSurface(self);
            if (!l) return ev::fromBool(false);
            ArgReader r(args);
            l->set_margin(parseMargins(r.get(0)));
            return ev::fromBool(true);
        });

        proto.def("setExclusiveZone", 1, [](Value self, std::span<const Value> args) -> Value {
            auto l = unwrapLayerSurface(self);
            if (!l) return ev::fromBool(false);
            ArgReader r(args);
            l->set_exclusive_zone(r.getInt(0));
            return ev::fromBool(true);
        });

        proto.def("setLayer", 1, [](Value self, std::span<const Value> args) -> Value {
            auto l = unwrapLayerSurface(self);
            if (!l) return ev::fromBool(false);
            ArgReader r(args);
            l->set_layer(parseLayer(r.getString(0, "top")));
            return ev::fromBool(true);
        });

        proto.def("ackConfigure", 1, [](Value self, std::span<const Value> args) -> Value {
            auto l = unwrapLayerSurface(self);
            if (!l) return ev::fromBool(false);
            ArgReader r(args);
            l->ack_configure(r.getUint(0));
            return ev::fromBool(true);
        });

        proto.def("commit", 0, [](Value self, std::span<const Value>) -> Value {
            auto l = unwrapLayerSurface(self);
            if (!l) return ev::fromBool(false);
            l->commit();
            auto d = activeDisplay();
            if (d) d->flush();
            return ev::fromBool(true);
        });

        proto.def("close", 0, [](Value self, std::span<const Value>) -> Value {
            void* p = g_layerSurfaceClass.unwrap(self);
            if (!p) return ev::fromBool(false);
            auto* sp = static_cast<std::shared_ptr<browl::LayerSurface>*>(p);
            sp->reset();
            auto d = activeDisplay();
            if (d) d->flush();
            return ev::fromBool(true);
        });

        proto.def("snapshot", 0, [](Value self, std::span<const Value>) -> Value {
            auto l = unwrapLayerSurface(self);
            if (!l) return ev::createObject();
            return layerSurfaceSnapshotToJs(l->snapshot());
        });
    });
}

Value createLayerSurfaceHandle(std::shared_ptr<browl::LayerSurface> layer) {
    initLayerSurfaceClass();
    if (!layer) return ev::undefined();
    auto holder = new std::shared_ptr<browl::LayerSurface>(std::move(layer));
    return g_layerSurfaceClass.make(holder, [](void* p) {
        delete static_cast<std::shared_ptr<browl::LayerSurface>*>(p);
    });
}

browl::LayerSurfaceConfig parseLayerConfig(Value opts) {
    browl::LayerSurfaceConfig cfg;
    if (!ev::isObject(opts)) return cfg;

    cfg.name_space = ArgReader::getPropString(opts, "namespace", "browl");
    cfg.layer = parseLayer(ArgReader::getPropString(opts, "layer", "top"));

    Value anchorVal = ArgReader::getProp(opts, "anchor");
    if (!ev::isUndefined(anchorVal)) {
        cfg.anchor = parseAnchor(anchorVal);
    }

    Value marginVal = ArgReader::getProp(opts, "margin");
    if (!ev::isUndefined(marginVal)) {
        cfg.margins = parseMargins(marginVal);
    }

    int32_t width = ArgReader::getPropInt(opts, "width", 0);
    int32_t height = ArgReader::getPropInt(opts, "height", 0);
    cfg.size = browl::Size{width, height};

    if (ArgReader::hasProp(opts, "exclusiveZone")) {
        cfg.exclusive_zone = ArgReader::getPropInt(opts, "exclusiveZone", 0);
    } else if (ArgReader::hasProp(opts, "exclusive_zone")) {
        cfg.exclusive_zone = ArgReader::getPropInt(opts, "exclusive_zone", 0);
    } else if (ArgReader::hasProp(opts, "exclusive")) {
        Value ex = ArgReader::getProp(opts, "exclusive");
        if (ev::isNumber(ex)) {
            cfg.exclusive_zone = static_cast<int32_t>(ev::toDouble(ex));
        } else if (ev::isBool(ex)) {
            cfg.exclusive_zone = ev::toBool(ex) ? (height > 0 ? height : 30) : 0;
        }
    }

    auto d = activeDisplay();
    if (d && ArgReader::hasProp(opts, "output")) {
        uint32_t outId = ArgReader::getPropUint(opts, "output", 0);
        auto out = d->output_by_id(outId);
        if (out) cfg.output = out.get();
    }
    return cfg;
}

} // namespace

void installLayersOnto(Value wlObj) {
    initLayerSurfaceClass();
    ObjectBuilder wl(wlObj);

    // bro.wl.createLayerSurface(options) -> LayerSurfaceHandle
    wl.def("createLayerSurface", 1, [](Value, std::span<const Value> args) -> Value {
        auto d = activeDisplay();
        if (!d) return ev::throwError("No Wayland display available");
        ArgReader r(args);
        browl::LayerSurfaceConfig cfg = parseLayerConfig(r.get(0));
        auto surf = d->create_layer_surface(cfg);
        if (!surf) return ev::throwError("Failed to create layer surface");
        d->flush();
        std::shared_ptr<browl::LayerSurface> shared(std::move(surf));
        return createLayerSurfaceHandle(shared);
    });

    // bro.wl.setLayerRole(windowHandle, options) -> boolean
    wl.def("setLayerRole", 2, [](Value, std::span<const Value> args) -> Value {
        ArgReader r(args);
        if (r.count() == 0) return ev::fromBool(false);

        auto d = activeDisplay();
        if (!d) return ev::fromBool(false);

        Value win = r.get(0);
        Value opts = r.get(1);

        // Check if `win` is already a LayerSurface handle
        auto existing = unwrapLayerSurface(win);
        if (existing) {
            browl::LayerSurfaceConfig cfg = parseLayerConfig(opts);
            existing->set_layer(cfg.layer);
            if (cfg.anchor != browl::Anchor::None) existing->set_anchor(cfg.anchor);
            existing->set_margin(cfg.margins);
            if (cfg.exclusive_zone != 0) existing->set_exclusive_zone(cfg.exclusive_zone);
            if (cfg.size.width > 0 || cfg.size.height > 0) {
                existing->set_size(static_cast<uint32_t>(cfg.size.width),
                                   static_cast<uint32_t>(cfg.size.height));
            }
            existing->commit();
            d->flush();
            return ev::fromBool(true);
        }

        // Check if `win` is an object with an attached `_layerSurface`
        if (ev::isObject(win)) {
            Value attached = ArgReader::getProp(win, "_layerSurface");
            auto fromAttached = unwrapLayerSurface(attached);
            if (fromAttached) {
                browl::LayerSurfaceConfig cfg = parseLayerConfig(opts);
                fromAttached->set_layer(cfg.layer);
                if (cfg.anchor != browl::Anchor::None) fromAttached->set_anchor(cfg.anchor);
                fromAttached->set_margin(cfg.margins);
                if (cfg.exclusive_zone != 0) fromAttached->set_exclusive_zone(cfg.exclusive_zone);
                if (cfg.size.width > 0 || cfg.size.height > 0) {
                    fromAttached->set_size(static_cast<uint32_t>(cfg.size.width),
                                           static_cast<uint32_t>(cfg.size.height));
                }
                fromAttached->commit();
                d->flush();
                return ev::fromBool(true);
            }
        }

        // Otherwise create a new layer surface
        browl::LayerSurfaceConfig cfg = parseLayerConfig(opts);
        auto surf = d->create_layer_surface(cfg);
        if (!surf) return ev::fromBool(false);

        surf->commit();
        d->flush();

        std::shared_ptr<browl::LayerSurface> shared(std::move(surf));
        ev::Persistent handleP(createLayerSurfaceHandle(shared));

        if (ev::isObject(win)) {
            ev::Persistent winP(win);
            winP.set(ev::setProperty(winP.get(), "_layerSurface", handleP.get()));
        }

        return ev::fromBool(true);
    });
}

} // namespace browl::api
