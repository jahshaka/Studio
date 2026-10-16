// view.window_on_render_thread — THE WINDOW SPIKE (ENGINE-UPGRADE-U3 stage 4's premise, measured first).
//
// The render thread owns Root, the device and every window surface; the widgets stay Qt's. This drives the five
// steps the stage will put on two threads, with EVERY engine call on a second std::thread (a one-slot executor the
// UI thread posts to and waits on — the two joins' shape), the widgets on the UI thread:
//   1. Engine::create on Qt's own X Display + createView on the widget's winId, 60 frames;
//   2. three resizes (the widget, then View::resize on the render thread), a frame each;
//   3. a dock float + re-dock: the native window is REPLACED (setParent: QEvent::WinIdChange) — destroyView and
//      createView on the new winId, on the render thread, while the UI thread waits (the join);
//   4. an MSAA change (View::setSampleCount, the swapchain's recreate) and frames;
//   5. quit: destroyView, ~Engine ON the render thread, joined BEFORE Qt destroys the widget's X window.
// The bar: 0 X errors (XSetErrorHandler counts them; the engine's two Xlib calls run on the render thread over Qt's
// Display — XInitThreads is Qt's, called in QXcbIntegration's ctor) and 0 validation errors (the row runs under
// VK_LAYER_KHRONOS_validation, asserted live; the row FAILS on any "Validation Error" the layer prints).
#include <QApplication>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGui/qguiapplication_platform.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } std::fflush(stdout); } while (0)

// tests/ui/xerrorcounter.cpp (Xlib stays out of this TU).
void installXErrorCounter();
int xErrorCount();
struct XErrors { int load() const { return xErrorCount(); } operator int() const { return xErrorCount(); } };
static const XErrors gXErrors;

/// The render thread: one task at a time, the poster waits for it (a join).
class RenderThread {
public:
    RenderThread() : mThread([this] { loop(); }) {}
    ~RenderThread() { run(nullptr, true); mThread.join(); }
    void run(std::function<void()> task, bool quit = false)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mTask = std::move(task);
        mQuit = quit;
        mPending = true;
        mCv.notify_all();
        mCv.wait(lock, [this] { return !mPending; });
    }
    std::thread::id id() const { return mThread.get_id(); }
private:
    void loop()
    {
        for (;;) {
            std::unique_lock<std::mutex> lock(mMutex);
            mCv.wait(lock, [this] { return mPending; });
            if (mTask) { auto t = std::move(mTask); lock.unlock(); t(); lock.lock(); }
            const bool quit = mQuit;
            mPending = false;
            mCv.notify_all();
            if (quit) return;
        }
    }
    std::mutex mMutex;
    std::condition_variable mCv;
    std::function<void()> mTask;
    bool mPending = false, mQuit = false;
    std::thread mThread;
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "xcb");
    QApplication app(argc, argv);
    std::printf("== view.window_on_render_thread: the window on a render thread (create, resize, re-dock, MSAA, quit)\n");
    auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>();
    void *dpy = x11 ? static_cast<void *>(x11->display()) : nullptr;
    CHECK(dpy != nullptr, "Qt's own X Display");
    if (!dpy) return 1;
    installXErrorCounter();

    QWidget dock;                                     // the dock the view lives in
    dock.resize(640, 400);
    auto *layout = new QVBoxLayout(&dock);
    /// QWidget::destroy()/create() are protected: the one way to REPLACE the native window outright (a dock float may
    /// reparent the same X window — measured: it kept its winId — or make a new one; both are driven here).
    struct Surface : QWidget {
        void replaceNativeWindow() { destroy(); create(); }
    };
    auto *surface = new Surface;                      // the engine surface: a native child, Qt paints nothing
    surface->setAttribute(Qt::WA_NativeWindow);
    surface->setAttribute(Qt::WA_PaintOnScreen);
    surface->setAttribute(Qt::WA_NoSystemBackground);
    layout->addWidget(surface);
    dock.show();
    for (int i = 0; i < 20; ++i) app.processEvents();

    const std::string logFile = "test_window_render_thread-ogre.log";
    std::shared_ptr<Engine> engine;
    View *view = nullptr;
    Scene *scene = nullptr;
    std::string err;
    RenderThread rt;
    const auto frames = [&](int n) { rt.run([&] { for (int i = 0; i < n; ++i) engine->renderOneFrame(); }); };

    // ---- 1. create on the render thread -------------------------------------------------------------------------
    const unsigned long long winId = surface->winId();
    rt.run([&] {
        const auto cfg = enginetest::testEngineConfig(logFile,
            enginetest::EngineOverrides().display(static_cast<NativeDisplayHandle>(reinterpret_cast<unsigned long long>(dpy))));
        engine = Engine::create(cfg, err);
        if (!engine) return;
        engine->submitFrame(FrameInput::delta(1.0f / 60.0f));
        view = engine->createView("spike", static_cast<NativeWindowHandle>(winId), unsigned(surface->width()),
                                  unsigned(surface->height()), Colour(0.1f, 0.1f, 0.2f));
        scene = engine->createScene("spike");
        if (view && scene) {
            view->setScene(scene);
            enginetest::addDirectionalLight(scene, Vec3(-0.3f, -1.0f, 0.4f), 2.0f);
            enginetest::addTestCube(scene, Colour(0.8f, 0.3f, 0.2f), 0.0f, 0.5f);
            enginetest::testCameraLookAt(view, Vec3(2.0f, 2.0f, 4.0f), Vec3(0, 0, 0));
        }
    });
    CHECK(engine && view && scene, "1. Engine::create + createView on the render thread (%s)", err.c_str());
    if (!engine || !view) return 1;
    frames(60);
    CHECK(gXErrors == 0, "...60 frames on the render thread, %d X errors", gXErrors.load());

    // ---- 2. three resizes ---------------------------------------------------------------------------------------
    const int sizes[3][2] = { { 800, 500 }, { 500, 320 }, { 1024, 640 } };
    for (const auto &sz : sizes) {
        dock.resize(sz[0], sz[1]);
        for (int i = 0; i < 10; ++i) app.processEvents();
        const unsigned w = unsigned(surface->width()), h = unsigned(surface->height());
        rt.run([&] { view->resize(w, h); engine->renderOneFrame(); });
    }
    frames(5);
    CHECK(gXErrors == 0, "2. three resizes (the widget, then View::resize on the render thread), %d X errors",
          gXErrors.load());

    // ---- 3. float + re-dock: the native window is replaced --------------------------------------------------------
    for (int step = 0; step < 2; ++step) {
        // THE JOIN: the old surface goes on the render thread before Qt destroys its window.
        rt.run([&] { engine->destroyView(view); view = nullptr; });
        if (step == 0) { surface->setParent(nullptr); surface->resize(480, 300); surface->show(); }
        else { layout->addWidget(surface); surface->show(); }
        for (int i = 0; i < 20; ++i) app.processEvents();
        const unsigned long long id = surface->winId();
        const unsigned w = unsigned(surface->width()), h = unsigned(surface->height());
        rt.run([&] {
            view = engine->createView(step == 0 ? "spike-float" : "spike-dock", static_cast<NativeWindowHandle>(id), w, h,
                                      Colour(0.1f, 0.2f, 0.1f));
            if (view) {
                view->setScene(scene);
                enginetest::testCameraLookAt(view, Vec3(2.0f, 2.0f, 4.0f), Vec3(0, 0, 0));
            }
        });
        CHECK(view != nullptr, "3%s. the view rebuilt on the %s window (winId %llu)", step == 0 ? "a" : "b",
              step == 0 ? "floating" : "re-docked", id);
        if (!view) return 1;
        frames(20);
    }
    CHECK(gXErrors == 0, "...float + re-dock, %d X errors", gXErrors.load());
    // 3c. THE NATIVE WINDOW REPLACED OUTRIGHT (destroy + create: a new X window id under the widget).
    {
        const unsigned long long oldId = surface->winId();
        rt.run([&] { engine->destroyView(view); view = nullptr; });   // the join
        surface->replaceNativeWindow();
        surface->show();
        for (int i = 0; i < 20; ++i) app.processEvents();
        const unsigned long long id = surface->winId();
        const unsigned w = unsigned(surface->width()), h = unsigned(surface->height());
        rt.run([&] {
            view = engine->createView("spike-new", static_cast<NativeWindowHandle>(id), w, h, Colour(0.2f, 0.1f, 0.1f));
            if (view) {
                view->setScene(scene);
                enginetest::testCameraLookAt(view, Vec3(2.0f, 2.0f, 4.0f), Vec3(0, 0, 0));
            }
        });
        CHECK(view != nullptr && id != oldId, "3c. the view rebuilt on a NEW native window (%llu -> %llu)", oldId, id);
        if (!view) return 1;
        frames(20);
        CHECK(gXErrors == 0, "...the replaced window, %d X errors", gXErrors.load());
    }

    // ---- 4. MSAA change -------------------------------------------------------------------------------------------
    rt.run([&] { view->setSampleCount(4); engine->renderOneFrame(); });
    frames(20);
    rt.run([&] { view->setSampleCount(1); engine->renderOneFrame(); });
    frames(10);
    CHECK(gXErrors == 0, "4. MSAA 1 -> 4 -> 1 on the render thread, %d X errors", gXErrors.load());

    // THE LAYER WAS LIVE (a run without it proves nothing): its errors go to this process's output, which the row
    // fails on (FAIL_REGULAR_EXPRESSION "Validation Error").
    ValidationStatus vs;
    rt.run([&] { vs = engine->validation(); });
    CHECK(vs.requested && vs.active, "the validation layer is requested (%d) and live on the device (%d)", int(vs.requested),
          int(vs.active));

    // ---- 5. quit: the engine dies on its thread, before Qt's windows ------------------------------------------------
    rt.run([&] {
        engine->destroyView(view);
        view = nullptr;
        engine.reset();   // ~OgreEngine on the render thread
    });
    for (int i = 0; i < 5; ++i) app.processEvents();
    CHECK(gXErrors == 0, "5. destroyView + ~Engine on the render thread, joined before the widgets go, %d X errors",
          gXErrors.load());

    std::printf(gFailures ? "view.window_on_render_thread: FAILED\n" : "view.window_on_render_thread: PASS\n");
    return gFailures ? 1 : 0;
}
