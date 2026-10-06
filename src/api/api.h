#pragma once

#include <memory>
#include <string>

namespace browl {
class Display;
}

namespace browl::api {

/// Mounts `bro.wl` in the current Bronze realm.
void installWl();

/// Pumps Wayland display events, drains `EventQueue`, dispatches JS reactions.
void tickWlAsync();

/// Cleans up pending captures, locks, and listeners.
void shutdownWlAsync();

/// Sets the Display used by the API (if nullptr, uses Display::connect()).
void setDisplay(std::shared_ptr<browl::Display> display);

/// Gets the Display currently used by the API.
std::shared_ptr<browl::Display> getDisplay();

/// Returns whether Wayland client support is available on this platform.
bool available(std::string* reason = nullptr);

} // namespace browl::api

using browl::api::installWl;
using browl::api::tickWlAsync;
using browl::api::shutdownWlAsync;
using browl::api::setDisplay;
using browl::api::getDisplay;
using browl::api::available;
