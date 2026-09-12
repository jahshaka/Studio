// avatar.responsive — the Avatar module must not freeze the app (AV1, owner
// 2026-09-13), measured the way archive.responsive measures an archive.
//
// The owner imported a rigged FBX into the module and the app looked crashed:
// the session log reads `import: 'Jennifer.fbx' -> … (6 objects, 9332 ms)` on
// the UI thread with the renderer idle beside it, and switching characters
// cost another 1541 ms with nothing on screen. Both halves now run off the UI
// thread — the import through the pipeline's own ImportBatchRunner (the Assets
// page's threaded import, not a second one), the switch's model parse on a
// QtConcurrent worker — behind `{async: true}` on the two verbs the module
// calls.
//
// Sections:
//   1. the ASYNC IMPORT: the verb returns at once, the UI thread keeps
//      ticking, and the app answers requests while it runs.
//   2. SAME ANSWER as the synchronous path: the avatar the threaded import
//      produces has the same clips, rig and model metadata as the one the
//      synchronous verb produces from the same file.
//   3. CANCEL: a cancelled import leaves the library exactly as it was and
//      opens nothing.
//   4. the ASYNC SWITCH: `avatar.open({async:true})` returns with the
//      definition already open (the clip column is right immediately) and the
//      character follows without a UI-thread gap.
//   5. EACH AVATAR KEEPS ITS OWN ANIMATIONS: a clip added to the open avatar
//      survives switching away and back AND a restart of the app, with its
//      per-clip settings and the default-clip pointer.
//   6. quitting with an avatar import IN FLIGHT still terminates (the
//      import.shutdown zombie class, applied to the module's own worker).
//
// "Responsive" is measured, not asserted by eye: the app's heartbeat probe
// (app.heartbeat / app.heartbeatStats) reports the WORST gap between ticks on
// the UI thread, and a blocked UI thread cannot tick. Every poll below is an
// HTTP request the app can only answer between slices, so the polls are a
// second, independent proof.
#include "../support/seedsettings.h"
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
#include <QTcpServer>
#include <QThread>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

/// THE BUDGET. The largest block the avatar paths may still put on the UI
/// thread is one import COMMIT slice (the CAS ingest of one already-hashed
/// file plus its catalog rows) or the graft of a parsed character into the
/// preview document — tens of milliseconds each on this fixture. 750 ms is the
/// same number archive.responsive uses and for the same reason: an order of
/// magnitude of headroom over the real work, an order of magnitude below the
/// ~5 s of unanswered pings that makes the desktop offer to force-quit. The
/// pre-fix measurement on the owner's file was 12 438 ms (lane AV1, on this
/// box, ASan Debug).
static const double kMaxGapMs = 750.0;
static const int kOpBudgetMs = 300000;
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
                                { "clientInfo", QJsonObject{ { "name", "avatar-test" }, { "version", "0" } } } } } });
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

    /// The script's `result` as a value / string / int, with the ok flag
    /// printed when a call failed (a refusal is a test finding, not silence).
    QJsonValue value(const QString &script)
    {
        const QJsonObject reply = runScript(script);
        if (!reply.value("ok").toBool(true))
            std::printf("info: script refused: %s -> %s\n", script.toUtf8().constData(),
                        QJsonDocument(reply).toJson(QJsonDocument::Compact).constData());
        return reply.value("result");
    }
    QString string(const QString &script) { return value(script).toString(); }
    int integer(const QString &script) { return value(script).toInt(); }
};

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
    while (timer.elapsed() < 180000 && jahshaka.state() == QProcess::Running) {
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

struct JobStats { bool done = false; int polls = 0, ticks = 0; double maxGap = 0.0; QJsonObject last; };

/// Poll avatar.progress() until the module's background job is idle, and
/// report what the UI thread did while it ran.
static JobStats waitForJob(McpClient &mcp, const char *label)
{
    JobStats r;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < kOpBudgetMs) {
        const QJsonObject progress = mcp.runScript(QStringLiteral("avatar.progress()"))
                                         .value("result").toObject();
        ++r.polls;
        r.last = progress;
        if (!progress.value("running").toBool()) { r.done = true; break; }
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

static QStringList clipNames(McpClient &mcp, const QString &expression)
{
    const QJsonArray clips = mcp.value(expression).toArray();
    QStringList names;
    for (const QJsonValue &v : clips) names.append(v.toString());
    return names;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // A FRESH library per run: this suite asserts ABSOLUTE row counts around a
    // cancel ("the library is exactly as it was"), and a home that persists
    // across gates accumulates a second copy of everything. Guarded on the
    // directory NAME so it can never point at a real user's home.
    {
        const QString home = qEnvironmentVariable("HOME");
        if (QFileInfo(home).fileName() == QLatin1String("e2e-home-avatar-async"))
            QDir(QDir(home).filePath(QStringLiteral(".local/share/Jahshaka"))).removeRecursively();
        else
            std::printf("info: HOME is not the suite's scratch home (%s) — not resetting\n",
                        home.toUtf8().constData());
    }
    // AFTER the wipe (the archive.responsive ordering lesson): unseeded, the
    // modal donate dialog meets app.quit() and the process never exits.
    testsupport::seedSettingsForSpawnedApp(QStringLiteral(JAHSHAKA_BINARY));

    // The fixtures are the pair tests/avatar generates from the shipped
    // Skeletal Animation sample: a rigged character and an animation-only clip
    // file that shares its joint names.
    const QString rig = QStringLiteral(JAHSHAKA_MANNEQUIN_DAE);
    const QString walk = QStringLiteral(JAHSHAKA_BETA_WALK_DAE);
    CHECK(QFileInfo::exists(rig), "the rigged fixture is present");
    CHECK(QFileInfo::exists(walk), "the animation-only fixture is present");
    if (!QFileInfo::exists(rig)) return 1;

    QProcess jahshaka;
    QString token;
    const quint16 port = freePort();
    CHECK(spawn(jahshaka, port, &token), "app booted and printed the MCP token");
    if (token.isEmpty()) return 1;

    McpClient mcp;
    mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    mcp.token = token;
    mcp.initialize();
    mcp.runScript(QStringLiteral("project.create('avatar async')"));

    // ---- 1. the ASYNC IMPORT, with the UI thread under measurement --------
    mcp.runScript(QStringLiteral("app.heartbeat(100)"));
    QElapsedTimer verbTimer;
    verbTimer.start();
    const QJsonObject started =
        mcp.runScript(QStringLiteral("avatar.importAvatar('%1', {async: true})").arg(rig))
            .value("result").toObject();
    const qint64 verbMs = verbTimer.elapsed();
    std::printf("info: the verb returned in %lld ms: %s\n", static_cast<long long>(verbMs),
                QJsonDocument(started).toJson(QJsonDocument::Compact).constData());
    CHECK(started.value("started").toBool(), "avatar.importAvatar({async:true}) started a job");
    CHECK(verbMs < 1000, "... and RETURNED immediately instead of importing inline");

    const JobStats imported = waitForJob(mcp, "import");
    CHECK(imported.done, "the threaded avatar import completed");
    CHECK(imported.polls >= 2, "the app answered requests WHILE the import was in flight");
    CHECK(imported.ticks > 0, "the UI thread kept ticking during the import");
    CHECK(imported.maxGap > 0.0 && imported.maxGap < kMaxGapMs,
          "no UI-thread gap beyond the budget during the threaded import");

    const QJsonObject result = imported.last.value("result").toObject();
    const QString asyncAvatar = result.value("avatar").toString();
    const QString asyncModel = result.value("asset").toString();
    CHECK(asyncAvatar.length() > 10 && asyncModel.length() > 10,
          "the job produced a model asset AND an avatar asset");
    CHECK(mcp.string(QStringLiteral("avatar.asset().guid")) == asyncAvatar,
          "... and left the module EDITING it (D7-A), as the synchronous verb does");
    const int previewBones = mcp.integer(QStringLiteral("avatar.preview().bones"));
    std::printf("info: the preview loaded %d bones\n", previewBones);
    CHECK(previewBones > 10, "the preview really loaded the character");

    // ---- 2. the SAME ANSWER as the synchronous path -----------------------
    const QStringList asyncClips =
        clipNames(mcp, QStringLiteral("avatar.asset().definition.clips.map(function(c){return c.name})"));
    const int asyncBones = mcp.integer(QStringLiteral("assets.metadata('%1').bones").arg(asyncModel));
    const QString asyncRig = mcp.string(QStringLiteral("assets.metadata('%1').rigId").arg(asyncModel));
    const int asyncTextures = mcp.integer(QStringLiteral("assets.metadata('%1').textures").arg(asyncModel));

    const QJsonObject syncResult =
        mcp.runScript(QStringLiteral("avatar.importAvatar('%1')").arg(rig))
            .value("result").toObject();
    const QString syncAvatar = syncResult.value("avatar").toString();
    const QString syncModel = syncResult.value("asset").toString();
    CHECK(syncAvatar.length() > 10, "the synchronous verb still imports the same file");
    const QStringList syncClips =
        clipNames(mcp, QStringLiteral("avatar.asset().definition.clips.map(function(c){return c.name})"));
    std::printf("info: clips async=[%s] sync=[%s]\n",
                asyncClips.join(QStringLiteral(", ")).toUtf8().constData(),
                syncClips.join(QStringLiteral(", ")).toUtf8().constData());
    CHECK(asyncClips == syncClips, "the threaded import produced the same clip list");
    // The WHOLE definition, minus the identity a second import necessarily
    // changes (the model asset's guid): clips with their options, the default
    // clip, the rig block. This is what "identical to the synchronous path's
    // result" means.
    const QString shape =
        QStringLiteral("(function(){var d = avatar.asset().definition;"
                       " return JSON.stringify({clips: d.clips.map(function(c){"
                       "   return {name: c.name, rawName: c.rawName, looping: c.looping,"
                       "           rootMotion: c.rootMotion}}),"
                       " defaultClip: d.defaultClip, rig: d.rig, name: avatar.asset().name});})()");
    const QString syncShape = mcp.string(shape);
    mcp.runScript(QStringLiteral("avatar.open('%1')").arg(asyncAvatar));
    const QString asyncShape = mcp.string(shape);
    std::printf("info: definition shape: %s\n", asyncShape.toUtf8().constData());
    CHECK(!asyncShape.isEmpty() && asyncShape == syncShape,
          "... and the same definition (clips, options, default, rig, name)");
    mcp.runScript(QStringLiteral("avatar.open('%1')").arg(syncAvatar));
    CHECK(asyncBones == mcp.integer(QStringLiteral("assets.metadata('%1').bones").arg(syncModel))
              && asyncBones > 10,
          "... the same rig size");
    CHECK(asyncRig == mcp.string(QStringLiteral("assets.metadata('%1').rigId").arg(syncModel))
              && asyncRig.length() == 40,
          "... the same rig id");
    CHECK(asyncTextures == mcp.integer(QStringLiteral("assets.metadata('%1').textures").arg(syncModel)),
          "... and the same texture set");

    // ---- 3. CANCEL leaves the library exactly as it was --------------------
    // (The heartbeat restarts here: section 2 ran the SYNCHRONOUS verb, which
    // blocks the UI thread BY DESIGN — that is what the script/headless route
    // is — and its gap would otherwise be read as this section's.)
    mcp.runScript(QStringLiteral("app.heartbeat(0)"));
    mcp.runScript(QStringLiteral("app.heartbeat(100)"));
    const int assetsBefore = mcp.integer(QStringLiteral("assets.list().length"));
    // Start AND cancel in one script run: the worker cannot have passed its
    // first progress callback yet, so the cancel is delivered inside prepare
    // and nothing is ever committed.
    const QJsonObject cancelRun = mcp.runScript(
        QStringLiteral("var s = avatar.importAvatar('%1', {async: true}); "
                       "[s.started === true, avatar.cancelImport()]").arg(rig));
    const QJsonArray cancelFlags = cancelRun.value("result").toArray();
    CHECK(cancelFlags.size() == 2 && cancelFlags.at(0).toBool(), "a fourth import started, to be cancelled");
    CHECK(cancelFlags.size() == 2 && cancelFlags.at(1).toBool(), "avatar.cancelImport() accepted it");
    const JobStats cancelled = waitForJob(mcp, "cancel");
    CHECK(cancelled.done, "the cancelled import stopped");
    CHECK(cancelled.last.value("cancelled").toBool(), "... and reports itself cancelled");
    const int assetsAfter = mcp.integer(QStringLiteral("assets.list().length"));
    std::printf("info: library rows %d -> %d across the cancelled import\n",
                assetsBefore, assetsAfter);
    CHECK(assetsAfter == assetsBefore, "a cancelled avatar import leaves the library unchanged");
    CHECK(mcp.string(QStringLiteral("avatar.asset().guid")) == syncAvatar,
          "... and does not disturb the avatar that was open");

    // ---- 4. the ASYNC SWITCH ----------------------------------------------
    mcp.runScript(QStringLiteral("app.heartbeat(0)"));
    mcp.runScript(QStringLiteral("app.heartbeat(100)"));
    verbTimer.restart();
    mcp.runScript(QStringLiteral("avatar.open('%1', {async: true})").arg(asyncAvatar));
    const qint64 openMs = verbTimer.elapsed();
    // The DEFINITION is open before the verb returns — that is what makes the
    // module's right column right immediately while the character loads.
    CHECK(mcp.string(QStringLiteral("avatar.asset().guid")) == asyncAvatar,
          "avatar.open({async:true}) has the definition open when it returns");
    std::printf("info: avatar.open({async:true}) returned in %lld ms\n",
                static_cast<long long>(openMs));
    CHECK(openMs < 1000, "... and returned immediately instead of parsing inline");
    const JobStats switched = waitForJob(mcp, "switch");
    CHECK(switched.done, "the threaded avatar switch completed");
    CHECK(switched.maxGap > 0.0 && switched.maxGap < kMaxGapMs,
          "no UI-thread gap beyond the budget during the switch");
    CHECK(mcp.integer(QStringLiteral("avatar.preview().bones")) > 10,
          "the switched-to character is loaded in the preview");

    // ---- 5. EACH AVATAR KEEPS ITS OWN ANIMATIONS --------------------------
    const QJsonObject loaded =
        mcp.runScript(QStringLiteral("avatar.loadAnimation('%1')").arg(walk))
            .value("result").toObject();
    CHECK(loaded.value("added").toInt() > 0, "a clip file loads onto the open avatar");
    const QString clip = loaded.value("clip").toString();
    mcp.runScript(QStringLiteral("avatar.setClipOptions('%1', {looping: false, rootMotion: true})")
                      .arg(clip));
    mcp.runScript(QStringLiteral("avatar.setDefaultClip('%1')").arg(clip));
    CHECK(!mcp.value(QStringLiteral("avatar.asset().dirty")).toBool(),
          "the clip was WRITTEN THROUGH into the avatar's stored definition, not left unsaved");

    // switch away and back
    mcp.runScript(QStringLiteral("avatar.open('%1')").arg(syncAvatar));
    CHECK(!clipNames(mcp, QStringLiteral("avatar.asset().definition.clips.map(function(c){return c.name})"))
               .contains(clip),
          "the OTHER avatar does not have the clip (each avatar keeps its own)");
    mcp.runScript(QStringLiteral("avatar.open('%1')").arg(asyncAvatar));
    CHECK(clipNames(mcp, QStringLiteral("avatar.asset().definition.clips.map(function(c){return c.name})"))
              .contains(clip),
          "the clip is still there after switching away and back");

    // ---- 6. quitting with an import IN FLIGHT -----------------------------
    mcp.runScript(QStringLiteral("avatar.importAvatar('%1', {async: true})").arg(rig));
    mcp.runScript(QStringLiteral("app.quit()"));
    QElapsedTimer exitTimer;
    exitTimer.start();
    const bool exited = jahshaka.waitForFinished(kExitBudgetMs);
    std::printf("info: exit after %lld ms\n", static_cast<long long>(exitTimer.elapsed()));
    CHECK(exited, "process terminated within the exit budget with an avatar import in flight");
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

    // ---- 5b. ... AND SURVIVES A RESTART -----------------------------------
    // The same HOME, a second process: what the module stored is what a user
    // finds when they come back tomorrow.
    QProcess second;
    QString secondToken;
    const quint16 secondPort = freePort();
    CHECK(spawn(second, secondPort, &secondToken), "the app booted a second time on the same library");
    if (!secondToken.isEmpty()) {
        McpClient restarted;
        restarted.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(secondPort));
        restarted.token = secondToken;
        restarted.initialize();
        const QJsonObject reopened =
            restarted.runScript(QStringLiteral("avatar.open('%1')").arg(asyncAvatar))
                .value("result").toObject();
        CHECK(reopened.value("guid").toString() == asyncAvatar,
              "the avatar opens again in the new process");
        const QStringList afterRestart =
            clipNames(restarted,
                      QStringLiteral("avatar.asset().definition.clips.map(function(c){return c.name})"));
        std::printf("info: clips after the restart: [%s]\n",
                    afterRestart.join(QStringLiteral(", ")).toUtf8().constData());
        CHECK(afterRestart.contains(clip), "the clip added before the quit is STILL THERE");
        const QJsonObject entry =
            restarted.runScript(QStringLiteral("avatar.asset().definition.clips.filter("
                                               "function(c){return c.name === '%1'})[0]").arg(clip))
                .value("result").toObject();
        CHECK(entry.value("looping").toBool() == false && entry.value("rootMotion").toBool() == true,
              "... with the per-clip settings it was given");
        CHECK(restarted.string(QStringLiteral("avatar.asset().definition.defaultClip")) == clip,
              "... and it is still the default clip");
        restarted.runScript(QStringLiteral("app.quit()"));
        if (!second.waitForFinished(kExitBudgetMs)) { second.kill(); second.waitForFinished(5000); }
    }

    std::printf(failures ? "FAILED: %d check(s)\n" : "ALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
