#include "browl/event_queue.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace browl;

int main() {
    std::cout << "Running test_event_queue..." << std::endl;

    EventQueue queue;
    assert(queue.size() == 0);
    assert(queue.empty());

    // Basic push and drain
    queue.push(LayerClosedEvent{42});
    queue.push(PopupDoneEvent{101});
    assert(queue.size() == 2);
    assert(!queue.empty());

    auto drained = queue.drain();
    assert(drained.size() == 2);
    assert(queue.empty());
    assert(queue.size() == 0);

    assert(std::holds_alternative<LayerClosedEvent>(drained[0]));
    assert(std::get<LayerClosedEvent>(drained[0]).surface_id == 42);
    assert(std::holds_alternative<PopupDoneEvent>(drained[1]));
    assert(std::get<PopupDoneEvent>(drained[1]).surface_id == 101);

    // wait_for timeout when empty
    auto start = std::chrono::steady_clock::now();
    bool waited = queue.wait_for(std::chrono::milliseconds(50));
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    assert(!waited);
    assert(elapsed.count() >= 40);

    // wait_for wake on push from another thread
    std::thread t([&queue]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        queue.push(SessionLockedEvent{});
    });
    waited = queue.wait_for(std::chrono::milliseconds(200));
    assert(waited);
    assert(queue.size() == 1);
    t.join();
    auto item = queue.drain();
    assert(item.size() == 1);
    assert(std::holds_alternative<SessionLockedEvent>(item[0]));

    // wake callback
    bool wake_called = false;
    queue.set_wake([&wake_called]() {
        wake_called = true;
    });
    queue.push(SessionLockFinishedEvent{});
    assert(wake_called);
    queue.drain();

    // Multi-threaded concurrent push
    constexpr int kNumThreads = 4;
    constexpr int kEventsPerThread = 100;
    std::vector<std::thread> producers;
    for (int i = 0; i < kNumThreads; ++i) {
        producers.emplace_back([&queue, i]() {
            for (int j = 0; j < kEventsPerThread; ++j) {
                queue.push(ToplevelClosedEvent{static_cast<ToplevelId>(i * 1000 + j)});
            }
        });
    }
    for (auto& p : producers) {
        p.join();
    }

    assert(queue.size() == kNumThreads * kEventsPerThread);
    auto all = queue.drain();
    assert(all.size() == kNumThreads * kEventsPerThread);
    assert(queue.empty());

    std::cout << "test_event_queue passed!" << std::endl;
    return 0;
}
