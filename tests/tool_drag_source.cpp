// browl_drag_source: a window that drags files out, like a file manager.
//
//   browl_drag_source [--text TEXT] PATH...
//
// Press the left button on the window and move: a drag starts carrying the
// paths as a text/uri-list (file:// URIs) and as UTF-8 text (one path a
// line, or TEXT), with a small icon. Each event the compositor reports on
// the drag is printed; the program exits 0 once a drop finished, 1 when the
// drag was cancelled, and 2 when no compositor could be reached. A manual
// test client (for a drop target's file drop), not a ctest.
#include "browl/browl.h"

#include <poll.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace browl;

namespace {

std::string uri_of(const std::string& path) {
    static const char* hex = "0123456789ABCDEF";
    std::string out = "file://";
    for (unsigned char c : path) {
        if (std::isalnum(c) || std::strchr("/-_.~", c)) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

const char* kind_name(DragSourceEvent::Kind k) {
    switch (k) {
        case DragSourceEvent::Kind::Target: return "target";
        case DragSourceEvent::Kind::Action: return "action";
        case DragSourceEvent::Kind::Dropped: return "dropped";
        case DragSourceEvent::Kind::Finished: return "finished";
        case DragSourceEvent::Kind::Cancelled: return "cancelled";
    }
    return "?";
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> paths;
    std::string text;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--text") == 0 && i + 1 < argc) {
            text = argv[++i];
        } else {
            paths.emplace_back(argv[i]);
        }
    }
    if (paths.empty() && text.empty()) {
        std::fprintf(stderr, "usage: browl_drag_source [--text TEXT] PATH...\n");
        return 2;
    }
    std::string uris, lines;
    for (const auto& p : paths) {
        uris += uri_of(p) + "\r\n";
        lines += (lines.empty() ? "" : "\n") + p;
    }
    if (text.empty()) text = lines;

    std::string error;
    auto display = Display::connect("", &error);
    if (!display) {
        std::fprintf(stderr, "browl_drag_source: %s\n", error.c_str());
        return 2;
    }
    display->enable_input();
    WindowConfig cfg;
    cfg.title = "browl drag source";
    cfg.app_id = "browl.drag-source";
    auto window = display->create_window(cfg);
    if (!window) {
        std::fprintf(stderr, "browl_drag_source: no window\n");
        return 2;
    }

    constexpr int32_t kW = 240, kH = 120;
    auto pool = display->create_shm_pool(static_cast<size_t>(kW) * kH * 4);
    auto buffer = pool ? pool->allocate_buffer(kW, kH, kW * 4, 0 /* WL_SHM_FORMAT_ARGB8888 */) : nullptr;
    if (!buffer) {
        std::fprintf(stderr, "browl_drag_source: no shm buffer\n");
        return 2;
    }
    auto* px = static_cast<uint32_t*>(buffer->data());
    for (int32_t i = 0; i < kW * kH; ++i) px[i] = 0xff3a6ea5;
    display->roundtrip();
    window->attach_buffer(buffer->wl_buffer_ptr());
    window->damage(0, 0, kW, kH);
    window->commit();
    display->flush();

    SelectionContents contents;
    if (!paths.empty()) contents.emplace_back("text/uri-list", std::vector<uint8_t>(uris.begin(), uris.end()));
    for (const char* m : {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING"})
        contents.emplace_back(m, std::vector<uint8_t>(text.begin(), text.end()));
    Seat::DragIcon icon;
    icon.width = 48;
    icon.height = 32;
    icon.pixels.resize(static_cast<size_t>(icon.width) * icon.height * 4);
    for (size_t i = 0; i < icon.pixels.size(); i += 4) {
        icon.pixels[i] = 0x30;      // B
        icon.pixels[i + 1] = 0xb0;  // G
        icon.pixels[i + 2] = 0xf0;  // R
        icon.pixels[i + 3] = 0xff;  // A
    }

    bool pressed = false, dragging = false;
    for (;;) {
        display->flush();
        pollfd pfd{display->fd(), POLLIN, 0};
        if (poll(&pfd, 1, 100) > 0 && display->dispatch() < 0) return 2;
        display->dispatch_pending();
        for (auto& ev : display->events().drain()) {
            if (auto* b = std::get_if<PointerButtonEvent>(&ev)) {
                pressed = b->pressed && b->button == 0x110;
            } else if (std::get_if<PointerMotionEvent>(&ev)) {
                if (pressed && !dragging) {
                    auto seat = display->default_seat();
                    dragging = seat && seat->start_drag(window->wl_surface_ptr(), contents, &icon,
                                                        dnd_action::Copy);
                    std::printf("start_drag: %s\n", dragging ? "ok" : "refused");
                    std::fflush(stdout);
                }
            } else if (auto* d = std::get_if<DragSourceEvent>(&ev)) {
                std::printf("%s mime=%s action=%u\n", kind_name(d->kind), d->mime_type.c_str(), d->action);
                std::fflush(stdout);
                if (d->kind == DragSourceEvent::Kind::Finished) return 0;
                if (d->kind == DragSourceEvent::Kind::Cancelled) return 1;
            } else if (auto* c = std::get_if<WindowConfigureEvent>(&ev)) {
                (void)c;
                window->attach_buffer(buffer->wl_buffer_ptr());
                window->damage(0, 0, kW, kH);
                window->commit();
            } else if (std::get_if<WindowCloseEvent>(&ev)) {
                return 1;
            }
        }
    }
}
