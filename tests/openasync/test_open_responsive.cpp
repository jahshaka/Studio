// Opening a world must not freeze the window (owner, 2026-09-03: GNOME
// offered to force-quit Jahshaka while a scene opened).
//
// "Responsive" is measured, not asserted by eye: the app runs a heartbeat
// probe on its UI thread (app.heartbeat / app.heartbeatStats,
// src/services/mainthreadheartbeat.h) and reports the WORST gap between
// ticks. A UI thread inside a long synchronous call cannot tick, so the gap
// IS the freeze in milliseconds — no window manager, no display, no
// screenshots needed.
//
// Driven over MCP against the REAL binary (the import.shutdown pattern),
// because the whole point is that OTHER work gets serviced while the open is
// in flight: every poll in this test is an HTTP request the app can only
// answer between slices.
//
// Three contracts:
//   1. project.openAsync of a Showroom-sized world completes, with the scene
//      really open, and no UI-thread gap beyond the budget.
//   2. project.open (the synchronous verb every script and headless run uses)
//      still opens the same world — unchanged behaviour, deliberately.
//   3. Quitting with an open IN FLIGHT terminates the process, bounded and
//      clean (the import.shutdown zombie, applied to the open runner).
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpServer>
#include <QThread>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

/// The gap budget. GNOME's "not responding" prompt follows ~5 s of unanswered
/// pings; 500 ms is the lane's contract and leaves an order of magnitude of
/// headroom. Measured on this machine, the worst slice is the engine's
/// geometry push.
static const double kMaxGapMs = 500.0;
/// The cold-process ceiling: the first open of a process also pays the
/// engine's shader/PSO compilation (see the comment at the cold open).
static const double kColdCeilingMs = 4000.0;
static const int kExitBudgetMs = 30000;
static const int kOpenBudgetMs = 120000;

struct McpClient
{
    QNetworkAccessManager net;
    QUrl url;
    QString token;
    int id = 0;

    QJsonObject post(const QJsonObject &body)
    {
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        QNetworkReply *reply = net.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        return QJsonDocument::fromJson(data).object();
    }

    void initialize()
    {
        post(QJsonObject{ { "jsonrpc", "2.0" }, { "id", ++id }, { "method", "initialize" },
                          { "params", QJsonObject{
                                { "protocolVersion", "2025-06-18" },
                                { "capabilities", QJsonObject{} },
                                { "clientInfo", QJsonObject{ { "name", "openasync-test" }, { "version", "0" } } } } } });
        post(QJsonObject{ { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } });
    }

    QJsonObject runScript(const QString &script)
    {
        const QJsonObject reply = post(QJsonObject{
            { "jsonrpc", "2.0" }, { "id", ++id }, { "method", "tools/call" },
            { "params", QJsonObject{ { "name", "run_script" },
                                     { "arguments", QJsonObject{ { "script", script } } } } } });
        const QJsonArray content = reply.value("result").toObject().value("content").toArray();
        if (content.isEmpty()) return {};
        return QJsonDocument::fromJson(
            content.first().toObject().value("text").toString().toUtf8()).object();
    }
};

/// Same reason as import.shutdown: keep the close path prompt-free.
static void seedSettings(const QString &binary)
{
    QStringList inis;
    inis << QFileInfo(binary).dir().filePath("jahsettings.ini");
#ifndef QT_DEBUG
    const QString testName = QCoreApplication::applicationName();
    QCoreApplication::setApplicationName(QStringLiteral("Jahshaka"));
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QCoreApplication::setApplicationName(testName);
    if (!appData.isEmpty() && QDir().mkpath(appData))
        inis << QDir(appData).filePath("jahsettings.ini");
#endif
    for (const QString &ini : inis) {
        QSettings settings(ini, QSettings::IniFormat);
        settings.setValue("ddialog_seen", true);
        settings.setValue("auto_save", true);
        settings.sync();
    }
}

static quint16 freePort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    return probe.serverPort();
}

static bool spawn(QProcess &jahshaka, quint16 port, QString *tokenOut)
{
    jahshaka.setProcessChannelMode(QProcess::MergedChannels);
    jahshaka.start(QStringLiteral(JAHSHAKA_BINARY),
                   { QStringLiteral("--mcp-port=%1").arg(port) });
    if (!jahshaka.waitForStarted(15000)) return false;
    QByteArray bootLog;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 120000 && jahshaka.state() == QProcess::Running) {
        jahshaka.waitForReadyRead(500);
        bootLog += jahshaka.readAll();
        const int at = bootLog.indexOf("MCP: token ");
        if (at >= 0) {
            const int end = bootLog.indexOf('\n', at);
            if (end > at) {
                *tokenOut = QString::fromUtf8(bootLog.mid(at + 11, end - at - 11)).trimmed();
                return true;
            }
        }
    }
    std::printf("---- boot log ----\n%s\n", bootLog.constData());
    return false;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    seedSettings(QStringLiteral(JAHSHAKA_BINARY));

    const QString sample =
        QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/scenes/Showroom.zip");
    CHECK(QFileInfo::exists(sample), "Showroom sample archive present");

    QProcess jahshaka;
    QString token;
    const quint16 port = freePort();
    CHECK(spawn(jahshaka, port, &token), "app booted and printed the MCP token");
    if (token.isEmpty()) return 1;

    McpClient mcp;
    mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    mcp.token = token;
    mcp.initialize();

    // ---- import the world once; both opens below use it -------------------
    const QJsonObject imported = mcp.runScript(
        QStringLiteral("project.importArchive('%1')").arg(sample));
    const QString guid = imported.value("result").toObject().value("guid").toString();
    CHECK(imported.value("ok").toBool() && guid.length() > 10,
          "Showroom.zip imported");
    if (guid.isEmpty()) { jahshaka.kill(); jahshaka.waitForFinished(5000); return 1; }

    // ---- 1. the THREADED open, with the UI thread under measurement -------
    //
    // TWICE, and the difference is the finding. The FIRST open of a process
    // also pays the engine's shader/PSO compilation: Ogre's Hlms builds a
    // shader variant per material/pass permutation on the first frames of the
    // world, inside Engine::renderOneFrame — UI-thread work this lane cannot
    // move or slice, and the pin persists NONE of it (no HlmsDiskCache, no
    // GpuProgramManager microcode cache, no VkPipelineCache blob is wired;
    // lane-openasync audit, 2026-09-03). It is a per-PROCESS cost: every open
    // after the first reuses the compiled variants, which is what the warm
    // budget below asserts.
    const auto openAsyncAndWait = [&](const char *label) {
        mcp.runScript(QStringLiteral("app.heartbeat(250)"));
        const bool started =
            mcp.runScript(QStringLiteral("project.openAsync('%1')").arg(guid)).value("ok").toBool();
        int polls = 0;
        bool done = false;
        QElapsedTimer openTimer;
        openTimer.start();
        while (started && openTimer.elapsed() < kOpenBudgetMs) {
            const QJsonObject state = mcp.runScript(QStringLiteral("project.openState()"));
            ++polls;
            if (state.value("ok").toBool() &&
                state.value("result").toString() == QLatin1String("idle")) { done = true; break; }
            QThread::msleep(50);
        }
        const QJsonObject stats = mcp.runScript(QStringLiteral("app.heartbeatStats()"))
                                      .value("result").toObject();
        std::printf("info: [%s] open finished=%d after %lld ms, %d polls; "
                    "heartbeat ticks=%d maxGapMs=%.1f\n",
                    label, int(done), static_cast<long long>(openTimer.elapsed()), polls,
                    stats.value("ticks").toInt(), stats.value("maxGapMs").toDouble());
        struct R { bool started, done; int polls, ticks; double maxGap; };
        return R{ started, done, polls, stats.value("ticks").toInt(),
                  stats.value("maxGapMs").toDouble() };
    };

    const auto cold = openAsyncAndWait("cold");
    CHECK(cold.started, "project.openAsync accepted (cold)");
    CHECK(cold.done, "the threaded open completed (cold)");
    CHECK(cold.polls >= 2, "the app answered requests WHILE the open was in flight");
    CHECK(cold.ticks > 0, "the UI thread kept ticking during the cold open");
    // The cold ceiling is a REGRESSION guard, not the contract: it is above
    // the engine's compile storm and far below the multi-second document
    // blocking this lane removed (the pre-fix Matcaps open spent 12.5 s on
    // this thread).
    CHECK(cold.maxGap > 0.0 && cold.maxGap < kColdCeilingMs,
          "the cold open's worst UI gap stays under the regression ceiling");

    const QJsonObject nodes = mcp.runScript(QStringLiteral("scene.nodes().length"));
    std::printf("info: nodes after the threaded open: %d\n", nodes.value("result").toInt());
    CHECK(nodes.value("ok").toBool() && nodes.value("result").toInt() > 1,
          "the threaded open really loaded the world");

    // WARM: same world, same code path, shader variants already compiled.
    // THIS is the lane's contract — the document, asset and engine-push work
    // of an open must never block the UI thread past the budget.
    CHECK(mcp.runScript(QStringLiteral("project.close()")).value("ok").toBool(),
          "project.close before the warm open");
    const auto warm = openAsyncAndWait("warm");
    CHECK(warm.done, "the threaded open completed (warm)");
    CHECK(warm.ticks > 0, "the UI thread kept ticking during the warm open");
    CHECK(warm.maxGap > 0.0 && warm.maxGap < kMaxGapMs,
          "no UI-thread gap beyond the budget during a warm threaded open");

    // ---- 2. the SYNCHRONOUS verb is unchanged -----------------------------
    CHECK(mcp.runScript(QStringLiteral("project.close()")).value("ok").toBool(),
          "project.close");
    mcp.runScript(QStringLiteral("app.heartbeat(0)"));
    CHECK(mcp.runScript(QStringLiteral("project.open('%1')").arg(guid)).value("ok").toBool(),
          "project.open (synchronous) still opens the world");
    const QJsonObject syncNodes = mcp.runScript(QStringLiteral("scene.nodes().length"));
    CHECK(syncNodes.value("ok").toBool() && syncNodes.value("result").toInt() > 1,
          "the synchronous open loaded the same world");
    // Same world, same content: the two paths are not allowed to diverge.
    CHECK(syncNodes.value("result").toInt() == nodes.value("result").toInt(),
          "both open paths produce the same node count");

    // The ledger is populated and names its stages (the profiling contract).
    const QJsonObject timings = mcp.runScript(QStringLiteral(
        "(function(){var t=app.openTimings();var m={};for(var i=0;i<t.length;i++)"
        "m[t[i].stage]=Math.round(t[i].ms);return m})()"));
    std::printf("info: open ledger: %s\n",
                QJsonDocument(timings.value("result").toObject()).toJson(QJsonDocument::Compact).constData());
    CHECK(timings.value("ok").toBool() &&
              timings.value("result").toObject().contains("total"),
          "app.openTimings() reports the ledger of the last open");

    // ---- 3. quit with an open IN FLIGHT -----------------------------------
    CHECK(mcp.runScript(QStringLiteral("project.close()")).value("ok").toBool(),
          "project.close before the shutdown case");
    CHECK(mcp.runScript(QStringLiteral("project.openAsync('%1')").arg(guid)).value("ok").toBool(),
          "second threaded open started");
    const QJsonObject inFlight = mcp.runScript(QStringLiteral("project.openState()"));
    std::printf("info: state when the quit was issued: %s\n",
                qUtf8Printable(inFlight.value("result").toString()));
    mcp.runScript(QStringLiteral("app.quit()"));

    QElapsedTimer exitTimer;
    exitTimer.start();
    const bool exited = jahshaka.waitForFinished(kExitBudgetMs);
    std::printf("info: exit after %lld ms\n", static_cast<long long>(exitTimer.elapsed()));
    CHECK(exited, "process terminated within the exit budget with an open in flight");
    if (!exited) {
        jahshaka.kill();
        jahshaka.waitForFinished(5000);
    } else {
        const bool clean = jahshaka.exitStatus() == QProcess::NormalExit && jahshaka.exitCode() == 0;
        if (!clean) {
            std::printf("info: exitStatus=%s exitCode=%d\n",
                        jahshaka.exitStatus() == QProcess::CrashExit ? "CrashExit" : "NormalExit",
                        jahshaka.exitCode());
            const QByteArray tail = jahshaka.readAll().right(3000);
            std::printf("---- app output tail ----\n%s\n-------------------------\n", tail.constData());
        }
        CHECK(clean, "process exited normally with code 0");
    }

    std::printf(failures ? "FAILED: %d check(s)\n" : "ALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
