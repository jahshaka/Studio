// archive.responsive — exporting and importing a world must not freeze the
// window (STABILITY_PROGRAM_SPEC.md Lane 4, REBASED shape).
//
// The archive path used to be two static functions that ran the whole thing —
// catalog sweep, CAS materialization, zip/unzip, catalog commit — on the UI
// thread. This asserts the landed split:
//
//   plan   (UI)    the DB reads. They cannot move: every Database method rides
//                  the implicit default QSqlDatabase connection, which belongs
//                  to the thread that opened it. There is NO per-thread
//                  connection here, deliberately — ImportBatchRunner and
//                  SceneOpenRunner both hop DB work back to the UI thread and
//                  Lane 4 follows them.
//   worker         extract / compress / copy. The part that used to freeze.
//   install(UI,    the catalog rows, one asset per event-loop turn, 1 ms apart.
//           SLICED)
//
// "Responsive" is measured, not asserted by eye: the app's heartbeat probe
// (app.heartbeat / app.heartbeatStats) reports the WORST gap between ticks on
// the UI thread, and a blocked UI thread cannot tick. Every poll below is an
// HTTP request the app can only answer between slices, so the polls are a
// second, independent proof.
//
// And correctness is not allowed to move: what comes back out of an archive
// this wrote is the same world, and a cancelled import leaves no orphan.
#include "../support/mcpharness.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
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
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>

#include "export/exportmanifest.h"
#include "io/ziphelper.h"

static int failures = 0;
using namespace mcpharness;

#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

/// THE BUDGET, and why this number.
///
/// The largest block an archive operation can still put on the UI thread is
/// ONE install slice — one asset's CAS ingest, i.e. hashing and copying a
/// single file — or the export's catalog sweep, which is pure SQLite over a
/// project's asset rows. Both are tens of milliseconds on the sample world.
/// 750 ms leaves an order of magnitude of headroom over that while staying an
/// order of magnitude BELOW the ~5 s of unanswered pings that makes GNOME
/// offer to force-quit the app — the failure this whole program exists to
/// remove. It is deliberately looser than open.responsive's 500 ms warm budget
/// because an archive run has no warm/cold distinction to hide behind: the
/// number below covers the first and only run in a cold process, engine
/// shader compilation included.
static const double kMaxGapMs = 750.0;
/// The heartbeat probe's interval (app.heartbeat(kHeartbeatMs)) — every gap
/// this suite reads carries this period as a floor, because a 250 ms probe
/// never fires early.
static const double kHeartbeatMs = 250.0;
static const int kOpBudgetMs = 180000;
static const int kExitBudgetMs = 30000;

/// AND A CONTROL, because 750 ms is a MEASUREMENT on a shared box and this
/// suite reds under a -j4 gate while the UI thread is not blocked at all
/// (ledger 416: 1 734.6 ms against the 750 above, in HARNESS-1's gate, with
/// every byte of the archive work on a worker).
///
/// The baseline is NOT an idle reading. HARNESS-1 measured that one directly
/// (open.responsive's header carries the numbers): an idle app's UI thread is
/// runnable for microseconds every probe period and the scheduler hands a
/// long-sleeping task the CPU almost immediately, so an idle floor
/// under-reports contention by ~2.3x. Contention bites a thread that BURNS its
/// slice — which is what this thread does while an archive runs: it renders
/// the editor, answers every poll, and takes the install slices.
///
/// So the baseline is a CONTROL: the same app, the same box, the same second,
/// the UI thread doing work of the same kind with NOTHING in flight —
/// editor.frame(1) is one document->engine sync plus one renderOneFrame, the
/// import's and export's neighbour on that thread. The assertion becomes what
/// this suite means: an archive must not block the thread more than running
/// the app does.
///
///     budget = min(max(kMaxGapMs, kControlFactor x control), kBudgetCeilingMs)
///
/// The factor is 2 and not 1 because the probe period is a floor inside both
/// numbers (quiet: 750 + 2 x (control - 250)).
///
/// THE CEILING bounds the degenerate case, and 2 000 ms is where it belongs
/// for a reason worth writing down, because the obvious tighter number is
/// wrong. The regression this suite exists to catch is the archiver's work
/// back on the UI thread — ProjectArchiver used to run the catalog sweep, the
/// CAS materialization, the zip/unzip and the catalog commit there — and what
/// that shows as is the OPERATION'S OWN WALL TIME, which runArchive prints on
/// every run. On this box, warm: import ~1 900-2 600 ms, export ~1 315 ms
/// quiet and ~5 000 ms under 40 spinners.
///
/// So the quiet-box regression signal (~1 315 ms) is SMALLER than the largest
/// budget this mechanism legitimately produced under load (control 940.3 ms
/// x 2 = 1 881 ms, measured here at load average 42-48). A ceiling between the
/// two would clip a loaded run's budget to catch a regression that cannot
/// happen at that load — because under the weather that produced that control,
/// the export took 4 959 ms and a regression would have shown five seconds,
/// not 1.3. The two constraints only conflict across weathers, never within
/// one.
///
/// The fixed 750 ms floor is what refuses a quiet-box regression (1 315 > 750,
/// and a quiet control here reads 260-345 ms, so the floor is the whole
/// budget); the ceiling's job is only to stop a pathological control from
/// buying multi-second permission under load, where the regression signal is
/// multiple seconds anyway. 2 000 ms does both: above every budget this box
/// produced with 40 spinners on 20 cores, and well under what the work coming
/// back to this thread would cost at that load.
static const double kControlFactor = 2.0;
static const double kBudgetCeilingMs = 2000.0;

/// THE CONTROL (see kControlFactor): the worst heartbeat gap over ~2 s of the
/// app rendering on its UI thread with no archive in flight.
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
static double budgetFor(double controlMs, const char *label)
{
    const double budget = qMin(qMax(kMaxGapMs, kControlFactor * controlMs), kBudgetCeilingMs);
    std::printf("info: [%s] UI-gap budget for this run: %.1f ms "
                "(fixed %.0f, control %.1f x %.1f, ceiling %.0f)\n",
                label, budget, kMaxGapMs, controlMs, kControlFactor, kBudgetCeilingMs);
    return budget;
}

/// Was this run's worst gap inside this run's budget? Measures the control
/// right after the operation it judges (same second, same weather), and on the
/// way to a red re-rolls THE CONTROL once.
///
/// The repeat re-rolls the control and not the operation, which is the one
/// place this differs from open.responsive: an archive run here is tens of
/// seconds and re-importing would leave a project the later sections count,
/// while a warm open is 220 ms and free to repeat. The control is the half
/// that is a single roll of a shared box's scheduler (HARNESS-1 measured
/// 486/375, 673/527 and 764/319 in three consecutive runs of the same build),
/// and the ceiling means a second roll can never rescue a real regression:
/// work back on the UI thread is a multi-second block at any control value.
static bool withinControlBudget(McpClient &mcp, double maxGap, const char *label)
{
    const double control = measureControlGap(mcp, label);
    if (maxGap > 0.0 && maxGap < budgetFor(control, label)) return true;
    std::printf("info: [%s] the worst gap (%.1f ms) exceeded this run's budget — "
                "re-rolling the control once\n", label, maxGap);
    const double control2 = measureControlGap(mcp, label);
    return maxGap > 0.0 && maxGap < budgetFor(qMax(control, control2), label);
}

struct RunStats { bool started = false, done = false; int polls = 0, ticks = 0;
                  double maxGap = 0.0, elapsedMs = 0.0; };

/// Start an async archive verb, poll project.archiveState() until idle, and
/// report what the UI thread did while it ran.
static RunStats runArchive(McpClient &mcp, const QString &startScript, const char *label)
{
    RunStats r;
    mcp.runScript(QStringLiteral("app.heartbeat(%1)").arg(int(kHeartbeatMs)));
    const QJsonObject start = mcp.runScript(startScript);
    r.started = start.value("ok").toBool() && start.value("result").toBool();
    if (!r.started)
        std::printf("info: [%s] start refused: %s\n", label,
                    QJsonDocument(start).toJson(QJsonDocument::Compact).constData());
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < kOpBudgetMs) {
        const QJsonObject state = mcp.runScript(QStringLiteral("project.archiveState()"));
        ++r.polls;
        if (state.value("ok").toBool() &&
            state.value("result").toString() == QLatin1String("idle")) { r.done = true; break; }
        QThread::msleep(50);
    }
    // Read BEFORE the stats round trip: an operation shorter than the probe
    // period could otherwise show > kHeartbeatMs elapsed with 0 ticks (the tick
    // fires while the stats are answered) and fail both halves (open.responsive
    // learned this the hard way).
    r.elapsedMs = double(timer.elapsed());
    const QJsonObject stats = mcp.runScript(QStringLiteral("app.heartbeatStats()"))
                                  .value("result").toObject();
    r.ticks = stats.value("ticks").toInt();
    r.maxGap = stats.value("maxGapMs").toDouble();
    std::printf("info: [%s] finished=%d after %lld ms, %d polls; ticks=%d maxGapMs=%.1f\n",
                label, int(r.done), static_cast<long long>(timer.elapsed()), r.polls,
                r.ticks, r.maxGap);
    return r;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // HERMETIC BY CONSTRUCTION. This suite's HOME lives in the build tree and
    // SURVIVES between runs, and a warm library quietly weakens what the run
    // can prove: a mesh bake is content-addressed, so a second run's import
    // finds the bake object already in the store and records no bake row
    // against the new project's asset rows at all — the "no bake travels"
    // assertion below would then pass on an empty set and prove nothing
    // (measured: bake:2 from a cold library, bake:0 from a warm one). Start
    // every run from an empty library. Guarded on the directory NAME so this
    // can never point at a real user's home.
    {
        const QString home = qEnvironmentVariable("HOME");
        if (QFileInfo(home).fileName() == QLatin1String("e2e-home-archive"))
            QDir(QDir(home).filePath(QStringLiteral(".local/share/Jahshaka"))).removeRecursively();
        else
            std::printf("info: HOME is not the suite's scratch home (%s) — not resetting\n",
                        home.toUtf8().constData());
    }

    // SEEDED AFTER THE WIPE, and the order is load-bearing (hygiene lane,
    // 2026-09-09). The seed used to land in `<binary dir>/jahsettings.ini`,
    // which is OUTSIDE this scratch home, so it survived the reset above by
    // accident. It lands inside the data root now — which is the point, that
    // is what stops this suite writing the developer's shared settings file —
    // and the reset above would therefore delete it, leaving the app to meet
    // the modal donate dialog at quit and hang until the exit budget ran out.
    // (Measured: "process terminated within the exit budget with an archive in
    // flight" failed for exactly this reason and nothing else.)
    seedSettings(QStringLiteral(JAHSHAKA_BINARY));

    // THE FIXTURE IS World Background (2026-09-07): it has to be a world with
    // MODEL assets, because the export half asserts that mesh bakes exist on
    // disk and never travel in the archive — and it has to be BIG, because the
    // heartbeat below samples every 250 ms and an operation shorter than that
    // records zero ticks (Matcaps imports in 231 ms and failed exactly there).
    // World Background is the largest archive the tree ships (10 MB, 22
    // objects); the Grand Showroom that replaced the old Showroom fixture is
    // built from primitives, imports instantly and bakes nothing.
    const QString sample =
        QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/scenes/World Background.zip");
    CHECK(QFileInfo::exists(sample), "World Background sample archive present");

    const QString outZip = QDir::current().filePath(QStringLiteral("archive-roundtrip.zip"));
    QFile::remove(outZip);

    QProcess jahshaka;
    QString token;
    const quint16 port = freePort();
    CHECK(spawn(jahshaka, port, &token), "app booted and printed the MCP token");
    if (token.isEmpty()) return 1;

    McpClient mcp;
    mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    mcp.token = token;
    mcp.clientName = QStringLiteral("archive-test");
    mcp.initialize();

    // ---- 0. THE BOOT CONTROL, which is also the warm-up -------------------
    //
    // AND IT IS THE BIGGEST NUMBER THIS SUITE PRINTS, by a factor of three.
    // Measured on this box, quiet, 2026-09-15: this control — the app doing
    // NOTHING but rendering its own empty editor, two seconds after boot, with
    // no archive anywhere near it — reads ~1 780 ms. That is the engine's cold
    // shader/PSO compile storm on the first frames of a process (Ogre's Hlms
    // builds a variant per material/pass permutation inside renderOneFrame,
    // and this pin persists none of it), and it is UI-thread work this lane
    // neither owns nor can slice.
    //
    // The import used to run INSIDE that window, because it was the first
    // thing the suite did after boot, and the storm was read as the
    // archiver's: 1 184 ms with every byte of the archive work on a worker,
    // against a 750 ms contract, on a quiet box. Warming first drops the same
    // import to 632 ms and its wall time from 4 512 to 2 598 ms (the UI thread
    // is no longer compiling shaders while the worker runs). HARNESS-1's
    // -j4 gate red on this suite was 1 734.6 ms — the same number as this
    // control, not a contention number.
    //
    // So the storm is paid HERE, once, before anything is measured, and what
    // follows measures the archive. The cold-process case has an owner already:
    // open.responsive asserts it against its own kColdCeilingMs, where the
    // subject is the engine's compilation rather than the archiver's file
    // work. This number is printed and not asserted for exactly that reason —
    // a suite should not red on a cost it does not own.
    measureControlGap(mcp, "boot");
    std::printf("info: (the number above is the ENGINE's boot compile storm, not the "
                "archiver's — see the comment at this call)\n");

    // ---- 1. the THREADED import, with the UI thread under measurement -----
    const RunStats imported =
        runArchive(mcp, QStringLiteral("project.importArchiveAsync('%1')").arg(sample), "import");
    CHECK(imported.started, "project.importArchiveAsync accepted");
    CHECK(imported.done, "the threaded import completed");
    CHECK(imported.polls >= 2, "the app answered requests WHILE the import was in flight");
    // OR the operation finished inside one probe interval and could not be
    // asked for a tick. That is a live case since the warm-up above: an import
    // that used to run 2.6-4.5 s (with the UI thread compiling shaders beside
    // it) now runs 420-470 ms against a 250 ms probe, so one tick is a normal
    // reading and zero is possible. The GAP still covers it either way —
    // maxGapMs is max(worst gap, time since the last tick or the start).
    CHECK(imported.ticks > 0 || imported.elapsedMs < kHeartbeatMs,
          "the UI thread kept ticking during the import (or the import finished "
          "inside the first heartbeat interval)");
    // The control is measured HERE, right after the import and before anything
    // else touches the app: same app, same box, same second, the UI thread
    // doing the same kind of work with nothing in flight (see kControlFactor).
    CHECK(withinControlBudget(mcp, imported.maxGap, "import"),
          "no UI-thread gap beyond the budget during the threaded import");

    const QJsonObject importResult = mcp.runScript(QStringLiteral("project.archiveResult()"))
                                         .value("result").toObject();
    std::printf("info: import result: %s\n",
                QJsonDocument(importResult).toJson(QJsonDocument::Compact).constData());
    const QString guid = importResult.value("guid").toString();
    CHECK(importResult.value("ok").toBool() && guid.length() > 10,
          "the import produced a project");
    const int importedAssets = importResult.value("assets").toInt();
    CHECK(importedAssets > 0, "the import ingested assets");
    if (guid.isEmpty()) { jahshaka.kill(); jahshaka.waitForFinished(5000); return 1; }

    // ---- 2. the THREADED export of that same world ------------------------
    CHECK(mcp.runScript(QStringLiteral("project.open('%1')").arg(guid)).value("ok").toBool(),
          "the imported world opens");
    const int nodes = mcp.runScript(QStringLiteral("scene.nodes().length"))
                          .value("result").toInt();
    CHECK(nodes > 1, "the imported world really loaded");

    // Make sure the world HAS mesh bakes before it is archived — otherwise the
    // "no bakes travel" assertion below would pass on an empty set and prove
    // nothing. Opening bakes lazily already; this is the belt.
    mcp.runScript(QStringLiteral("assets.bakeAll({dryRun: false})"));
    const QString storeRoot =
        mcp.runScript(QStringLiteral("assets.storeRoot()")).value("result").toString();
    int bakesOnDisk = 0;
    if (!storeRoot.isEmpty()) {
        QDirIterator it(storeRoot, QStringList() << QStringLiteral("*.jmb"), QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) { it.next(); ++bakesOnDisk; }
    }
    std::printf("info: %d mesh bake(s) in the store at %s\n", bakesOnDisk,
                storeRoot.toUtf8().constData());
    CHECK(bakesOnDisk > 0, "the world has mesh bakes on disk before the export");

    const RunStats exported =
        runArchive(mcp, QStringLiteral("project.exportArchiveAsync('%1')").arg(outZip), "export");
    CHECK(exported.started, "project.exportArchiveAsync accepted");
    CHECK(exported.done, "the threaded export completed");
    CHECK(exported.polls >= 2, "the app answered requests WHILE the export was in flight");
    CHECK(exported.ticks > 0 || exported.elapsedMs < kHeartbeatMs,
          "the UI thread kept ticking during the export (or the export finished "
          "inside the first heartbeat interval)");
    CHECK(withinControlBudget(mcp, exported.maxGap, "export"),
          "no UI-thread gap beyond the budget during the threaded export");

    const QJsonObject exportResult = mcp.runScript(QStringLiteral("project.archiveResult()"))
                                         .value("result").toObject();
    std::printf("info: export result: %s\n",
                QJsonDocument(exportResult).toJson(QJsonDocument::Compact).constData());
    CHECK(exportResult.value("ok").toBool(), "the export reported success");
    CHECK(QFileInfo::exists(outZip) && QFileInfo(outZip).size() > 1024,
          "the archive was written and is not empty");

    // ---- 2b. WHAT the archive ships (PUBLISH_AUDIT #2) --------------------
    // The mesh bake is derived data keyed on the BUILD that produced it: the
    // importing installation rejects a foreign bake as stale on sight and the
    // .jaf ingest cannot preserve the role anyway. The raw exporter has always
    // filtered role 'bake'; the project archiver silently did not, so every
    // generation of every bake travelled as dead megabytes. Read the manifest
    // the archiver actually wrote and hold that line.
    {
        QTemporaryDir peek;
        CHECK(peek.isValid(), "temp dir to read the archive manifest");
        QString zipError;
        CHECK(ZipHelper::extract(outZip, peek.path(), &zipError),
              "the exported archive extracts");
        QFile manifestFile(QDir(peek.path()).filePath(exportformat::manifestFileName()));
        CHECK(manifestFile.open(QIODevice::ReadOnly), "the archive carries a manifest.json");
        const QJsonObject manifest =
            QJsonDocument::fromJson(manifestFile.readAll()).object();
        const QJsonArray manifestAssets = manifest.value("assets").toArray();
        CHECK(!manifestAssets.isEmpty(), "the manifest lists assets");
        int bakeEntries = 0, jmbEntries = 0, sourceEntries = 0;
        for (const auto &av : manifestAssets) {
            for (const auto &fv : av.toObject().value("files").toArray()) {
                const QJsonObject f = fv.toObject();
                const QString role = f.value("role").toString();
                if (role == QLatin1String("bake")) ++bakeEntries;
                else if (role == QLatin1String("source")) ++sourceEntries;
                if (f.value("name").toString().endsWith(QLatin1String(".jmb"))) ++jmbEntries;
            }
        }
        std::printf("info: manifest files — source:%d bake:%d .jmb:%d\n",
                    sourceEntries, bakeEntries, jmbEntries);
        CHECK(sourceEntries > 0, "the manifest still ships the real sources");
        CHECK(bakeEntries == 0, "no role:'bake' entry travels in the archive");
        CHECK(jmbEntries == 0, "no .jmb bake file travels in the archive");
    }

    // ---- 3. CORRECTNESS: the synchronous verb and the threaded one agree ---
    // Re-importing what the threaded export wrote must produce the same world.
    // This is the half that is not allowed to move while the threading does.
    mcp.runScript(QStringLiteral("project.close()"));
    const QJsonObject roundTrip = mcp.runScript(
        QStringLiteral("project.importArchive('%1')").arg(outZip));
    std::printf("info: round-trip import: %s\n",
                QJsonDocument(roundTrip).toJson(QJsonDocument::Compact).constData());
    CHECK(roundTrip.value("ok").toBool(), "the exported archive imports back (synchronous verb)");
    const QJsonObject rt = roundTrip.value("result").toObject();
    CHECK(rt.value("assets").toInt() == importedAssets,
          "the round trip carries the same asset count");
    const QString rtGuid = rt.value("guid").toString();
    CHECK(mcp.runScript(QStringLiteral("project.open('%1')").arg(rtGuid)).value("ok").toBool(),
          "the round-tripped world opens");
    CHECK(mcp.runScript(QStringLiteral("scene.nodes().length")).value("result").toInt() == nodes,
          "the round-tripped world has the same node count");

    // ---- 4. CANCEL leaves no orphan ---------------------------------------
    mcp.runScript(QStringLiteral("project.close()"));
    const int projectsBefore =
        mcp.runScript(QStringLiteral("project.list().length")).value("result").toInt();
    CHECK(mcp.runScript(QStringLiteral("project.importArchiveAsync('%1')").arg(sample))
              .value("ok").toBool(), "a third import started, to be cancelled");
    const bool cancelAccepted =
        mcp.runScript(QStringLiteral("project.cancelArchive()")).value("result").toBool();
    std::printf("info: cancel accepted: %d\n", int(cancelAccepted));
    QElapsedTimer cancelTimer;
    cancelTimer.start();
    bool idle = false;
    while (cancelTimer.elapsed() < kOpBudgetMs) {
        if (mcp.runScript(QStringLiteral("project.archiveState()")).value("result").toString()
            == QLatin1String("idle")) { idle = true; break; }
        QThread::msleep(50);
    }
    CHECK(idle, "the cancelled import stopped");
    const int projectsAfter =
        mcp.runScript(QStringLiteral("project.list().length")).value("result").toInt();
    std::printf("info: projects %d -> %d after the cancelled import\n",
                projectsBefore, projectsAfter);
    // Either the cancel landed before the catalog phase (no row was ever made)
    // or it landed inside it and the archiver deleted the row it had started.
    // Both are "zero orphans"; a THIRD project is not.
    if (cancelAccepted)
        CHECK(projectsAfter == projectsBefore, "a cancelled import leaves no orphan project");
    else
        std::printf("info: the import finished before the cancel could be delivered\n");

    // ---- 5. quitting with an archive IN FLIGHT ----------------------------
    // shutdownArchives() is step 2 of the shutdown order; this is the
    // import.shutdown zombie applied to the archiver.
    CHECK(mcp.runScript(QStringLiteral("project.importArchiveAsync('%1')").arg(sample))
              .value("ok").toBool(), "a fourth import started, to be quit through");
    // No project is open at this point (the cancel case closed it), which used
    // to mean a modal "Unsaved Changes" box at quit — the close-path zombie
    // this lane fixed in MainWindow::closeEvent. Quitting from here is
    // therefore part of the assertion, not an accident.
    mcp.quit();
    QElapsedTimer exitTimer;
    exitTimer.start();
    const bool exited = jahshaka.waitForFinished(kExitBudgetMs);
    std::printf("info: exit after %lld ms\n", static_cast<long long>(exitTimer.elapsed()));
    CHECK(exited, "process terminated within the exit budget with an archive in flight");
    if (!exited) { jahshaka.kill(); jahshaka.waitForFinished(5000); }
    else {
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

    QFile::remove(outZip);
    std::printf(failures ? "FAILED: %d check(s)\n" : "ALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
