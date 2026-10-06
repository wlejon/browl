#include "host_wl_internal.h"
#include "object_builder.h"
#include "arg_reader.h"
#include "embed/embed_typed_array.h"
#include "browl/screencopy.h"
#include "browl/shm_pool.h"
#include "browl/output.h"

#include <wayland-client.h>
#include <mutex>
#include <vector>
#include <sstream>
#include <iomanip>

namespace browl::api {

namespace {

struct PendingCapture {
    uint64_t capture_id = 0;
    std::shared_ptr<ev::Persistent> promise;
    std::unique_ptr<browl::ScreenCopyFrame> frame;
    std::shared_ptr<browl::ShmPool> pool;
    std::shared_ptr<browl::ShmBuffer> buffer;
    uint32_t format = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    bool buffer_received = false;
};

std::mutex g_captures_mu;
uint64_t g_next_capture_id = 1;
std::vector<std::shared_ptr<PendingCapture>> g_pending_captures;

std::string formatName(uint32_t fmt) {
    if (fmt == WL_SHM_FORMAT_ARGB8888) return "ARGB8888";
    if (fmt == WL_SHM_FORMAT_XRGB8888) return "XRGB8888";
    if (fmt == 0x34324258) return "XBGR8888";
    if (fmt == 0x34324241) return "ABGR8888";
    if (fmt == 0x34325258) return "RGBX8888";
    if (fmt == 0x34325241) return "RGBA8888";
    if (fmt == 0x36314752) return "RGB565";

    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << fmt;
    return oss.str();
}

} // namespace

void handleScreenCopyBufferEvent(const browl::ScreenCopyBufferEvent& e) {
    std::lock_guard lock(g_captures_mu);
    for (auto& cap : g_pending_captures) {
        if (!cap->buffer_received) {
            cap->format = e.format;
            cap->width = e.width;
            cap->height = e.height;
            cap->stride = e.stride;
            cap->buffer_received = true;

            auto d = activeDisplay();
            if (d && cap->frame) {
                size_t poolSize = static_cast<size_t>(e.stride) * e.height;
                cap->pool = d->create_shm_pool(poolSize);
                if (cap->pool) {
                    cap->buffer = cap->pool->allocate_buffer(e.width, e.height, e.stride, e.format);
                    if (cap->buffer) {
                        cap->frame->copy(cap->buffer->wl_buffer_ptr());
                        d->flush();
                    }
                }
            }
            break;
        }
    }
}

void handleScreenCopyReadyEvent(const browl::ScreenCopyReadyEvent& /*e*/) {
    std::shared_ptr<PendingCapture> resolved;
    {
        std::lock_guard lock(g_captures_mu);
        for (auto it = g_pending_captures.begin(); it != g_pending_captures.end(); ++it) {
            if ((*it)->buffer_received) {
                resolved = *it;
                g_pending_captures.erase(it);
                break;
            }
        }
    }

    if (resolved && resolved->promise && resolved->buffer) {
        uint32_t totalBytes = resolved->stride * resolved->height;
        ev::Persistent viewP(ev::createTypedArray(ev::elements::Uint8, totalBytes));
        if (resolved->buffer->data()) {
            ev::fillTypedArray(viewP.get(), std::span<const uint8_t>(
                static_cast<const uint8_t*>(resolved->buffer->data()), totalBytes));
        }

        ObjectBuilder res;
        res.set("width", resolved->width);
        res.set("height", resolved->height);
        res.set("stride", resolved->stride);
        res.set("format", formatName(resolved->format));
        res.set("pixels", viewP.get());

        ev::resolvePromise(resolved->promise->get(), res.build());
    }
}

void handleScreenCopyFailedEvent(const browl::ScreenCopyFailedEvent& e) {
    std::shared_ptr<PendingCapture> failed;
    {
        std::lock_guard lock(g_captures_mu);
        if (!g_pending_captures.empty()) {
            failed = g_pending_captures.front();
            g_pending_captures.erase(g_pending_captures.begin());
        }
    }

    if (failed && failed->promise) {
        std::string reason = e.reason.empty() ? "Screencopy frame capture failed" : e.reason;
        ev::rejectPromise(failed->promise->get(), makeError(reason));
    }
}

void cleanupPendingCaptures() {
    std::vector<std::shared_ptr<PendingCapture>> toReject;
    {
        std::lock_guard lock(g_captures_mu);
        toReject.swap(g_pending_captures);
    }
    for (auto& cap : toReject) {
        if (cap->promise) {
            ev::rejectPromise(cap->promise->get(), makeError("Capture aborted during shutdown"));
        }
    }
}

void installScreencopyOnto(Value wlObj) {
    ObjectBuilder wl(wlObj);

    // bro.wl.captureOutput(outputId?) -> Promise<{ width, height, stride, format, pixels }>
    wl.def("captureOutput", 1, [](Value, std::span<const Value> args) -> Value {
        ev::Persistent promiseP(ev::createPromise());

        auto d = activeDisplay();
        if (!d) {
            ev::rejectPromise(promiseP.get(), makeError("No Wayland display available"));
            return promiseP.get();
        }

        auto mgr = d->screencopy_manager();
        if (!mgr) {
            ev::rejectPromise(promiseP.get(), makeError("Screencopy manager unavailable"));
            return promiseP.get();
        }

        ArgReader r(args);
        std::shared_ptr<browl::Output> outObj;
        if (r.has(0) && r.isNumber(0)) {
            outObj = d->output_by_id(r.getUint(0));
        }
        if (!outObj) {
            outObj = d->default_output();
        }
        if (!outObj) {
            ev::rejectPromise(promiseP.get(), makeError("No output available for capture"));
            return promiseP.get();
        }

        auto frame = mgr->capture_output(*outObj, /*overlay_cursor=*/true);
        if (!frame) {
            ev::rejectPromise(promiseP.get(), makeError("Failed to initiate screencopy frame"));
            return promiseP.get();
        }

        d->flush();

        auto cap = std::make_shared<PendingCapture>();
        cap->capture_id = g_next_capture_id++;
        cap->promise = std::make_shared<ev::Persistent>(promiseP.get());
        cap->frame = std::move(frame);

        {
            std::lock_guard lock(g_captures_mu);
            g_pending_captures.push_back(std::move(cap));
        }

        return promiseP.get();
    });
}

} // namespace browl::api
