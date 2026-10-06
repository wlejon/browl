// A browl Display connected to the in-process HeadlessCompositor over a
// socketpair, plus helpers for looking through drained events.
//
// HeadlessCompositor is a test double: a hand-written wayland-server that
// advertises every global browl binds and sends exactly the events a test
// scripts (configure, closed, locked, idled, ...). It checks what browl
// marshals and how browl turns events into snapshots and queue entries; it
// does not check that a real compositor accepts what browl sends. That is
// test_sway's job (a real wlroots compositor, headless).
#pragma once

#include "browl/display.h"
#include "check.h"
#include "headless_compositor.h"

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace bstest {

struct FakeSession {
    browl::test::HeadlessCompositor server;
    std::unique_ptr<browl::Display> display;

    FakeSession() {
        server.start();
        const int fd = server.create_client_fd();
        if (fd < 0) {
            fail(__FILE__, __LINE__, "socketpair to the headless compositor failed");
            return;
        }
        std::string error;
        display = browl::Display::connect_to_fd(fd, &error);
        if (!display) fail(__FILE__, __LINE__, "connect_to_fd: " + error);
    }

    ~FakeSession() {
        display.reset();
        server.stop();
    }
};

// The first event of type E in `events` that satisfies `pred`, or nullptr.
template <class E, class Pred>
const E* find_event(const std::vector<browl::ShellEvent>& events, Pred pred) {
    for (const auto& ev : events) {
        if (const E* e = std::get_if<E>(&ev); e && pred(*e)) return e;
    }
    return nullptr;
}

template <class E>
const E* find_event(const std::vector<browl::ShellEvent>& events) {
    return find_event<E>(events, [](const E&) { return true; });
}

}  // namespace bstest
