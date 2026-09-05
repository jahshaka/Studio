// ui.engine_view_winid — an EngineViewWidget survives Qt handing it a NEW
// native window (deep audit area 7).
//
// THE DEFECT. The engine View is bound to the native window handle that existed
// when createView() ran, and a Vulkan surface cannot be re-pointed at another
// window. Qt is entitled to destroy and recreate that window under the widget —
// reparenting a native widget does it, and the Materials page's "Display" dock,
// which hosts the engine material preview, is a floatable QDockWidget. Tearing
// it off used to leave the View presenting into a window that no longer
// belonged to anybody. Qt announces it with QEvent::WinIdChange; this pins that
// the widget rebuilds on the new one, and that Qt destroying the window on the
// way out does NOT trigger a rebuild (that would swap a working view for an
// offscreen fallback during teardown).
//
// Needs a real X display and the xcb platform plugin — it is about native
// windows. The display comes from JAH_TEST_DISPLAY (tests/CMakeLists.txt).
#include <QtTest>
#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>

#include "viewport/engineviewwidget.h"
#include "jahshaka/engine/Engine.h"

#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>

using namespace jahshaka::engine;

/// QWidget::destroy()/create() are protected, and a derived class may call them
/// on itself. Replacing the native window is the ONE thing this suite needs and
/// there is no public spelling of it.
class RecreatableView : public EngineViewWidget
{
public:
    using EngineViewWidget::EngineViewWidget;
    void destroyNativeWindow() { destroy(); }
    void createNativeWindow()  { create(); }
};

class TestEngineViewWinId : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void rebuildsWhenTheNativeWindowIsReplaced();
    void reparentLeavesALiveView();
    void cleanupTestCase();

private:
    std::shared_ptr<Engine> mEngine;
};

/// Qt's OWN X11 connection — never a second one (the flicker/cross-bleed rule).
/// Same call EngineHost makes (src/bridge/enginehost.cpp).
static unsigned long long qtDisplay()
{
    if (auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>())
        return reinterpret_cast<unsigned long long>(x11->display());
    return 0;
}

void TestEngineViewWinId::initTestCase()
{
    QVERIFY2(QGuiApplication::platformName() == QLatin1String("xcb"),
             "this suite is about native X windows and needs the xcb plugin");
    EngineConfig cfg;
    cfg.backend = Backend::Vulkan;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_engine_view_winid-ogre.log";
    cfg.display = static_cast<NativeDisplayHandle>(qtDisplay());
    QVERIFY2(cfg.display != 0, "no X display from the platform plugin");
    std::string error;
    mEngine = Engine::create(cfg, error);
    QVERIFY2(mEngine != nullptr, error.c_str());
}

void TestEngineViewWinId::cleanupTestCase() { mEngine.reset(); }

/// THE case the handler exists for: the widget's native window is destroyed and
/// a new one created under it. On Windows that is ordinary toolkit behaviour for
/// a reparent; here it is forced with QWidget::destroy()/create(), which is the
/// same sequence and the same QEvent::WinIdChange, so the handler is exercised
/// on the platform we can run.
void TestEngineViewWinId::rebuildsWhenTheNativeWindowIsReplaced()
{
    QWidget host;
    host.resize(400, 300);
    auto *layout = new QVBoxLayout(&host);
    auto *widget = new RecreatableView(&host);
    layout->addWidget(widget);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    QVERIFY(widget->createView(mEngine, QStringLiteral("winid-test")));
    View *first = widget->view();
    QVERIFY(first != nullptr);
    QVERIFY2(!first->isOffscreen(), "the on-screen path did not take — nothing to test");
    QVERIFY(widget->viewCreationError().isEmpty());

    Scene *scene = mEngine->createScene("winid-scene");
    QVERIFY(scene != nullptr);
    QVERIFY(first->setScene(scene));
    for (int i = 0; i < 3; ++i) mEngine->renderOneFrame();
    QVERIFY(first->framesPresented() > 0);

    const WId firstId = widget->internalWinId();
    QVERIFY(firstId != 0);

    widget->destroyNativeWindow();  // WinIdChange with a NULL id: must NOT rebuild
    QVERIFY2(widget->view() == first || widget->view() == nullptr,
             "the teardown notification was treated as a move");
    widget->createNativeWindow();   // WinIdChange with the new id: MUST rebuild
    widget->show();
    QCoreApplication::processEvents();

    QVERIFY2(widget->internalWinId() != firstId, "the native window was not replaced");
    View *second = widget->view();
    QVERIFY2(second != nullptr, "the view was not rebuilt after the window changed");
    // NOT `second != first`: `first` is freed by the rebuild, and glibc happily
    // hands the SAME address back for the new View — the pointer-inequality
    // check compared a stale pointer against a fresh allocation and failed
    // spuriously whenever the allocator reused the block (deterministic under
    // plain glibc, 4/4 green under ASan's quarantine — wave-1 gate, 2026-09-05).
    // The rebuild is proven by the winId change above plus the behavioural
    // assertions below (non-offscreen, scene rebind, framesPresented rising).
    QVERIFY2(!second->isOffscreen(), "the rebuild fell back offscreen");

    // And it renders. A rebuilt view has no scene (the old one took its binding
    // with it), which is exactly why EngineViewWidget::viewRecreated() exists —
    // bind it the way a host does and confirm frames flow again.
    QVERIFY(second->setScene(scene));
    const unsigned long long before = second->framesPresented();
    for (int i = 0; i < 3; ++i) mEngine->renderOneFrame();
    QVERIFY2(second->framesPresented() > before, "the rebuilt view presents nothing");

    second->setScene(nullptr);
    widget->destroyView();
    QVERIFY(widget->view() == nullptr);
    mEngine->destroyScene(scene);
}

/// A reparent — what floating the Materials "Display" dock does. Qt's xcb
/// backend usually reparents the X window IN PLACE (same XID, no WinIdChange),
/// so this is not the handler's case on Linux; what it pins is the invariant
/// either way: after being moved to another top-level the widget still has a
/// live on-screen view and it still presents.
void TestEngineViewWinId::reparentLeavesALiveView()
{
    QWidget first;
    first.resize(400, 300);
    auto *firstLayout = new QVBoxLayout(&first);
    auto *widget = new EngineViewWidget(&first);
    firstLayout->addWidget(widget);
    first.show();
    QVERIFY(QTest::qWaitForWindowExposed(&first));
    QVERIFY(widget->createView(mEngine, QStringLiteral("reparent-test")));
    QVERIFY(widget->view() && !widget->view()->isOffscreen());

    Scene *scene = mEngine->createScene("reparent-scene");
    QVERIFY(scene != nullptr);
    QVERIFY(widget->view()->setScene(scene));
    for (int i = 0; i < 3; ++i) mEngine->renderOneFrame();

    QWidget second;
    second.resize(420, 320);
    auto *secondLayout = new QVBoxLayout(&second);
    widget->setParent(nullptr);
    secondLayout->addWidget(widget);
    second.show();
    QVERIFY(QTest::qWaitForWindowExposed(&second));
    QCoreApplication::processEvents();

    View *view = widget->view();
    QVERIFY2(view != nullptr, "the reparent left the widget with no view at all");
    QVERIFY2(!view->isOffscreen(), "the reparent dropped the widget to an offscreen view");
    if (!view->scene()) QVERIFY(view->setScene(scene));
    const unsigned long long before = view->framesPresented();
    for (int i = 0; i < 3; ++i) mEngine->renderOneFrame();
    QVERIFY2(view->framesPresented() > before, "nothing presents after the reparent");

    view->setScene(nullptr);
    widget->destroyView();
    mEngine->destroyScene(scene);
}

QTEST_MAIN(TestEngineViewWinId)
#include "test_engine_view_winid.moc"
