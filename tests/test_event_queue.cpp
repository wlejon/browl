// EventQueue (MessageQueue<ShellEvent>): ordering, timed waits, the wake
// hook, and concurrent producers. Portable: runs on every platform.
#include "browl/event_queue.h"
#include "check.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace browl;

namespace {

void test_push_drain() {
    EventQueue queue;
    CHECK_EQ(queue.size(), size_t(0));
    CHECK(queue.empty());

    queue.push(LayerClosedEvent{42});
    queue.push(PopupDoneEvent{101});
    CHECK_EQ(queue.size(), size_t(2));
    CHECK(!queue.empty());

    auto drained = queue.drain();
    REQUIRE(drained.size() == 2);
    CHECK(queue.empty());
    // In push order.
    REQUIRE(std::holds_alternative<LayerClosedEvent>(drained[0]));
    CHECK_EQ(std::get<LayerClosedEvent>(drained[0]).surface_id, SurfaceId(42));
    REQUIRE(std::holds_alternative<PopupDoneEvent>(drained[1]));
    CHECK_EQ(std::get<PopupDoneEvent>(drained[1]).surface_id, SurfaceId(101));
    CHECK(queue.drain().empty());

    queue.push(SessionLockedEvent{});
    queue.clear();
    CHECK(queue.empty());
}

void test_wait_for() {
    EventQueue queue;
    // Empty: times out after (about) the full timeout.
    const auto start = std::chrono::steady_clock::now();
    CHECK(!queue.wait_for(std::chrono::milliseconds(50)));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed >= std::chrono::milliseconds(45));

    // Non-empty: returns at once.
    queue.push(SessionLockedEvent{});
    CHECK(queue.wait_for(std::chrono::milliseconds(0)));
    queue.clear();

    // A push from another thread wakes the waiter well before the timeout.
    std::thread t([&queue] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        queue.push(SessionLockedEvent{});
    });
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(queue.wait_for(std::chrono::seconds(5)));
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(4));
    t.join();
    auto items = queue.drain();
    REQUIRE(items.size() == 1);
    CHECK(std::holds_alternative<SessionLockedEvent>(items[0]));
}

void test_wake_hook() {
    EventQueue queue;
    int wakes = 0;
    size_t size_seen = 0;
    queue.set_wake([&] {
        ++wakes;
        size_seen = queue.size();  // called outside the queue's lock
    });
    queue.push(SessionLockFinishedEvent{});
    queue.push(SessionLockFinishedEvent{});
    CHECK_EQ(wakes, 2);
    CHECK_EQ(size_seen, size_t(2));
    queue.set_wake(nullptr);
    queue.push(SessionLockFinishedEvent{});
    CHECK_EQ(wakes, 2);
}

void test_concurrent_producers() {
    EventQueue queue;
    constexpr int kThreads = 4;
    constexpr int kPerThread = 500;
    std::atomic<int> drained_total{0};
    std::atomic<bool> done{false};
    std::vector<ToplevelId> seen;
    // A consumer drains while the producers push: nothing lost or duplicated.
    std::thread consumer([&] {
        while (!done || !queue.empty()) {
            for (auto& ev : queue.drain()) {
                seen.push_back(std::get<ToplevelClosedEvent>(ev).id);
                ++drained_total;
            }
        }
    });
    std::vector<std::thread> producers;
    for (int i = 0; i < kThreads; ++i) {
        producers.emplace_back([&queue, i] {
            for (int j = 0; j < kPerThread; ++j) {
                queue.push(ToplevelClosedEvent{static_cast<ToplevelId>(i * 1000 + j)});
            }
        });
    }
    for (auto& p : producers) p.join();
    done = true;
    consumer.join();

    CHECK_EQ(drained_total.load(), kThreads * kPerThread);
    // Each producer's own events arrive in its push order.
    std::vector<int> last(kThreads, -1);
    int out_of_order = 0;
    for (ToplevelId id : seen) {
        const int producer = static_cast<int>(id / 1000), n = static_cast<int>(id % 1000);
        if (n <= last[producer]) ++out_of_order;
        last[producer] = n;
    }
    CHECK_EQ(out_of_order, 0);
    std::sort(seen.begin(), seen.end());
    CHECK(std::adjacent_find(seen.begin(), seen.end()) == seen.end());
    CHECK(queue.empty());
}

}  // namespace

int main() {
    test_push_drain();
    test_wait_for();
    test_wake_hook();
    test_concurrent_producers();
    return bstest::finish("test_event_queue");
}
