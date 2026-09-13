// app.shutdown_order — the shutdown sequence, asserted instead of remembered
// (STABILITY_PROGRAM_SPEC.md §1.5 / Lane 3).
//
// Nobody had written the order down, and twice that cost a defect: undo
// commands writing to a database ~MainWindow had already closed (`740e0155`),
// and the Engine — held by shared_ptr copies in the viewport widgets — dying
// at Qt's child-tree teardown, i.e. AFTER closeDatabase(), with the engine
// teardown law running there. src/shell/shutdownorder.h is the written order;
// this is the test that keeps it true.
//
// The app prints "[shutdown] step N/8 <name>" from each participant (debug
// builds). This spawns the REAL binary (the import.shutdown pattern), quits it
// through app.quit() — the normal close path, so all eight steps run — and
// asserts the eight steps appear exactly once each, in order.
//
// Step 3 (Modules) joined the sequence with the deep-audit memory lane:
// StudioModule::shutdown() is part of the module contract and had zero call
// sites, so the avatar module's documented "the document model goes before the
// engine does" guarantee did not hold. It runs BEFORE EngineHostRelease, which
// is the whole point, and steps 4-8 shifted up by one.
#include "mcpharness.h"

#include <QRegularExpression>
#include <QThread>

using namespace shutdownharness;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static const int kExitBudgetMs = 30000;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    seedSettings(QStringLiteral(JAHSHAKA_BINARY));

    QProcess jahshaka;
    QString token;
    QByteArray log;
    const quint16 port = freePort();
    CHECK(spawn(jahshaka, port, &token, &log), "app booted and printed the MCP token");
    if (token.isEmpty()) return 1;

    McpClient mcp;
    mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    mcp.token = token;
    mcp.initialize();

    // A project is opened first so the teardown has something real to take
    // apart: a scene, an engine scene mirrored into it, and an undo stack with
    // entries — which is exactly the state the two incidents happened in.
    const QJsonObject created = mcp.runScript(QStringLiteral("project.create('ShutdownOrder')"));
    CHECK(created.value("ok").toBool(), "a project was created for the teardown to unwind");
    mcp.runScript(QStringLiteral("node.add('cube')"));

    // Nothing has shut down yet.
    log += jahshaka.readAll();
    CHECK(!log.contains("[shutdown] step "), "no shutdown step fires before the quit");

    mcp.runScript(QStringLiteral("app.quit()"));

    QElapsedTimer exitTimer;
    exitTimer.start();
    const bool exited = jahshaka.waitForFinished(kExitBudgetMs);
    log += jahshaka.readAll();
    std::printf("info: exit after %lld ms\n", static_cast<long long>(exitTimer.elapsed()));
    CHECK(exited, "process terminated within the exit budget");
    if (!exited) { jahshaka.kill(); jahshaka.waitForFinished(5000); }
    else {
        const bool clean = jahshaka.exitStatus() == QProcess::NormalExit && jahshaka.exitCode() == 0;
        CHECK(clean, "process exited normally with code 0");
    }

    // ---- the sequence ------------------------------------------------------
    QVector<int> steps;
    QStringList names;
    QRegularExpression re(QStringLiteral(R"(\[shutdown\] step (\d)/8 ([^\n]*))"));
    auto it = re.globalMatch(QString::fromUtf8(log));
    while (it.hasNext()) {
        const auto m = it.next();
        steps.append(m.captured(1).toInt());
        names.append(m.captured(2).trimmed());
    }
    for (int i = 0; i < steps.size(); ++i)
        std::printf("info: step %d — %s\n", steps.at(i), qUtf8Printable(names.at(i)));

    CHECK(steps.size() == 8, "exactly eight shutdown steps were recorded");
    bool ordered = true, onceEach = true;
    for (int i = 0; i < steps.size(); ++i) {
        if (steps.at(i) != i + 1) ordered = false;
        if (steps.count(steps.at(i)) != 1) onceEach = false;
    }
    CHECK(ordered, "the steps ran 1..8 in order");
    CHECK(onceEach, "each step ran exactly once");

    // The recorder itself complains when the order is violated; a clean run
    // must contain none of those complaints.
    CHECK(!log.contains("fired again"), "no shutdown step fired twice");
    CHECK(!log.contains("fired AFTER step"), "no shutdown step ran out of order");

    // The load-bearing pairs, named. First: the engine's death is step 6 and
    // the database closes at step 7. If someone reorders them the app is back
    // to tearing the engine down against a closed connection.
    const int engineAt = steps.indexOf(6), dbAt = steps.indexOf(7);
    CHECK(engineAt >= 0 && dbAt >= 0 && engineAt < dbAt,
          "the engine-holding widgets are destroyed BEFORE the database closes");
    // Second: the modules shut down (3) while the engine host is still up (4),
    // which is the contract AvatarModule::shutdown() documents.
    const int modulesAt = steps.indexOf(3), hostAt = steps.indexOf(4);
    CHECK(modulesAt >= 0 && hostAt >= 0 && modulesAt < hostAt,
          "the modules shut down BEFORE the engine host is released");
    CHECK(!log.contains("the Engine is STILL referenced"),
          "no shared_ptr<Engine> holder outlived the viewports");

    // ---- THE EARLY QUIT (ledger 150) --------------------------------------
    // A CLI serving run that cannot do its job — `--mcp-port` on a port the
    // process may not bind — used to `return 1` straight out of runMcpServe,
    // skipping EngineHost::shutdown() entirely. The engine was then torn down
    // from ~EngineHost at STATIC-DESTRUCTION time: after Qt, after the
    // database, and after the engine library's own function-local statics, one
    // of which is the texture cache. `TextureCache::save` iterated a destroyed
    // std::map and the app died of SIGSEGV (fault address 0x20, inside
    // std::string::find) AFTER printing "[shutdown] step 8/8" — the crash the
    // rig caught on 2026-09-13.
    //
    // Port 1 is the reproduction: a real port number (so the parser accepts
    // it), never bindable by a non-root process (so the listen always fails),
    // and never in use by a sibling suite.
    {
        std::printf("-- early quit: a doomed --mcp-port must still exit in order\n");
        QProcess doomed;
        doomed.setProcessChannelMode(QProcess::MergedChannels);
        doomed.start(QStringLiteral(JAHSHAKA_BINARY), QStringList{ QStringLiteral("--mcp-port=1") });
        const bool started = doomed.waitForStarted(15000);
        CHECK(started, "the doomed run started");
        const bool finished = started && doomed.waitForFinished(kExitBudgetMs);
        QByteArray dlog = doomed.readAll();
        CHECK(finished, "the doomed run exited within the budget");
        if (!finished) { doomed.kill(); doomed.waitForFinished(5000); }

        CHECK(dlog.contains("cannot listen on 127.0.0.1:1"),
              "it failed the bind it was meant to fail");
        // THE POINT: a clean process exit, not a signal, and no backtrace file.
        CHECK(finished && doomed.exitStatus() == QProcess::NormalExit,
              "it exited NORMALLY (no SIGSEGV on the way out)");
        if (finished) std::printf("info: exit code %d\n", doomed.exitCode());
        CHECK(finished && doomed.exitCode() == 1, "...with the CLI failure code");
        CHECK(!dlog.contains("Jahshaka crashed"), "no crash backtrace was written");

        // ...and the teardown it skipped now runs, in order. Steps 1-3 belong
        // to a window CLOSE and never fire on a CLI path; step 4 is
        // finalizeAppExit and steps 5-8 are ~MainWindow, which is exactly the
        // ordered tail this case was missing.
        QVector<int> dsteps;
        QRegularExpression dre(QStringLiteral(R"(\[shutdown\] step (\d)/8 ([^\n]*))"));
        auto dit = dre.globalMatch(QString::fromUtf8(dlog));
        while (dit.hasNext()) dsteps.append(dit.next().captured(1).toInt());
        std::printf("info: early-quit steps:");
        for (int st : dsteps) std::printf(" %d", st);
        std::printf("\n");
        bool tail = true;
        const QVector<int> wanted{ 4, 5, 6, 7, 8 };
        if (dsteps != wanted) tail = false;
        CHECK(tail, "the early quit records steps 4,5,6,7,8 — the ordered tail — exactly once each");
        CHECK(!dlog.contains("fired again") && !dlog.contains("fired AFTER step"),
              "no early-quit step fired twice or out of order");
        if (!tail || !finished || doomed.exitStatus() != QProcess::NormalExit)
            std::printf("---- doomed run tail ----\n%s\n-------------------------\n",
                        dlog.right(4000).constData());
    }

    if (failures) {
        const QByteArray tail = log.right(4000);
        std::printf("---- app output tail ----\n%s\n-------------------------\n", tail.constData());
    }
    std::printf(failures ? "FAILED: %d check(s)\n" : "ALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
