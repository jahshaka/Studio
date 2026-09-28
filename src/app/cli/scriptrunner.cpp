/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "app/cli/scriptrunner.h"

#include <cstdio>
#include <cstdlib>

#include <QApplication>
#include <QEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QThreadPool>

#include "bridge/enginehost.h"
#include "services/framemonitor.h"
#include "services/materialpresetseeder.h"
#include "scripting/scriptengine.h"
#include "scripting/mcp/mcpserver.h"
#include "shell/mainwindow.h"
#include "services/mainthreadwatchdog.h"
#include "services/jahlog.h"
#include "shell/shutdownorder.h"
#include "services/testtier.h"

int finalizeAppExit(int rc)
{
    // STEP 3 of the shutdown order. The whole sequence is documented in one
    // place — at ~MainWindow (src/shell/mainwindow.cpp), enumerated in
    // src/shell/shutdownorder.h.
    //
    // NOTE WHAT THIS DOES NOT DO: EngineHost::shutdown() stops the render
    // driver, writes the shader cache and the warm-up set, and drops the
    // HOST's shared_ptr — it does NOT destroy the Engine, because the
    // viewport widgets hold their own copies. The Engine dies at step 5,
    // inside ~MainWindow, deliberately before the database closes.
    JAH_SHUTDOWN_STEP(ShutdownOrder::EngineHostRelease,
                      "finalizeAppExit: EngineHost::shutdown (driver + host ref)");

    // The CLI paths (--script, --dump-api-docs, --engine-selftest) never close
    // the window, so they never reach step 2 — and step 2 is where the
    // main-thread watchdog normally stops. Stopping it again here is a no-op
    // for the window-close path and the only stop the CLI paths get.
    MainThreadWatchdog::stop();

    // A CAPTURE STILL RUNNING IS FINISHED AND WRITTEN, here, before the engine
    // goes: the bundle's records live in the engine's ring until they are
    // drained, so quitting mid-capture would otherwise throw away everything
    // the owner was recording when they hit the problem — which is exactly the
    // capture worth keeping. Stopping it is idempotent and a no-op when nothing
    // is running.
    FrameMonitor::instance().stop();

    // THE PRESET SEED IS A WARM-UP, NEVER A REASON TO WAIT (its worker checks
    // the flag between files). Without this the pool's 5 s join below could
    // be spent finishing a seed nobody is waiting for — and a forced exit is
    // how a session log loses its close bracket.
    MaterialPresetSeeder::instance().requestAbort();

    // The engine borrows Qt's X display: release it before QApplication goes away.
    EngineHost::instance().shutdown();
    if (!QThreadPool::globalInstance()->waitForDone(5000)) {
        qWarning("shutdown: background workers still running 5s after exit — "
                 "forcing process exit (code %d)", rc);
        // The session log's close bracket is worth having even on the forced
        // path: an absent one is the signal that the session died, and this
        // exit is not a death.
        JahLog::stop(QStringLiteral("forced exit: background workers hung"));
        std::fflush(nullptr);
        std::_Exit(rc);
    }
    // THE CLOSE BRACKET (SESSION_LOG_SPEC §3.9) plus the session summary. This
    // is the choke point every ordinary exit passes through — the window close
    // path, --script and --mcp-port all end here.
    JahLog::stop(QStringLiteral("exit code %1").arg(rc));
    return rc;
}

namespace {

// THE TEST TIER'S WINDOW (TEST-TIER-1, services/testtier.h): a windowed script
// run whose process has a test tier boots at 1280x720 instead of the screen's
// size — the chain's render targets follow the window. What app.resizeWindow
// does, before the first frame; an arm that needs another size resizes itself.
void applyTestTierWindow(MainWindow &window, QApplication &app, bool headless)
{
    if (headless || !testtier::active()) return;
    if (window.isFullScreen() || window.isMaximized()) window.showNormal();
    window.resize(testtier::kWindowWidth, testtier::kWindowHeight);
    app.processEvents();
}

}   // namespace

int runScriptFile(MainWindow &window, QApplication &app, const QString &path, bool headless,
                  bool live)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "script: cannot open %s\n", qPrintable(path));
        // THROUGH THE ORDERED EXIT, like every other way out of this file
        // (ledger 150, round 2): MainWindow's constructor has already started
        // the engine, so a bare `return 1` here left it to the exit-handler
        // chain — the unordered teardown that ended in a SIGSEGV inside
        // TextureCache::save.
        return finalizeAppExit(1);
    }
    const QString source = QString::fromUtf8(file.readAll());

    window.show();
    app.processEvents();
    applyTestTierWindow(window, app, headless);

    // A WINDOWED SCRIPT RUN SHOWS THE EDITOR PAGE, so a script has a live
    // viewport from its first line — and that is a difference from the boot a
    // PERSON gets, which lands on the Desktop page and shows the editor only
    // when they go there. `JAHSHAKA_TEST_NO_EDITOR_BOOT=1` asks for the
    // person's shape (SMOKE-FIX-1's fix round, F3's sibling): the window is up
    // on the desktop, the engine is running, and NOTHING has shown the editor
    // page — which is the only state in which "the Player page is what built
    // the editor's engine scene" can be observed at all, and the state the
    // owner's `--vr` smoke was actually in. A run that asks for it gets no
    // editor viewport until it opens one, so `editor.frame()` and everything
    // else that needs the editor's window refuses until then: only a suite
    // whose subject IS that boot should ask.
    const bool noEditorBoot = qEnvironmentVariableIntValue("JAHSHAKA_TEST_NO_EDITOR_BOOT") > 0;
    const bool editorBoot = !headless && !noEditorBoot;
    if (editorBoot) {
        QString why;
        if (!window.enterEditorOnNewScene(why)) {
            std::fprintf(stderr, "script: %s\n", qPrintable(why));
            return finalizeAppExit(1);      // ordered teardown, see above
        }
        // Let the engine settle (swapchain, first frames) like the selftest does.
        for (int frame = 0; frame < 10; ++frame) {
            app.processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(16);
        }
    }

    ScriptEngine *engine = window.scripting();
    QObject::connect(engine, &ScriptEngine::consoleOutput, [](const QString &t) {
        std::fprintf(stdout, "%s\n", qPrintable(t));
        std::fflush(stdout);
    });

    // OFF unless --script-live asked otherwise (SCRIPTING_LIVE_SPEC §3.1): a
    // command-line run must see no frame it did not ask for.
    const ScriptResult result = engine->evaluate(source, path, true, 0,
                                                 live ? ScriptRunPolicy::Live
                                                      : ScriptRunPolicy::Off);

    int rc = 0;
    if (!result.ok) {
        std::fprintf(stderr, "%s\n", qPrintable(result.toString()));
        if (!result.stack.isEmpty()) std::fprintf(stderr, "%s\n", qPrintable(result.stack));
        rc = 1;
    } else {
        const int typeId = result.value.typeId();
        if (typeId == QMetaType::Int || typeId == QMetaType::Double || typeId == QMetaType::LongLong)
            rc = qBound(0, result.value.toInt(), 255);
    }

    if (editorBoot) window.leaveEditorSpace();   // symmetrical with the begin above
    return finalizeAppExit(rc);
}

namespace {

struct PoolArm { QString name; QString path; };

// "e2e_gi_status.js" -> "gi_status"; "e2e_x.js.in" was configured to "e2e_x.js".
QString armNameFor(const QString &path)
{
    QString base = QFileInfo(path).fileName();
    if (base.endsWith(QLatin1String(".js"))) base.chop(3);
    if (base.startsWith(QLatin1String("e2e_"))) base.remove(0, 4);
    return base;
}

QList<PoolArm> poolArmsFrom(const QString &scripts)
{
    QList<PoolArm> arms;
    const QFileInfo asDir(scripts);
    if (asDir.isDir()) {
        const QStringList files = QDir(scripts).entryList({ QStringLiteral("*.js") },
                                                          QDir::Files, QDir::Name);
        for (const QString &f : files) {
            const QString path = QDir(scripts).filePath(f);
            arms.append({ armNameFor(path), path });
        }
        return arms;
    }
    for (const QString &entry : scripts.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const int eq = entry.indexOf(QLatin1Char('='));
        if (eq > 0) arms.append({ entry.left(eq), entry.mid(eq + 1) });
        else        arms.append({ armNameFor(entry), entry });
    }
    return arms;
}

void armLine(const char *fmt, const QByteArray &a, const QByteArray &b = {}, const QByteArray &c = {})
{
    std::fprintf(stdout, fmt, a.constData(), b.constData(), c.constData());
    std::fflush(stdout);
}

// The engine-up boot a one-script process gets (runScriptFile): the editor page
// shown on a new default scene, ten settling frames.
bool beginEditorBoot(MainWindow &window, QApplication &app, QString &why)
{
    if (!window.enterEditorOnNewScene(why)) return false;
    for (int frame = 0; frame < 10; ++frame) {
        app.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(16);
    }
    return true;
}

}   // namespace

int runScriptPool(MainWindow &window, QApplication &app, const QString &scripts,
                  const QString &poolIn, const QStringList &only, const QString &poolBaseline,
                  bool headless, bool live)
{
    const QString pool = poolIn.isEmpty() ? QStringLiteral("pool") : poolIn;
    const QByteArray poolUtf8 = pool.toUtf8();
    QList<PoolArm> arms = poolArmsFrom(scripts);
    int failed = 0;

    // --arms: the named subset, in the POOL's order. A name the pool does not
    // have is a FAIL line of its own — a typo in a solo retry must not read as
    // "nothing failed".
    if (!only.isEmpty()) {
        QList<PoolArm> picked;
        for (const PoolArm &arm : arms)
            if (only.contains(arm.name)) picked.append(arm);
        for (const QString &name : only) {
            bool known = false;
            for (const PoolArm &arm : arms) known = known || arm.name == name;
            if (!known) {
                armLine("ARM %s.%s FAIL 0 no such arm in this pool\n", poolUtf8, name.toUtf8());
                ++failed;
            }
        }
        arms = picked;
    }
    if (arms.isEmpty() && failed == 0) {
        std::fprintf(stderr, "pool %s: no arms in '%s'\n", poolUtf8.constData(), qPrintable(scripts));
        return finalizeAppExit(1);
    }

    window.show();
    app.processEvents();
    applyTestTierWindow(window, app, headless);

    const bool noEditorBoot = qEnvironmentVariableIntValue("JAHSHAKA_TEST_NO_EDITOR_BOOT") > 0;
    const bool editorBoot = !headless && !noEditorBoot;
    if (editorBoot) {
        QString why;
        if (!beginEditorBoot(window, app, why)) {
            std::fprintf(stderr, "pool %s: %s\n", poolUtf8.constData(), qPrintable(why));
            return finalizeAppExit(1);
        }
    }

    ScriptEngine *engine = window.scripting();
    QObject::connect(engine, &ScriptEngine::consoleOutput, [](const QString &t) {
        std::fprintf(stdout, "%s\n", qPrintable(t));
        std::fflush(stdout);
    });
    const ScriptRunPolicy policy = live ? ScriptRunPolicy::Live : ScriptRunPolicy::Off;
    QString bootWindow;     // "<w>x<h>" as the boot had it (engine-up pools)
    QString poolBaselineSource;
    if (!poolBaseline.isEmpty()) {
        QFile f(poolBaseline);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            std::fprintf(stderr, "pool %s: cannot open --pool-baseline %s\n", poolUtf8.constData(),
                         qPrintable(poolBaseline));
            return finalizeAppExit(1);
        }
        poolBaselineSource = QString::fromUtf8(f.readAll());
    }
    if (editorBoot) {
        const ScriptResult w = engine->evaluate(
            QStringLiteral("(function(){var w = app.window(); return w.width + 'x' + w.height;})()"),
            QStringLiteral("<pool-baseline>"), false, 0, ScriptRunPolicy::Off);
        if (w.ok) bootWindow = w.value.toString();
    }

    // WHAT THIS PROCESS HOLDS AT BOOT (TEST-TIER-1): one `POOL-MEM` line per
    // process, through the verbs a script would use, for the driver to put in
    // the run log (run_pool.py prints it as `MEM <pool> …` with the process's
    // own nvidia-smi figure beside it). gpuPoolUsed = the VaoManager pools'
    // capacity minus their free bytes (textures included on Vulkan); textures =
    // every texture the texture manager knows.
    //
    // ...AND AFTER EVERY ARM (the leak probe): the same two figures once the arm's baseline
    // has held — the project closed, the deferred deletes delivered — as
    // `POOL-MEM <pool>.<arm> gpuPoolUsed=<MB> textures=<MB>`, so the pool's VRAM across its
    // arms is a curve at one comparable state, and run_pool.py can call a monotonic climb.
    auto memFigures = [&]() -> QString {
        if (headless) return QStringLiteral("headless");
        const ScriptResult m = engine->evaluate(
            QStringLiteral("(function(){var m = app.memoryStats(); var t = app.textureMemory({top: 1});"
                           "return 'gpuPoolUsed=' + Math.round((m.gpuPoolCapacityBytes - m.gpuPoolFreeBytes) / 1048576)"
                           " + ' textures=' + Math.round(t.totalBytes / 1048576);})()"),
            QStringLiteral("<pool-mem>"), false, 0, ScriptRunPolicy::Off);
        return m.ok ? m.value.toString()
                    : QStringLiteral("unavailable (%1)").arg(m.toString().simplified());
    };
    {
        const QString tier = testtier::active() ? testtier::name() : QStringLiteral("document");
        armLine("POOL-MEM %s %s tier=%s\n", poolUtf8, memFigures().toUtf8(), tier.toUtf8());
    }

    for (int i = 0; i < arms.size(); ++i) {
        const PoolArm &arm = arms.at(i);
        const QByteArray armUtf8 = arm.name.toUtf8();
        armLine("ARM-BEGIN %s.%s\n", poolUtf8, armUtf8);
        QElapsedTimer clock;
        clock.start();

        // THE ARM'S BASELINE, part 1 — NOT a second boot. The first arm has the
        // boot a one-script process gets; a later arm starts where the previous
        // arm's baseline left the window (the project closed, the desktop page
        // up) and reaches the editor through the product's own route, the
        // `project.create`/`project.open` it begins with. Re-showing the editor
        // page after an arm had a camera PiP up lost the device with an Xid 13
        // "3D WIDTH ZT Violation" (lane SUITE-POOL-1's first run,
        // spikes/suite-pool-1/xid/): the View kept the closed scene's PiP
        // request and rebuilt it on the new scene (VIEWS-XID-1, fixed in the
        // engine and the viewport; the second route onto the page is deleted).
        // A path no user takes is not a baseline.
        // ...part 2: a fresh JavaScript realm — no global of an earlier arm.
        if (!engine->resetScriptContext()) {
            armLine("POOL-BASELINE-LOST %s.%s %s\n", poolUtf8, armUtf8,
                    QByteArrayLiteral("the script realm could not be reset"));
            return finalizeAppExit(qBound(1, failed + 1, 255));
        }

        QString failure;
        QFile file(arm.path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            failure = QStringLiteral("cannot open %1").arg(arm.path);
        } else {
            const QString source = QString::fromUtf8(file.readAll());
            const ScriptResult result = engine->evaluate(source, arm.path, true, 0, policy);
            if (!result.ok) {
                std::fprintf(stderr, "%s\n", qPrintable(result.toString()));
                if (!result.stack.isEmpty()) std::fprintf(stderr, "%s\n", qPrintable(result.stack));
                failure = result.toString().section(QLatin1Char('\n'), 0, 0);
            } else {
                // The one-script contract: a numeric completion value is the
                // exit code, so a non-zero one is this arm's failure count.
                const int typeId = result.value.typeId();
                if ((typeId == QMetaType::Int || typeId == QMetaType::Double ||
                     typeId == QMetaType::LongLong) && result.value.toInt() != 0)
                    failure = QStringLiteral("completion value %1").arg(result.value.toInt());
            }
        }
        const QByteArray ms = QByteArray::number(clock.elapsed());
        if (failure.isEmpty()) {
            armLine("ARM %s.%s PASS %s\n", poolUtf8, armUtf8, ms);
        } else {
            ++failed;
            armLine("ARM %s.%s FAIL %s", poolUtf8, armUtf8, ms);
            armLine(" %s\n", failure.simplified().toUtf8());
        }

        // THE POOL'S OWN BASELINE FIRST (--pool-baseline), after every arm,
        // green or red: what a family's arms change in the process and cannot
        // put back on a red path (a throw skips a script's own tail). Its
        // failure is a lost baseline like the runner's.
        if (!poolBaselineSource.isEmpty()) {
            const ScriptResult pb = engine->evaluate(poolBaselineSource, poolBaseline, false, 0,
                                                     ScriptRunPolicy::Off);
            if (!pb.ok) {
                armLine("POOL-BASELINE-LOST %s.%s %s\n", poolUtf8, armUtf8,
                        pb.toString().simplified().toUtf8());
                if (editorBoot) window.leaveEditorSpace();
                return finalizeAppExit(qBound(1, failed, 255));
            }
        }

        // THE BASELINE AFTER THE ARM, THROUGH THE VERBS (API-first: the pool
        // uses what a script uses). Whatever project the arm left open is
        // closed; if one is still open afterwards the next arm cannot start
        // from the boot's state, so the process ends here and the driver
        // restarts it for the arms that remain.
        //
        // THE WINDOW TOO (engine-up): an arm that resized the window or left it
        // in immersive full screen (editor_controls does both) would hand the
        // next arm a different viewport — its framing, its pick pixels. Put
        // back to the size the boot had, through the same verbs.
        QString windowBaseline;
        if (editorBoot && !bootWindow.isEmpty()) {
            windowBaseline = QStringLiteral(
                "if (editor.fullscreen()) editor.fullscreen(false);"
                "(function(){var w = app.window(); if (w.width + 'x' + w.height !== '%1') app.resizeWindow(%2, %3);})();")
                .arg(bootWindow, bootWindow.section(QLatin1Char('x'), 0, 0),
                     bootWindow.section(QLatin1Char('x'), 1, 1));
        }
        const ScriptResult base = engine->evaluate(
            windowBaseline +
                QStringLiteral("if (project.current()) project.close(); !project.current()"),
            QStringLiteral("<pool-baseline>"), false, 0, ScriptRunPolicy::Off);
        // ...and what the close handed to the event loop to delete is deleted
        // now, not in the middle of the next arm's measurement (doc.memory
        // counted a previous arm's node leaving during its own burst).
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        app.processEvents();
        if (!headless)
            armLine("POOL-MEM %s.%s %s\n", poolUtf8, armUtf8, memFigures().toUtf8());
        if (!base.ok || !base.value.toBool()) {
            armLine("POOL-BASELINE-LOST %s.%s %s\n", poolUtf8, armUtf8,
                    (base.ok ? QStringLiteral("a project is still open after project.close()")
                             : base.toString()).simplified().toUtf8());
            if (i + 1 < arms.size()) {
                if (editorBoot) window.leaveEditorSpace();
                return finalizeAppExit(qBound(1, failed, 255));
            }
        }
    }

    if (editorBoot) window.leaveEditorSpace();
    return finalizeAppExit(qBound(0, failed, 255));
}

int runMcpServe(MainWindow &window, QApplication &app, unsigned short port, bool headless)
{
    window.show();
    app.processEvents();

    if (!headless) {
        // Same boot as a windowed script run: editor page shown, engine view
        // live, a default scene up — screenshot works immediately, and
        // run_script's project.create() switches to a real project.
        QString why;
        if (!window.enterEditorOnNewScene(why)) {
            std::fprintf(stderr, "mcp: %s\n", qPrintable(why));
            // THROUGH THE SAME EXIT AS A SUCCESSFUL RUN (ledger 150). A bare
            // `return 1` here skipped EngineHost::shutdown() entirely, leaving
            // the engine to be torn down from ~EngineHost at static-destruction
            // time — after Qt, after the database, and after this library's own
            // statics: exactly the shape of the SIGSEGV in TextureCache::save
            // that a doomed --mcp-port produced. Every way out of this function
            // is now the ordered one.
            return finalizeAppExit(1);
        }
        for (int frame = 0; frame < 10; ++frame) {
            app.processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(16);
        }
    }

    QString error;
    if (!window.startMcpServer(port, &error)) {
        std::fprintf(stderr, "mcp: %s\n", qPrintable(error));
        return finalizeAppExit(1);       // ordered teardown, see above
    }

    McpServer *mcp = window.mcp();
    // THE PORT, ON ITS OWN LINE AND MACHINE-READABLE (TEST_GATE_AUDIT.md §4.1).
    // `--mcp-port=0` binds an EPHEMERAL port, which is the only way several
    // driver suites can boot the app at once — two of them hard-coded 8751 and
    // were kept apart by RUN_SERIAL alone, which is what made -j4 unsafe. A
    // caller that asked for 0 has to be told what it got, and parsing it out of
    // the URL line is a URL parser every shell script would have to carry.
    std::printf("MCP: port %u\n", unsigned(mcp->port()));
    std::printf("MCP: listening on http://127.0.0.1:%u/mcp\n", unsigned(mcp->port()));
    std::printf("MCP: token %s\n", qPrintable(mcp->token()));
    std::printf("MCP: connect with: %s\n", qPrintable(mcp->connectCommand()));
    std::fflush(stdout);

    const int rc = app.exec();
    return finalizeAppExit(rc);
}

int runDumpApiDocs(MainWindow &window, const QString &outPath)
{
    QFile docs(outPath);
    if (!docs.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        std::fprintf(stderr, "dump-api-docs: cannot write %s\n", qPrintable(outPath));
        return 1;
    }
    docs.write(window.scripting()->registry().markdown().toUtf8());
    std::fprintf(stderr, "dump-api-docs: wrote %s\n", qPrintable(outPath));
    return 0;
}
