#include "browl/display.h"
#include "browl/output.h"

#include <cstdlib>
#include <iostream>

using namespace browl;

int main() {
    const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
    if (!wayland_display || wayland_display[0] == '\0') {
        std::cout << "No WAYLAND_DISPLAY in environment; skipping live compositor test (code 77)."
                  << std::endl;
        return 77;
    }

    auto display = Display::connect(wayland_display);
    if (!display) {
        std::cout << "Could not connect to WAYLAND_DISPLAY " << wayland_display
                  << "; skipping live compositor test (code 77)." << std::endl;
        return 77;
    }

    std::cout << "Connected to live Wayland compositor: " << wayland_display << std::endl;
    std::cout << "Compositor: " << (display->has_compositor() ? "yes" : "no") << std::endl;
    std::cout << "Shm: " << (display->has_shm() ? "yes" : "no") << std::endl;
    std::cout << "Layer Shell: " << (display->has_layer_shell() ? "yes" : "no") << std::endl;
    std::cout << "XDG Shell: " << (display->has_xdg_shell() ? "yes" : "no") << std::endl;
    std::cout << "Foreign Toplevel: " << (display->has_foreign_toplevel_manager() ? "yes" : "no")
              << std::endl;

    display->roundtrip();
    auto outputs = display->outputs();
    std::cout << "Outputs detected: " << outputs.size() << std::endl;

    return 0;
}
