// The ON-SCREEN view: the swapchain path, which had no test at all.
//
// Every other engine suite renders offscreen into an RTT, so nothing covered
// what the editor actually runs on: a Vulkan swapchain bound to a native X11
// window that the host resizes underneath it. That gap is deep audit area 7 F3,
// and it is why F1 (the resize recreate being unreachable dead code) survived —
// the only assertion anywhere read back the numbers the host had just pushed.
//
// WHAT THIS PINS
//   1. View::width()/height() report the RENDER TARGET, not the request. A view
//      created at one size on a window of another must report the window's.
//   2. A host resize reaches the swapchain: resize the X window, tell the view,
//      render, and the target has followed. This is the F1 gate.
//   3. It keeps PRESENTING across resizes — framesPresented must still climb
//      after several of them in a row. The recorded defect this lane exists for
//      is a viewport that goes quiet after a dock-open resize.
//   4. A runtime MSAA change on an on-screen view (the one path that still
//      destroys and recreates the window) works, keeps the size, and — under the
//      Vulkan validation layer, which is how the app-level suite runs it —
//      does not destroy in-flight GPU objects.
//
// THE WINDOW IS NEVER MAPPED. An X11 window has geometry, and a Vulkan surface
// over it has a currentExtent, whether or not it is on screen; nothing here
// needs pixels to reach a display. That keeps this suite safe to run on a shared
// machine: it cannot pop a window onto anybody's desktop.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <X11/Xlib.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
static int checks = 0;

#define CHECK_MSG(cond, ...)                                     \
    do {                                                         \
        ++checks;                                                \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
    } while (0)

namespace {

struct HostWindow {
    Display *display = nullptr;
    ::Window window = 0;

    bool open(unsigned w, unsigned h) {
        display = XOpenDisplay(nullptr);
        if (!display) return false;
        const int screen = DefaultScreen(display);
        window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, w, h, 0,
                                     BlackPixel(display, screen), BlackPixel(display, screen));
        // Deliberately no XMapWindow — see the file header.
        XSync(display, False);
        return window != 0;
    }

    void resize(unsigned w, unsigned h) {
        XResizeWindow(display, window, w, h);
        // The engine reads the window's geometry from the X server on the same
        // connection; the flush is what guarantees the request is there first.
        XSync(display, False);
    }

    void geometry(unsigned &w, unsigned &h) const {
        ::Window root; int x, y; unsigned bw, depth;
        XGetGeometry(display, window, &root, &x, &y, &w, &h, &bw, &depth);
    }

    void close() {
        if (!display) return;
        if (window) XDestroyWindow(display, window);
        XSync(display, False);
        XCloseDisplay(display);
        display = nullptr; window = 0;
    }
};

}  // namespace

int main() {
    HostWindow host;
    if (!host.open(320, 240)) {
        // Same contract as every other engine suite: a display must be reachable.
        std::printf("FAIL: could not open an X display / window\n");
        return 1;
    }

    EngineConfig cfg;
    cfg.backend = Backend::Vulkan;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_engine_window-ogre.log";
    cfg.display = static_cast<NativeDisplayHandle>(reinterpret_cast<unsigned long long>(host.display));

    std::string error;
    auto engine = Engine::create(cfg, error);
    if (!engine) {
        std::printf("FAIL: Engine::create — %s\n", error.c_str());
        host.close();
        return 1;
    }

    View *v = engine->createView("onscreen", static_cast<NativeWindowHandle>(host.window),
                                 320, 240, Colour(0.0f, 0.0f, 1.0f));
    if (!v) {
        std::printf("FAIL: createView — %s\n", engine->lastError().c_str());
        engine.reset();
        host.close();
        return 1;
    }
    CHECK_MSG(!v->isOffscreen(), "the view is on-screen");

    Scene *s = engine->createScene("scene");
    if (!s) { std::printf("FAIL: createScene\n"); engine.reset(); host.close(); return 1; }
    v->setScene(s);
    s->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    enginetest::addDirectionalLight(s, Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
    enginetest::addTestCube(s, Colour(0.9f, 0.3f, 0.1f), 0.2f, 0.6f);
    enginetest::testCameraLookAt(v, Vec3(2.2f, 1.8f, 2.6f), Vec3(0, 0, 0));

    auto render = [&](int n) { for (int i = 0; i < n; ++i) engine->renderOneFrame(); };
    render(3);

    CHECK_MSG(v->framesPresented() >= 3, "presenting into the window (%llu frames)",
              v->framesPresented());
    {
        unsigned gw = 0, gh = 0; host.geometry(gw, gh);
        CHECK_MSG(v->width() == gw && v->height() == gh,
                  "target %ux%u matches the window %ux%u", v->width(), v->height(), gw, gh);
    }

    // ---- 1: width()/height() are the TARGET, not the request -----------------
    // Asking for a size the window does not have must NOT change the answer:
    // nothing has been applied yet, and the swapchain is still the old one.
    v->resize(512, 384);
    CHECK_MSG(v->width() == 320 && v->height() == 240,
              "a pending resize does not change the reported size (%ux%u)",
              v->width(), v->height());

    // ---- 2: a host resize reaches the swapchain (the F1 gate) ----------------
    host.resize(512, 384);
    render(2);
    CHECK_MSG(v->width() == 512 && v->height() == 384,
              "after resize the target is 512x384, got %ux%u", v->width(), v->height());

    // Shrink, too: the growth direction alone would pass on a swapchain that
    // merely never shrinks.
    host.resize(256, 192);
    v->resize(256, 192);
    render(2);
    CHECK_MSG(v->width() == 256 && v->height() == 192,
              "after shrink the target is 256x192, got %ux%u", v->width(), v->height());

    // ---- 3: it keeps presenting across a burst of resizes --------------------
    // The recorded defect is a viewport that stops presenting after a resize,
    // so the assertion is on the frame counter moving, not on any one size.
    const unsigned long long before = v->framesPresented();
    const unsigned sizes[][2] = { {300, 220}, {480, 360}, {333, 251}, {512, 384} };
    for (const auto &sz : sizes) {
        host.resize(sz[0], sz[1]);
        v->resize(sz[0], sz[1]);
        render(2);
    }
    CHECK_MSG(v->framesPresented() >= before + 8,
              "still presenting after 4 resizes (%llu -> %llu)", before, v->framesPresented());
    CHECK_MSG(v->width() == 512 && v->height() == 384,
              "the last size of the burst won: %ux%u", v->width(), v->height());

    // ---- 3b: resized while DISABLED — the dock-open shape of the defect ------
    // A hidden viewport is a disabled view: the compositor skips its workspace,
    // so it presents nothing and Ogre's OUT_OF_DATE self-heal (which only runs
    // on an acquire, i.e. inside a frame that actually presented) cannot fire.
    // A layout change while a dock opens does exactly this — resize, then show.
    // The size must be right by the time it is showing again.
    v->setEnabled(false);
    host.resize(288, 208);
    v->resize(288, 208);
    render(2);
    v->setEnabled(true);
    render(2);
    CHECK_MSG(v->width() == 288 && v->height() == 208,
              "a resize applied while disabled took: %ux%u", v->width(), v->height());
    const unsigned long long afterHidden = v->framesPresented();
    render(3);
    CHECK_MSG(v->framesPresented() >= afterHidden + 3,
              "presenting again after being resized while hidden (%llu -> %llu)",
              afterHidden, v->framesPresented());
    host.resize(512, 384);
    v->resize(512, 384);
    render(2);

    // ---- 4: runtime MSAA change on an on-screen view -------------------------
    // The only remaining path that destroys and recreates the render window.
    // Under the validation layer this is where destroying a swapchain (and its
    // acquire semaphore) with frames in flight would be reported.
    CHECK_MSG(v->sampleCount() == 1, "on-screen views start at 1x (got %u)", v->sampleCount());
    v->setSampleCount(4);
    render(3);
    const unsigned achieved = v->sampleCount();
    std::printf("      achieved sample count after a 4x request: %u\n", achieved);
    CHECK_MSG(achieved >= 1, "the view survived the MSAA change (achieved %ux)", achieved);
    CHECK_MSG(v->width() == 512 && v->height() == 384,
              "the MSAA recreate kept the size: %ux%u", v->width(), v->height());
    const unsigned long long afterMsaa = v->framesPresented();
    render(3);
    CHECK_MSG(v->framesPresented() >= afterMsaa + 3,
              "still presenting after the MSAA recreate (%llu -> %llu)",
              afterMsaa, v->framesPresented());

    // Back to 1x, and one more resize on top of it: the two pending paths
    // coalesce in applyPendingResize and must not deadlock or lose the window.
    v->setSampleCount(1);
    host.resize(400, 300);
    v->resize(400, 300);
    render(3);
    CHECK_MSG(v->sampleCount() == 1, "back to 1x (got %u)", v->sampleCount());
    CHECK_MSG(v->width() == 400 && v->height() == 300,
              "coalesced MSAA+resize landed at 400x300, got %ux%u", v->width(), v->height());

    // ---- teardown, in the mandated order ------------------------------------
    engine->destroyView(v);
    engine->destroyScene(s);
    engine.reset();
    host.close();

    std::printf("%s: %d checks, %d failures\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
