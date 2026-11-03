// app.render_route — THE ONE RENDER-SYSTEM DECISION AND THE HEADLESS HOST's VIEW, on the CPU (TEST-STREAMLINE-1 stage
// 3a; the reader's test plan P4 + P5). No display, no card: the NULL render system only.
//
//  1. THE TABLE (bridge/renderroute.h, a pure function): offscreen + --render-offscreen + a display -> Vulkan
//     offscreen; offscreen alone -> NULL; a windowed platform -> Vulkan on screen; --render-offscreen without the
//     offscreen platform, or with no DISPLAY, -> refused with its reason (never Ogre's "Malformed resolution string").
//  2. ONE DECISION PER PROCESS (the latch): a second decide() is refused, whatever it asks, naming the first route.
//  3. THE CLI: --render-offscreen parses, needs --headless and a script/pool/MCP run, never --dump-api-docs; the
//     runners' document-only flag is --headless WITHOUT it.
//  4. THE DECLARED OFFSCREEN VIEW (EngineViewWidget::setOffscreen) on a NULL engine: the flag reads back; the widget
//     asks for NO native window (WA_NativeWindow cleared, no window id made: no native attempt); createView() refuses
//     with the NULL system's own line; the factory's refusal (viewRefusal) names the NULL render system in one line.
//     The product's native-failure fallback (no declaration) keeps asking for its native window, untouched.
//
// LAW: one render system per process, decided once (enginehost.cpp; Types.h EngineConfig::headless), and the host
// never degrades silently - every refusal is one named line.
#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstring>
#include <QWidget>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "app/cli/clioptions.h"
#include "bridge/renderroute.h"
#include "viewport/engineviewwidget.h"
#include "jahshaka/engine/Engine.h"
#include "enginetesthelpers.h"

static int gFailures = 0;
static void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++gFailures;
}

static CliOptions parse(std::vector<const char *> args)
{
    args.insert(args.begin(), "Jahshaka");
    return CliOptions::parse(int(args.size()), const_cast<char **>(args.data()));
}

int main(int argc, char **argv)
{
    // 1. THE TABLE
    {
        const auto vk = chooseRenderRoute({ QStringLiteral("offscreen"), true, true });
        check(vk.route == RenderRoute::VulkanOffscreen && vk.why.isEmpty(),
              "offscreen + --render-offscreen + DISPLAY -> the Vulkan offscreen host");
        const auto null = chooseRenderRoute({ QStringLiteral("offscreen"), false, true });
        check(null.route == RenderRoute::Null && null.why.isEmpty(), "offscreen alone -> NULL (DISPLAY or not)");
        check(chooseRenderRoute({ QStringLiteral("offscreen"), false, false }).route == RenderRoute::Null,
              "offscreen alone with DISPLAY unset -> NULL");
        const auto win = chooseRenderRoute({ QStringLiteral("xcb"), false, true });
        check(win.route == RenderRoute::VulkanWindowed && win.why.isEmpty(), "the windowed platform -> Vulkan on screen");
        const auto noDisplay = chooseRenderRoute({ QStringLiteral("offscreen"), true, false });
        check(noDisplay.route == RenderRoute::Refused && noDisplay.why.contains(QLatin1String("DISPLAY is unset")) &&
                  noDisplay.why.contains(QLatin1String("VulkanXcbSupport")),
              "--render-offscreen with DISPLAY unset -> refused, naming why (Ogre's Vulkan connects at plugin load)");
        const auto onScreen = chooseRenderRoute({ QStringLiteral("xcb"), true, true });
        check(onScreen.route == RenderRoute::Refused && onScreen.why.contains(QLatin1String("--render-offscreen")),
              "--render-offscreen on the windowed platform -> refused, named");
        check(QByteArray(renderRouteName(RenderRoute::VulkanOffscreen)) == "vulkan-offscreen" &&
                  QByteArray(renderRouteName(RenderRoute::Null)) == "null" &&
                  QByteArray(renderRouteName(RenderRoute::VulkanWindowed)) == "vulkan-windowed",
              "the routes' names (the DEVICE block's `render route` row)");
    }

    // 2. ONE DECISION PER PROCESS
    {
        RenderRouteLatch latch;
        check(!latch.decided() && latch.route() == RenderRoute::Refused, "a fresh latch has decided nothing");
        const auto first = latch.decide({ QStringLiteral("offscreen"), false, false });
        check(first.route == RenderRoute::Null && latch.decided() && latch.route() == RenderRoute::Null,
              "the first decision is the table's");
        const auto second = latch.decide({ QStringLiteral("offscreen"), true, true });
        check(second.route == RenderRoute::Refused && second.why.contains(QLatin1String("chosen once")) &&
                  second.why.contains(QLatin1String("'null'")) && latch.route() == RenderRoute::Null,
              "asked twice in a process -> refused, naming the route taken; the first stands");
        const auto third = latch.decide({ QStringLiteral("offscreen"), false, false });
        check(third.route == RenderRoute::Refused, "...even when asked the same question again");
    }

    // 3. THE CLI
    {
        const CliOptions host = parse({ "--script", "x.js", "--headless", "--render-offscreen" });
        check(host.errors.isEmpty() && host.renderOffscreen && host.headlessScript && !host.documentOnly(),
              "--headless --render-offscreen --script: the host, NOT a document-only run");
        const CliOptions pool = parse({ "--scripts", "a.js,b.js", "--headless", "--render-offscreen" });
        check(pool.errors.isEmpty() && !pool.documentOnly(), "...and a pool run takes it too");
        const CliOptions mcp = parse({ "--mcp-port=0", "--headless", "--render-offscreen" });
        check(mcp.errors.isEmpty() && !mcp.documentOnly(), "...and an MCP run");
        const CliOptions doc = parse({ "--script", "x.js", "--headless" });
        check(doc.errors.isEmpty() && doc.documentOnly(), "--headless alone stays the document-only (NULL) run");
        check(!parse({ "--script", "x.js", "--render-offscreen" }).errors.isEmpty(),
              "--render-offscreen without --headless is refused");
        check(!parse({ "--headless", "--render-offscreen" }).errors.isEmpty(),
              "--render-offscreen with no script, pool or MCP run is refused");
        check(!parse({ "--dump-api-docs", "x.md", "--headless", "--render-offscreen" }).errors.isEmpty(),
              "--render-offscreen with --dump-api-docs is refused (the dump stays NULL, proven)");
    }

    // 3b. THE HOST's SCREEN IS THE RIG's (TEST-STREAMLINE-1 red (a)): the policy hands the offscreen platform a file
    // naming ONE screen equal to the rig's xcb screen (1920x1080, DPR 1, logical DPI 96 - measured on the rig's Xvfb -,
    // no frame margins), only for the host; the document-only run keeps the plain platform. Before QApplication, as main.
    {
        QString err;
        qunsetenv("QT_QPA_PLATFORM");
        const bool applied = parse({ "--script", "x.js", "--headless", "--render-offscreen" }).applyPlatformPolicy(err);
        const QByteArray plat = qgetenv("QT_QPA_PLATFORM");
        const QByteArray prefix = "offscreen:configfile=";
        check(applied && err.isEmpty() && plat.startsWith(prefix), "the host names a screen file to the offscreen platform");
        const QString path = QString::fromLocal8Bit(plat.mid(int(std::strlen(prefix.constData()))));
        check(!path.isEmpty() && !path.contains(QLatin1Char(':')),
              "...a real path with no ':' (the plugin splits its arguments on ':', so a resource path cannot pass)");
        QFile f(path);
        const bool opened = f.open(QIODevice::ReadOnly);
        const QJsonObject o = QJsonDocument::fromJson(opened ? f.readAll() : QByteArray()).object();
        f.close();
        const QJsonArray screens = o.value(QStringLiteral("screens")).toArray();
        const QJsonObject s = screens.isEmpty() ? QJsonObject() : screens.first().toObject();
        check(opened && screens.size() == 1 && s.value(QStringLiteral("width")).toInt() == 1920 &&
                  s.value(QStringLiteral("height")).toInt() == 1080 && s.value(QStringLiteral("dpr")).toDouble() == 1.0 &&
                  s.value(QStringLiteral("logicalDpi")).toDouble() == 96.0 &&
                  o.value(QStringLiteral("windowFrameMargins")).toBool(true) == false,
              "...naming ONE screen equal to the rig's: 1920x1080, DPR 1, logical DPI 96, no frame margins");
        CliOptions::releasePlatformFiles();
        check(!QFile::exists(path), "...and released once the app has read it");
        qunsetenv("QT_QPA_PLATFORM");
        const bool docApplied = parse({ "--script", "x.js", "--headless" }).applyPlatformPolicy(err);
        check(docApplied && qgetenv("QT_QPA_PLATFORM") == "offscreen", "the document-only run keeps the plain offscreen platform");
        qunsetenv("QT_QPA_PLATFORM");
    }

    // 4. THE DECLARED OFFSCREEN VIEW ON A NULL ENGINE
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    std::string err;
    std::shared_ptr<jahshaka::engine::Engine> engine = jahshaka::engine::Engine::create(
        enginetest::testEngineConfig("test-render-route-ogre.log", enginetest::EngineOverrides().headless()), err);
    check(engine && engine->isHeadless(), "a NULL engine boots with no display");
    if (engine) {
        const QString refusal = EngineViewWidget::viewRefusal(engine.get());
        check(refusal.contains(QLatin1String("NULL render system")) && !refusal.contains(QLatin1Char('\n')),
              "the factory's refusal under NULL: ONE line naming the NULL render system");
        check(!EngineViewWidget::viewRefusal(nullptr).isEmpty(), "no engine is refused too");

        QWidget window;
        window.resize(320, 240);
        auto *declared = new EngineViewWidget(&window);
        declared->resize(320, 240);
        check(!declared->offscreenByDeclaration(), "a widget is not offscreen until it is declared");
        check(declared->testAttribute(Qt::WA_NativeWindow), "...and asks for a native window until then");
        check(declared->setOffscreen(jahshaka::engine::OffscreenContract::StillPicture) &&
                  declared->offscreenByDeclaration(),
              "setOffscreen(StillPicture): the flag reads back");
        check(!declared->testAttribute(Qt::WA_NativeWindow), "...and the widget asks for NO native window");
        window.show();
        app.processEvents();
        const bool made = declared->createView(engine, QStringLiteral("declared"));
        check(!made && !declared->view(), "createView on NULL refuses: no View of any kind");
        check(declared->internalWinId() == 0, "the declared path made NO native attempt: no native window id exists");
        check(!declared->viewCreationError().isEmpty(), "...and its refusal is said (the engine's own line): " );
        std::printf("   the engine's line: %s\n", qPrintable(declared->viewCreationError()));

        // THE PRODUCT'S FALLBACK, UNTOUCHED: an undeclared widget is a native one (it tries the native view first).
        auto *native = new EngineViewWidget(&window);
        check(native->testAttribute(Qt::WA_NativeWindow) && !native->offscreenByDeclaration(),
              "an undeclared widget keeps its native window (the on-screen path and its fallback, unchanged)");
        // THE NEGATIVE CONTROL of "no native attempt": the undeclared path asks for its window id first (createView
        // passes winId() to the engine) - so a window id exists after it, where the declared path's stays 0.
        native->createView(engine, QStringLiteral("native"));
        check(native->internalWinId() != 0 && !native->view(),
              "...and its createView DOES make the native attempt (a window id exists after it; NULL refuses both views)");
    }

    std::printf("app.render_route: %s\n", gFailures ? "FAILED" : "all ok");
    return gFailures ? 1 : 0;
}
