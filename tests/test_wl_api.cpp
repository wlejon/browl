#include "../src/api/api.h"
#include "embed/embed.h"
#include "eval/eval.h"
#include "browl/browl.h"
#include "fake_session.h"
#include "check.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

void checkJs(const std::string& code, const char* file, int line) {
    auto r = bronze::eval::evalScript(code);
    if (r.thrown) {
        std::string errStr;
        if (bronze::embed::isString(r.value)) {
            errStr = bronze::embed::toUtf8(r.value);
        } else if (bronze::embed::isObject(r.value)) {
            auto msg = bronze::embed::getProperty(r.value, "message");
            if (bronze::embed::isString(msg)) {
                errStr = bronze::embed::toUtf8(msg);
            }
        }
        std::cerr << "JS exception in: " << code << "\nError: " << errStr
                  << " (" << file << ":" << line << ")" << std::endl;
        std::exit(1);
    }
    if (!bronze::embed::toBool(r.value)) {
        std::cerr << "checkJs condition returned falsy: " << code
                  << " (" << file << ":" << line << ")" << std::endl;
        std::exit(1);
    }
}

#define CHECK_JS(expr) checkJs(expr, __FILE__, __LINE__)

} // namespace

void run() {
    std::cout << "Starting browl JavaScript API tests..." << std::endl;

    // 1. Install bro.wl
    browl::api::installWl();

    auto g = bronze::embed::globalValue("bro");
    CHECK(g.found);
    CHECK(bronze::embed::isObject(g.value));

    bronze::embed::Persistent wl(bronze::embed::getProperty(g.value, "wl"));
    CHECK(bronze::embed::isObject(wl.get()));
    std::cout << "  Mounted bro.wl successfully." << std::endl;

    // Verify all core methods and properties exist on bro.wl
    CHECK_JS("typeof bro.wl === 'object'");
    CHECK_JS("typeof bro.wl.available === 'boolean'");
    CHECK_JS("bro.wl.available === true");
    CHECK_JS("typeof bro.wl.getToplevels === 'function'");
    CHECK_JS("typeof bro.wl.on === 'function'");
    CHECK_JS("typeof bro.wl.off === 'function'");
    CHECK_JS("typeof bro.wl.addEventListener === 'function'");
    CHECK_JS("typeof bro.wl.removeEventListener === 'function'");
    CHECK_JS("typeof bro.wl.setLayerRole === 'function'");
    CHECK_JS("typeof bro.wl.createLayerSurface === 'function'");
    CHECK_JS("typeof bro.wl.captureOutput === 'function'");
    CHECK_JS("typeof bro.wl.acquireSessionLock === 'function'");
    CHECK_JS("typeof bro.wl.inhibitIdle === 'function'");
    CHECK_JS("typeof bro.wl.createIdleNotification === 'function'");
    CHECK_JS("typeof bro.wl.getOutputs === 'function'");
    CHECK_JS("typeof bro.wl.getSeats === 'function'");
    CHECK_JS("typeof bro.wl.hasCompositor === 'function'");
    CHECK_JS("typeof bro.wl.hasLayerShell === 'function'");
    CHECK_JS("typeof bro.wl.hasForeignToplevelManager === 'function'");
    CHECK_JS("typeof bro.wl.hasSessionLock === 'function'");
    CHECK_JS("typeof bro.wl.hasIdleInhibit === 'function'");
    CHECK_JS("typeof bro.wl.hasScreencopy === 'function'");
    CHECK_JS("typeof bro.wl.hasIdleNotify === 'function'");
    std::cout << "  Verified presence of all methods and properties." << std::endl;

    // 2. Connect with in-process HeadlessCompositor via FakeSession
    bstest::FakeSession s;
    REQUIRE(s.display != nullptr);
    browl::api::setDisplay(std::shared_ptr<browl::Display>(&*s.display, [](browl::Display*){}));
    std::cout << "  Connected FakeSession display to browl_api." << std::endl;

    // Capability queries
    CHECK_JS("bro.wl.hasCompositor() === true");
    CHECK_JS("bro.wl.hasLayerShell() === true");
    CHECK_JS("bro.wl.hasForeignToplevelManager() === true");
    CHECK_JS("bro.wl.hasSessionLock() === true");
    CHECK_JS("bro.wl.hasIdleInhibit() === true");
    CHECK_JS("bro.wl.hasScreencopy() === true");
    CHECK_JS("bro.wl.hasIdleNotify() === true");
    std::cout << "  Capability queries passed." << std::endl;

    // Outputs and Seats queries
    CHECK_JS("Array.isArray(bro.wl.getOutputs()) && bro.wl.getOutputs().length > 0");
    CHECK_JS("typeof bro.wl.getOutputs()[0].id === 'number'");
    CHECK_JS("typeof bro.wl.getOutputs()[0].geometry === 'object'");
    CHECK_JS("Array.isArray(bro.wl.getSeats()) && bro.wl.getSeats().length > 0");
    CHECK_JS("typeof bro.wl.getSeats()[0].hasKeyboard === 'boolean'");
    std::cout << "  Outputs and seats queries passed." << std::endl;

    // 3. Test Foreign Toplevels
    std::cout << "Testing Foreign Toplevels..." << std::endl;
    {
        CHECK_JS(
            "globalThis.__addedTitles = [];\n"
            "globalThis.__closedIds = [];\n"
            "globalThis.__hAdded = bro.wl.on('toplevelAdded', top => {\n"
            "    globalThis.__addedTitles.push(top.title);\n"
            "});\n"
            "globalThis.__hClosed = bro.wl.on('toplevelClosed', ev => {\n"
            "    globalThis.__closedIds.push(ev.id);\n"
            "});\n"
            "true;\n"
        );

        auto* comp_top = s.server.create_foreign_toplevel("Editor", "org.bro.editor", browl::toplevel_state::Activated);
        REQUIRE(comp_top != nullptr);
        CHECK(s.display->roundtrip() >= 0);
        browl::api::tickWlAsync();

        CHECK_JS("bro.wl.getToplevels().length === 1");
        CHECK_JS("bro.wl.getToplevels()[0].title === 'Editor'");
        CHECK_JS("bro.wl.getToplevels()[0].appId === 'org.bro.editor'");
        CHECK_JS("bro.wl.getToplevels()[0].isActivated === true");
        CHECK_JS("bro.wl.getToplevels()[0].isMaximized === false");
        CHECK_JS("bro.wl.getToplevels()[0].isMinimized === false");
        CHECK_JS("bro.wl.getToplevels()[0].isFullscreen === false");
        CHECK_JS("globalThis.__addedTitles.length === 1 && globalThis.__addedTitles[0] === 'Editor'");

        // Perform actions from JS
        CHECK_JS(
            "(function() {\n"
            "    const top = bro.wl.getToplevels()[0];\n"
            "    top.activate();\n"
            "    top.setMaximized();\n"
            "    top.close();\n"
            "    const snap = top.snapshot();\n"
            "    return typeof snap === 'object' && snap.title === 'Editor';\n"
            "})()\n"
        );

        CHECK(s.display->roundtrip() >= 0);
        CHECK(comp_top->activated);
        CHECK(comp_top->maximized);
        CHECK(comp_top->closed);

        // Close toplevel from server
        s.server.close_toplevel(comp_top);
        CHECK(s.display->roundtrip() >= 0);
        browl::api::tickWlAsync();

        CHECK_JS("bro.wl.getToplevels().length === 0");
        CHECK_JS("globalThis.__closedIds.length === 1");

        // Remove listener
        CHECK_JS("globalThis.__hAdded.remove() === true");
        CHECK_JS("bro.wl.off(globalThis.__hClosed) === true");
        std::cout << "  Foreign toplevels passed." << std::endl;
    }

    // 4. Test Layer Surfaces
    std::cout << "Testing Layer Surfaces..." << std::endl;
    {
        CHECK_JS(
            "(function() {\n"
            "    const win = {};\n"
            "    const ok = bro.wl.setLayerRole(win, {\n"
            "        layer: 'top',\n"
            "        anchor: ['top', 'left', 'right'],\n"
            "        exclusive: 48,\n"
            "        margin: { top: 5, left: 10, right: 10, bottom: 0 }\n"
            "    });\n"
            "    return ok === true;\n"
            "})()\n"
        );

        CHECK(s.display->roundtrip() >= 0);
        CHECK(s.server.layer_surface_created());

        s.server.configure_layer_surface(1920, 48);
        CHECK(s.display->roundtrip() >= 0);
        browl::api::tickWlAsync();

        // Direct createLayerSurface test
        CHECK_JS(
            "(function() {\n"
            "    const ls = bro.wl.createLayerSurface({\n"
            "        layer: 'overlay',\n"
            "        anchor: ['bottom', 'left', 'right'],\n"
            "        exclusiveZone: 60,\n"
            "        width: 1920,\n"
            "        height: 60\n"
            "    });\n"
            "    if (!ls) return false;\n"
            "    if (ls.layer !== 'overlay') return false;\n"
            "    if (ls.exclusiveZone !== 60) return false;\n"
            "    ls.setExclusiveZone(72);\n"
            "    ls.commit();\n"
            "    ls.close();\n"
            "    return true;\n"
            "})()\n"
        );
        CHECK(s.display->roundtrip() >= 0);
        std::cout << "  Layer surfaces passed." << std::endl;
    }

    // 5. Test Screencopy Capture
    std::cout << "Testing Screencopy Capture..." << std::endl;
    {
        CHECK_JS(
            "globalThis.__capResult = null;\n"
            "globalThis.__capError = null;\n"
            "bro.wl.captureOutput().then(\n"
            "    r => { globalThis.__capResult = r; },\n"
            "    e => { globalThis.__capError = e; }\n"
            ");\n"
            "true;\n"
        );

        CHECK(s.display->roundtrip() >= 0);
        CHECK(s.server.screencopy_frame_created());

        constexpr uint32_t kFormat = WL_SHM_FORMAT_XRGB8888;
        constexpr uint32_t kWidth = 64, kHeight = 64, kStride = 64 * 4;
        s.server.send_screencopy_buffer(kFormat, kWidth, kHeight, kStride);
        CHECK(s.display->roundtrip() >= 0);
        browl::api::tickWlAsync(); // Allocates buffer and issues copy

        CHECK(s.display->roundtrip() >= 0);
        CHECK(s.server.screencopy_copy_received());

        s.server.send_screencopy_ready(42, 1000);
        CHECK(s.display->roundtrip() >= 0);
        browl::api::tickWlAsync(); // Resolves promise and drains microtasks

        CHECK_JS("globalThis.__capError === null");
        CHECK_JS("globalThis.__capResult !== null");
        CHECK_JS("globalThis.__capResult.width === 64");
        CHECK_JS("globalThis.__capResult.height === 64");
        CHECK_JS("globalThis.__capResult.stride === 256");
        CHECK_JS("globalThis.__capResult.format === 'XRGB8888'");
        CHECK_JS("globalThis.__capResult.pixels instanceof Uint8Array");
        CHECK_JS("globalThis.__capResult.pixels.length === 64 * 256");
        std::cout << "  Screencopy capture passed." << std::endl;
    }

    // 6. Test Session Lock
    std::cout << "Testing Session Lock..." << std::endl;
    {
        CHECK_JS(
            "globalThis.__lock = bro.wl.acquireSessionLock();\n"
            "typeof globalThis.__lock === 'object' && globalThis.__lock.isLocked === false;\n"
        );

        CHECK(s.display->roundtrip() >= 0);
        CHECK(s.server.session_lock_created());

        s.server.send_session_locked();
        CHECK(s.display->roundtrip() >= 0);
        browl::api::tickWlAsync();

        CHECK_JS("globalThis.__lock.isLocked === true");

        CHECK_JS(
            "globalThis.__lockSurf = globalThis.__lock.createSurface();\n"
            "typeof globalThis.__lockSurf === 'object' && globalThis.__lockSurf !== null;\n"
        );

        CHECK(s.display->roundtrip() >= 0);
        CHECK(s.server.lock_surface_created());

        s.server.configure_lock_surface(1920, 1080);
        CHECK(s.display->roundtrip() >= 0);
        browl::api::tickWlAsync();

        CHECK_JS("globalThis.__lockSurf.configuredSize.width === 1920");
        CHECK_JS("globalThis.__lockSurf.configuredSize.height === 1080");

        CHECK_JS(
            "globalThis.__lockSurf.ackConfigure(globalThis.__lockSurf.configuredSerial);\n"
            "globalThis.__lockSurf.commit();\n"
            "globalThis.__lock.unlock() === true;\n"
        );

        CHECK(s.display->roundtrip() >= 0);
        CHECK(s.server.session_unlock_received());
        std::cout << "  Session lock passed." << std::endl;
    }

    // 7. Test Idle Inhibitor & Notification
    std::cout << "Testing Idle Inhibitor & Notification..." << std::endl;
    {
        CHECK_JS(
            "globalThis.__inh = bro.wl.inhibitIdle();\n"
            "typeof globalThis.__inh === 'object' && globalThis.__inh.active === true;\n"
        );

        CHECK(s.display->roundtrip() >= 0);
        CHECK(s.server.idle_inhibitor_created());

        CHECK_JS("globalThis.__inh.release() === true && globalThis.__inh.active === false");
        CHECK(s.display->roundtrip() >= 0);
        CHECK(s.server.idle_inhibitor_destroyed());

        // Notification
        CHECK_JS(
            "globalThis.__notif = bro.wl.createIdleNotification(3000);\n"
            "typeof globalThis.__notif === 'object' && globalThis.__notif.isIdled === false;\n"
        );

        CHECK(s.display->roundtrip() >= 0);
        s.server.fire_idle();
        CHECK(s.display->roundtrip() >= 0);
        browl::api::tickWlAsync();

        CHECK_JS("globalThis.__notif.isIdled === true");

        s.server.fire_resume();
        CHECK(s.display->roundtrip() >= 0);
        browl::api::tickWlAsync();

        CHECK_JS("globalThis.__notif.isIdled === false");
        CHECK_JS("globalThis.__notif.destroy() === true");
        std::cout << "  Idle inhibitor and notification passed." << std::endl;
    }

    // 8. Event Listener Management (on, off, addEventListener, removeEventListener)
    std::cout << "Testing Event Listener Management..." << std::endl;
    {
        CHECK_JS(
            "(function() {\n"
            "    let count = 0;\n"
            "    const fn = () => { count++; };\n"
            "    const h = bro.wl.on('testEvent', fn);\n"
            "    bro.wl.addEventListener('testEvent2', fn);\n"
            "    if (!bro.wl.off(h)) return false;\n"
            "    if (!bro.wl.removeEventListener('testEvent2', fn)) return false;\n"
            "    return true;\n"
            "})()\n"
        );
        std::cout << "  Event listener management passed." << std::endl;
    }

    // 9. GC Stress Safety Loop
    std::cout << "Testing GC Stress Safety..." << std::endl;
    {
        for (int i = 0; i < 150; ++i) {
            auto r = bronze::eval::evalScript(
                "(function() {\n"
                "    const outs = bro.wl.getOutputs();\n"
                "    const seats = bro.wl.getSeats();\n"
                "    const tops = bro.wl.getToplevels();\n"
                "    return outs.length + seats.length + tops.length;\n"
                "})()\n"
            );
            CHECK(!r.thrown);
            browl::api::tickWlAsync();
        }
        std::cout << "  GC stress loop passed." << std::endl;
    }

    // 10. Shutdown
    std::cout << "Testing shutdownWlAsync()..." << std::endl;
    browl::api::shutdownWlAsync();
    browl::api::setDisplay(nullptr);
    std::cout << "  Shutdown passed." << std::endl;

    std::cout << "All browl JavaScript API tests PASSED!" << std::endl;
}

int main() {
    run();
    return bstest::finish("browl_test_api");
}
