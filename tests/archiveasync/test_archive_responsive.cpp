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
#include "../support/seedsettings.h"
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
static const int kOpBudgetMs = 180000;
static const int kExitBudgetMs = 30000;

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
                                { "clientInfo", QJsonObject{ { "name", "archive-test" }, { "version", "0" } } } } } });
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

/// Same reason as import.shutdown: keep the close path prompt-free. In a
/// QT_DEBUG build jahsettings.ini lives beside the BINARY, which a scratch
/// HOME does not isolate.
static void seedSettings(const QString &binary)
{
    testsupport::seedSettingsForSpawnedApp(binary);
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

struct RunStats { bool started = false, done = false; int polls = 0, ticks = 0; double maxGap = 0.0; };

/// Start an async archive verb, poll project.archiveState() until idle, and
/// report what the UI thread did while it ran.
static RunStats runArchive(McpClient &mcp, const QString &startScript, const char *label)
{
    RunStats r;
    mcp.runScript(QStringLiteral("app.heartbeat(250)"));
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
    mcp.initialize();

    // ---- 1. the THREADED import, with the UI thread under measurement -----
    const RunStats imported =
        runArchive(mcp, QStringLiteral("project.importArchiveAsync('%1')").arg(sample), "import");
    CHECK(imported.started, "project.importArchiveAsync accepted");
    CHECK(imported.done, "the threaded import completed");
    CHECK(imported.polls >= 2, "the app answered requests WHILE the import was in flight");
    CHECK(imported.ticks > 0, "the UI thread kept ticking during the import");
    CHECK(imported.maxGap > 0.0 && imported.maxGap < kMaxGapMs,
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
    CHECK(exported.ticks > 0, "the UI thread kept ticking during the export");
    CHECK(exported.maxGap > 0.0 && exported.maxGap < kMaxGapMs,
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
    mcp.runScript(QStringLiteral("app.quit()"));
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
