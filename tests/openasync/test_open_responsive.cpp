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
//   1. project.openAsync of a heavy world completes, with the scene really
//      open, and no UI-thread gap beyond the budget — a budget that is the
//      fixed contract OR the box's own measured scheduling noise, whichever is
//      larger (kControlFactor below; the suite reds under a -j4 gate
//      otherwise, ledger 416, while the UI thread is not blocked at all).
//
// THE FIXTURE IS Matcaps (2026-09-07). It used to be Showroom, because that
// sample was the biggest world the tree shipped; the Grand Showroom that
// replaced it is 23 primitives that open in a blink, which would leave this
// suite measuring nothing (it needs an open long enough for the poll loop to
// catch the app answering mid-flight). Matcaps carries the Stanford dragon —
// the heaviest single mesh in the samples, and the world whose 12.5 s
// synchronous open is quoted below.
//   2. project.open (the synchronous verb every script and headless run uses)
//      still opens the same world — unchanged behaviour, deliberately.
//   3. Quitting with an open IN FLIGHT terminates the process, bounded and
//      clean (the import.shutdown zombie, applied to the open runner).
#include "../support/mcpharness.h"
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
using namespace mcpharness;

#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

/// The gap budget. GNOME's "not responding" prompt follows ~5 s of unanswered
/// pings; 500 ms is the lane's contract and leaves an order of magnitude of
/// headroom. Measured on this machine, the worst slice is the engine's
/// geometry push.
static const double kMaxGapMs = 500.0;
/// The heartbeat probe's interval (app.heartbeat(kHeartbeatMs)); a warm open
/// shorter than this can produce no tick at all.
static const double kHeartbeatMs = 250.0;
/// The cold-process ceiling: the first open of a process also pays the
/// engine's shader/PSO compilation (see the comment at the cold open).
static const double kColdCeilingMs = 4000.0;
static const int kExitBudgetMs = 30000;
static const int kOpenBudgetMs = 120000;

/// AND A CONTROL, because this is a MEASUREMENT on a shared box (ledger 416).
/// The failing mode is not a blocked UI thread: at load average 4-8, with
/// another lane compiling, the same warm open takes ~700 ms and the probe reads
/// a real 500-600 ms gap — the thread was not blocked, it was not SCHEDULED.
///
/// WHAT DOES NOT WORK, measured here before it was written (40 spinners on a
/// 20-core box, load average 25-36): avatar.responsive's IDLE noise floor. It
/// read 238-303 ms while the warm open in the same process read 552, 701 and
/// 732 ms — it under-reports contention by ~2.3x on those numbers (its 100 ms
/// probe already carried ~150 ms of contention above its quiet ~108), and the
/// reason is physics, not tuning. An idle app's UI thread is runnable for
/// microseconds every 250 ms and the scheduler hands a long-sleeping task the
/// CPU almost immediately; the engine even SKIPS still frames, so at rest the
/// thread barely competes at all. Contention only bites a thread that BURNS
/// its slice, which is what the thread does while a world opens.
///
/// So the baseline is a CONTROL, not an idle reading: the same app, the same
/// world, the same box, doing UI-THREAD WORK OF THE SAME KIND with no open in
/// flight — editor.frame() renders frames on this thread exactly as the open's
/// neighbours do. The control therefore carries the scheduling weather AND the
/// frame cost, and the assertion becomes what this suite actually means: the
/// open must not block the thread MORE THAN RUNNING THE APP DOES — read
/// honestly: a 250 ms probe never fires early, so every gap carries the probe
/// PERIOD as a floor, and the factor doubles that period along with the
/// contention (quiet: 500 + 2 x (control - 250) ~ 540). The CEILING below is
/// the guard against a real regression, not the factor.
///
/// THE RULE, and the numbers behind it (measured on this box, 2026-09-15, with
/// 40 spin loops on 20 cores at load average 33-44):
///
///     budget = min(max(kMaxGapMs, kControlFactor x control), kBudgetCeilingMs)
///
///   quiet   control 269 ms  open  237 ms  budget 538
///   loaded  control 331 ms  open  584 ms  budget 662
///   loaded  control 425 ms  open  576 ms  budget 850
///   loaded  control 309 ms  open  545 ms  budget 618
///
/// The open costs MORE than the control under load (its frames also mirror a
/// freshly-built scene and push it to the engine) and slightly less when the
/// box is quiet, which is why the factor is 2 and not 1. The CEILING is there
/// so the mechanism cannot degenerate: no amount of box load buys this suite
/// permission to accept a multi-second block, and the defect it was written for
/// was 12 500 ms — twenty-five times the highest budget this rule can produce.
///
/// AND ONE REPEAT before the red (see the warm case): the gap and the control
/// are each a single roll of a shared box's scheduler, and at the extreme load
/// above the two do not always land in the same weather. A blocking regression
/// is not a roll — it fails every attempt — so a second measurement costs four
/// seconds on the way to a red and nothing at all on a green run.
static const double kControlFactor = 2.0;
static const double kBudgetCeilingMs = 2000.0;

/// THE CONTROL (see kControlFactor): the worst heartbeat gap over ~2 s of
/// the app rendering the open world on its UI thread, with no open in flight.
/// editor.frame(1) is one synchronous document->engine sync plus one
/// renderOneFrame — the same thread doing the same kind of work, under whatever
/// load the box is under right now.
static double measureControlGap(McpClient &mcp, const char *label)
{
    mcp.runScript(QStringLiteral("app.heartbeat(0)"));
    mcp.runScript(QStringLiteral("app.heartbeat(%1)").arg(int(kHeartbeatMs)));
    QElapsedTimer timer;
    timer.start();
    int frames = 0;
    while (timer.elapsed() < 2000) {
        mcp.runScript(QStringLiteral("editor.frame(1)"));
        ++frames;
    }
    const double gapMs = mcp.runScript(QStringLiteral("app.heartbeatStats()"))
                             .value("result").toObject().value("maxGapMs").toDouble();
    std::printf("info: [%s] control: %d rendered frames in 2 s, worst UI gap %.1f ms "
                "(probe period %.0f ms)\n", label, frames, gapMs, kHeartbeatMs);
    return gapMs;
}

/// The control, turned into this run's budget (see kControlFactor).
static double budgetFor(double fixedMs, double controlMs, const char *label)
{
    const double budget = qMin(qMax(fixedMs, kControlFactor * controlMs), kBudgetCeilingMs);
    std::printf("info: [%s] UI-gap budget for this run: %.1f ms "
                "(fixed %.0f, control %.1f x %.1f, ceiling %.0f)\n",
                label, budget, fixedMs, controlMs, kControlFactor, kBudgetCeilingMs);
    return budget;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    seedSettings(QStringLiteral(JAHSHAKA_BINARY));

    const QString sample =
        QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/scenes/Matcaps.zip");
    CHECK(QFileInfo::exists(sample), "Matcaps sample archive present");

    QProcess jahshaka;
    QString token;
    const quint16 port = freePort();
    CHECK(spawn(jahshaka, port, &token), "app booted and printed the MCP token");
    if (token.isEmpty()) return 1;

    McpClient mcp;
    mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    mcp.token = token;
    mcp.clientName = QStringLiteral("openasync-test");
    mcp.initialize();

    // ---- import the world once; both opens below use it -------------------
    const QJsonObject imported = mcp.runScript(
        QStringLiteral("project.importArchive('%1')").arg(sample));
    const QString guid = imported.value("result").toObject().value("guid").toString();
    CHECK(imported.value("ok").toBool() && guid.length() > 10,
          "Matcaps.zip imported");
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
        mcp.runScript(QStringLiteral("app.heartbeat(%1)").arg(int(kHeartbeatMs)));
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
        // The open's duration is read BEFORE the stats round trip: read after
        // it, a 229 ms open could show > 250 ms elapsed with 0 ticks (the tick
        // fires while the stats are answered) and fail both halves.
        const double elapsedMs = double(openTimer.elapsed());
        const QJsonObject stats = mcp.runScript(QStringLiteral("app.heartbeatStats()"))
                                      .value("result").toObject();
        std::printf("info: [%s] open finished=%d after %lld ms, %d polls; "
                    "heartbeat ticks=%d maxGapMs=%.1f\n",
                    label, int(done), static_cast<long long>(elapsedMs), polls,
                    stats.value("ticks").toInt(), stats.value("maxGapMs").toDouble());
        struct R { bool started, done; int polls, ticks; double maxGap; double elapsedMs; };
        return R{ started, done, polls, stats.value("ticks").toInt(),
                  stats.value("maxGapMs").toDouble(), elapsedMs };
    };

    const auto cold = openAsyncAndWait("cold");
    CHECK(cold.started, "project.openAsync accepted (cold)");
    CHECK(cold.done, "the threaded open completed (cold)");
    CHECK(cold.polls >= 2, "the app answered requests WHILE the open was in flight");
    CHECK(cold.ticks > 0 || cold.elapsedMs < kHeartbeatMs,
          "the UI thread kept ticking during the cold open (or the open finished "
          "inside the first heartbeat interval — the cold open runs 330-570 ms now)");
    // The cold ceiling is a REGRESSION guard, not the contract: it is above
    // the engine's compile storm and far below the multi-second document
    // blocking this lane removed (the pre-fix Matcaps open spent 12.5 s on
    // this thread).
    // The cold ceiling stays a FIXED number: it guards a 12.5 s regression with
    // 4 s, and the worst this box produced with 40 spinners on 20 cores was
    // 1 002 ms — four times the headroom is enough, and a control measured
    // before any world is open would be measuring an app that renders nothing.
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
    // A warm open that FINISHES inside the first heartbeat interval cannot be
    // asked for a tick (the probe fires every kHeartbeatMs; the open path got
    // faster than that — ~220 ms measured 2026-09-15, which read as "no ticks"
    // on an unmodified tree, 4/4 solo). The gap reading still covers it:
    // maxGapMs is max(worst gap, time since the last tick or the start), so a
    // blocked thread shows there whether or not a tick ever fired.
    CHECK(warm.ticks > 0 || warm.elapsedMs < kHeartbeatMs,
          "the UI thread kept ticking during the warm open (or the open finished "
          "inside the first heartbeat interval)");
    // The control is measured HERE, after the open and with the same world open:
    // same app, same box, same second, the thread doing the same kind of work
    // with nothing in flight (see kControlFactor).
    const double warmBudget = budgetFor(kMaxGapMs, measureControlGap(mcp, "warm"), "warm");
    bool withinBudget = warm.maxGap > 0.0 && warm.maxGap < warmBudget;
    if (!withinBudget) {
        // ONE REPEAT, and only on the way to a red. Both the gap and the
        // control are single rolls of a shared box's scheduler: at load average
        // 37-46 the same build measured 486/375, 673/527 and 764/319 in three
        // consecutive runs — the third is not a different app, it is a
        // different roll. The defect this asserts against (a synchronous
        // 12.5 s open) is not a roll: it would fail both attempts, and every
        // attempt after them. The numbers of both are printed either way.
        std::printf("info: the warm open's worst gap (%.1f ms) exceeded this run's budget "
                    "(%.1f ms) — repeating the measurement once\n",
                    warm.maxGap, warmBudget);
        CHECK(mcp.runScript(QStringLiteral("project.close()")).value("ok").toBool(),
              "project.close before the repeated warm open");
        const auto warm2 = openAsyncAndWait("warm-2");
        const double budget2 = budgetFor(kMaxGapMs, measureControlGap(mcp, "warm-2"), "warm-2");
        withinBudget = warm2.done && warm2.maxGap > 0.0 && warm2.maxGap < budget2;
    }
    CHECK(withinBudget,
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
    mcp.quit();

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
