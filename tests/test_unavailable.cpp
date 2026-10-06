// Off Linux: browl builds, and says why it cannot connect rather than
// pretending. Windows and macOS only.
#include "browl/browl.h"
#include "check.h"

#include <string>

using namespace browl;

int main() {
    const std::string reason = unavailable_reason();
    std::printf("unavailable: %s\n", reason.c_str());
    CHECK(!reason.empty());

    std::string error;
    CHECK(Display::connect("", &error) == nullptr);
    CHECK_EQ(error, reason);
    error.clear();
    CHECK(Display::connect("wayland-0", &error) == nullptr);
    CHECK_EQ(error, reason);
    error.clear();
    CHECK(Display::connect_to_fd(3, &error) == nullptr);
    CHECK_EQ(error, reason);
    CHECK(Display::connect() == nullptr);  // no error out-parameter: still nullptr

    // Code written against the whole API links and stays inert.
    CHECK(ShmPool::create(nullptr, 4096) == nullptr);
    return bstest::finish("test_unavailable");
}
