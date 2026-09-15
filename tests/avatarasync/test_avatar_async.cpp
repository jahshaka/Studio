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
//   7. THE SWITCH WINDOW IS NOT EDITABLE (lead review, AV1 round 2): between an
//      async open's return and its parse landing the definition is the new
//      avatar's while the preview is still the old character, so every
//      definition edit REFUSES and the incoming avatar's stored file is
//      untouched — the data-corruption class this round fixes.
//   8. A SYNCHRONOUS open during an async one wins: the superseded parse
//      applies nothing (it used to leave the preview on the old character
//      under the new one's definition), and the same for async-during-async.
//
// "Responsive" is measured, not asserted by eye: the app's heartbeat probe
// (app.heartbeat / app.heartbeatStats) reports the WORST gap between ticks on
// the UI thread, and a blocked UI thread cannot tick. Every poll below is an
// HTTP request the app can only answer between slices, so the polls are a
// second, independent proof.
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
#include <QTcpServer>
#include <QRegularExpression>
#include <QThread>
#include <cstdio>

static int failures = 0;
using namespace mcpharness;

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
///
/// AND A CONTROL, because this is a MEASUREMENT and the box is shared: at -j4
/// beside a sibling lane's gate the same run read 1 338 ms — and 853.5 ms in
/// HARNESS-1's gate — with the import fully threaded. The UI thread was not
/// blocked, it was not SCHEDULED.
///
/// THE BASELINE IS NOT AN IDLE READING, and this suite used to take one (an
/// idle noise floor, x3). HARNESS-1 measured that mechanism directly on this
/// box with 40 spin loops on 20 cores: the idle floor read 238-303 ms while a
/// warm open in the same process read 552, 701 and 732 ms — it under-reports
/// contention by ~2.3x, and the reason is physics, not tuning. An idle app's
/// UI thread is runnable for microseconds every probe period and the scheduler
/// hands a long-sleeping task the CPU almost immediately; the engine even
/// SKIPS still frames, so at rest the thread barely competes at all.
/// Contention only bites a thread that BURNS its slice — which is what this
/// thread does while an avatar imports: it renders the editor, answers every
/// poll, and takes the commit slices.
///
/// So the baseline is a CONTROL: the same app, the same box, the same second,
/// the UI thread doing work OF THE SAME KIND with nothing in flight —
/// editor.frame(1) is one document->engine sync plus one renderOneFrame. The
/// assertion becomes what this suite means: an avatar import must not block
/// the thread more than running the app does.
///
///     budget = min(max(kMaxGapMs, kControlFactor x control), kBudgetCeilingMs)
///
/// The factor is 2 and not 1 because the probe period is a floor inside both
/// numbers (a 100 ms probe never fires early).
///
/// THE CEILING bounds the degenerate case: no pathological control may buy
/// this suite permission to accept a multi-second block. 2 000 ms is 6.2x
/// under the 12 438 ms the defect actually cost (above), and above every
/// budget this box produced with 40 spinners on 20 cores at load average
/// 42-48 (worst control 473.5 ms, budget 947.1; worst job gap 284.3 ms). It is
/// also under the wall time of the jobs themselves — an async import here runs
/// 1.0 s quiet and 2.7-3.9 s loaded — so the work coming back to this thread
/// cannot slip under it.
static const double kMaxGapMs = 750.0;
static const double kControlFactor = 2.0;
static const double kBudgetCeilingMs = 2000.0;
/// The heartbeat probe's interval (app.heartbeat(kHeartbeatMs)).
static const double kHeartbeatMs = 100.0;
static const int kOpBudgetMs = 300000;
static const int kExitBudgetMs = 30000;

/// THE APP'S OWN OUTPUT, kept. spawn() merges the child's stdout and stderr
/// and nothing read them after the boot token, so the app's
/// `[heartbeat] UI thread blocked N ms (stage: ...)` lines — and the
/// watchdog's backtrace of the blocked thread past two seconds — were thrown
/// away on every red this suite ever had. Drained on every poll below,
/// printed by printUiThreadEvidence when a gap misses its budget.
static QProcess *gApp = nullptr;
static QByteArray gAppLog;
static void drainApp() { if (gApp) gAppLog += gApp->readAll(); }

/// How many shaders the ENGINE has compiled in this process so far
/// (app.shaderCache().compiledThisRun).
///
/// Read on both sides of every measured window, because Ogre builds a shader
/// variant per material/pass permutation INSIDE renderOneFrame: a compile
/// inside a measured window is a UI-thread block this suite's subject does not
/// own, and this delta is the only way to tell one from the other. It is
/// PRINTED, never asserted on, and it is not an excuse — the gap is still
/// asserted whatever the delta says. What it buys is that the next red carries
/// its own verdict.
///
/// Measured on this box 2026-09-15 (lane RESPONSIVE-3), quiet and under 40
/// spinners, warm and with the binary's pages evicted: an avatar import
/// compiles ZERO (91 -> 91 every time, and a throwaway warm-up import changes
/// neither the count nor the gap). archive.responsive's import is the opposite
/// case: the sample's own permutations compile inside its window and block the
/// UI thread ~2.3 s doing it.
static int shadersCompiled(McpClient &mcp)
{
    return mcp.runScript(QStringLiteral("app.shaderCache().compiledThisRun"))
        .value("result").toInt();
}

struct JobStats { bool done = false; int polls = 0, ticks = 0; double maxGap = 0.0;
                  int logMark = 0; QJsonObject last; };

/// Poll avatar.progress() until the module's background job is idle, and
/// report what the UI thread did while it ran.
static JobStats waitForJob(McpClient &mcp, const char *label, int compiledBefore = -1)
{
    JobStats r;
    drainApp();
    r.logMark = gAppLog.size();   // the window's start in the app's own output
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < kOpBudgetMs) {
        const QJsonObject progress = mcp.runScript(QStringLiteral("avatar.progress()"))
                                         .value("result").toObject();
        ++r.polls;
        r.last = progress;
        drainApp();
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
    if (compiledBefore >= 0) {
        const int compiledAfter = shadersCompiled(mcp);
        std::printf("info: [%s] the ENGINE compiled %d shader(s) inside this window "
                    "(%d -> %d) — see shadersCompiled()\n",
                    label, compiledAfter - compiledBefore, compiledBefore, compiledAfter);
    }
    return r;
}

/// THE CONTROL (see kControlFactor): the worst heartbeat gap over ~2 s of the
/// app rendering on its UI thread with no avatar job in flight.
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

/// THE WARM-UP STOPS WHEN THE APP HAS SETTLED, NOT WHEN A CLOCK SAYS SO.
///
/// The first frames of a PROCESS pay the engine's shader/PSO compile storm
/// inside renderOneFrame — captured, not guessed: the watchdog's backtrace of
/// the blocked UI thread during a boot stall on this box reads
/// `glslang ... yyparse` in one sample and `libnvidia-glcore.so.595.84` in the
/// next (lane RESPONSIVE-3, spikes/responsive-3/). It is UI-thread work this
/// lane neither owns nor can slice, and this suite wipes its data root — which
/// IS the engine's shader + pipeline cache — at the top of every run, so EVERY
/// run pays it in full (`pipelineCacheLoaded:false, reason:"absent"`, 91
/// shaders compiled, every time).
///
/// A FIXED two-second warm-up is therefore a guess about how long that takes,
/// and the guess is wrong exactly when the box is busy: the storm does not
/// scale with the clock, it scales with the CPU the app gets. When it outlives
/// the window its TAIL lands in the first measured window and is read as the
/// subject's block. So the warm-up repeats its control until the app stops
/// getting better at it:
///
///   * stop when the round compiled NOTHING and either the round is already at
///     the probe period's floor (a quiet box: one round, exactly what this
///     suite did before — measured 111-121 ms here) or the round is no better
///     than the one before it by more than a fifth (a busy box: the storm is
///     over and what remains is the weather, which more rounds cannot buy);
///   * at most kWarmUpRounds, because a control costs 2 s and a warm-up that
///     never stops is a hang.
static const int kWarmUpRounds = 4;
static void warmUpUntilSettled(McpClient &mcp)
{
    double previous = 0.0;
    for (int round = 1; round <= kWarmUpRounds; ++round) {
        const int before = shadersCompiled(mcp);
        const double gap = measureControlGap(mcp, "boot");
        const int compiled = shadersCompiled(mcp) - before;
        const bool atFloor = gap <= 1.5 * kHeartbeatMs;
        const bool noBetter = round > 1 && gap >= 0.8 * previous;
        const bool settled = compiled == 0 && (atFloor || noBetter);
        std::printf("info: [boot] warm-up round %d: worst gap %.1f ms, %d shader(s) "
                    "compiled in it%s\n", round, gap, compiled, settled ? " — settled" : "");
        if (settled) return;
        previous = gap;
    }
    std::printf("info: [boot] warm-up gave up after %d rounds — the engine was still "
                "compiling, or the box never settled; the numbers below carry that\n",
                kWarmUpRounds);
}

/// Was this run's worst gap inside this run's budget? The control is measured
/// RIGHT AFTER the job it judges — same second, same weather — and on the way
/// to a red the CONTROL is re-rolled once.
///
/// The repeat re-rolls the control and not the job, which is where this differs
/// from open.responsive: an avatar import here is tens of seconds and a repeat
/// would leave library rows the later sections count, while a warm open is
/// 220 ms and free to repeat. The control is the half that is a single roll of
/// a shared box's scheduler (HARNESS-1 measured 486/375, 673/527 and 764/319 in
/// three consecutive runs of one build), and the ceiling means a second roll can
/// never rescue a regression: work back on this thread is a multi-second block
/// at any control value.
static bool withinControlBudget(McpClient &mcp, double maxGap, const char *label,
                                int logMark = 0)
{
    const double control = measureControlGap(mcp, label);
    if (maxGap > 0.0 && maxGap < budgetFor(control, label)) return true;
    std::printf("info: [%s] the worst gap (%.1f ms) exceeded this run's budget — "
                "re-rolling the control once\n", label, maxGap);
    const double control2 = measureControlGap(mcp, label);
    const bool ok = maxGap > 0.0 && maxGap < budgetFor(qMax(control, control2), label);
    if (!ok) {
        drainApp();
        printUiThreadEvidence(gAppLog, label, logMark);
    }
    return ok;
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
    CHECK(spawn(jahshaka, port, &token, QStringList(), 180000), "app booted and printed the MCP token");
    if (token.isEmpty()) return 1;
    // From here the app's own output is KEPT (see drainApp): a red prints the
    // app's account of its UI thread instead of discarding it with the pipe.
    gApp = &jahshaka;

    McpClient mcp;
    mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    mcp.token = token;
    mcp.clientName = QStringLiteral("avatar-test");
    // The avatar suite's own budget is 900 s and its verbs are the slowest in the
    // tree (a synchronous import of the owner's rig), so it raises the harness's
    // per-request budget rather than risking a real call being cut short.
    mcp.transferTimeoutMs = 300000;
    mcp.initialize();
    mcp.runScript(QStringLiteral("project.create('avatar async')"));

    // ---- 0. THE BOOT CONTROL, which is also the warm-up -------------------
    // The first frames of a PROCESS pay the engine's shader/PSO compile storm
    // inside renderOneFrame — UI-thread work this lane neither owns nor can
    // slice, and the cost is real: archive.responsive measured ~1 780 ms of it
    // on a quiet box and spent months reading it as its own subject (see the
    // boot control there). This suite wipes its data root — which IS the
    // engine's shader + pipeline cache — at the top of every run, so EVERY run
    // is cold and pays the storm; the only question is whether a measurement
    // is taken inside it. Pay it here, before anything is measured, and pay it
    // UNTIL IT IS PAID rather than for two seconds (see warmUpUntilSettled: a
    // fixed window is a guess about a cost that scales with the CPU the app
    // gets, and its tail lands in the first measured window when the guess is
    // short). Printed, never asserted: open.responsive owns the cold case.
    warmUpUntilSettled(mcp);

    // ---- 1. the ASYNC IMPORT, with the UI thread under measurement --------
    // (The budget is no longer computed here: the control that sets it is
    // measured AFTER each job, in the same weather the job ran in — see
    // kControlFactor.)
    mcp.runScript(QStringLiteral("app.heartbeat(0)"));
    mcp.runScript(QStringLiteral("app.heartbeat(%1)").arg(int(kHeartbeatMs)));
    const int compiledBeforeImport = shadersCompiled(mcp);
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

    const JobStats imported = waitForJob(mcp, "import", compiledBeforeImport);
    CHECK(imported.done, "the threaded avatar import completed");
    CHECK(imported.polls >= 2, "the app answered requests WHILE the import was in flight");
    CHECK(imported.ticks > 0, "the UI thread kept ticking during the import");
    CHECK(withinControlBudget(mcp, imported.maxGap, "import", imported.logMark),
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
    mcp.runScript(QStringLiteral("app.heartbeat(%1)").arg(int(kHeartbeatMs)));
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
    mcp.runScript(QStringLiteral("app.heartbeat(%1)").arg(int(kHeartbeatMs)));
    const int compiledBeforeSwitch = shadersCompiled(mcp);
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
    const JobStats switched = waitForJob(mcp, "switch", compiledBeforeSwitch);
    CHECK(switched.done, "the threaded avatar switch completed");
    CHECK(withinControlBudget(mcp, switched.maxGap, "switch", switched.logMark),
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
    // COALESCED, not immediate (AV1 round 2): the edit arms a short single-shot
    // so three clip toggles are ONE CAS publish and one instance refresh. What
    // the user is promised is that it lands without them pressing anything.
    bool settled = false;
    QElapsedTimer settleTimer;
    settleTimer.start();
    while (settleTimer.elapsed() < 10000) {
        const QJsonObject open = mcp.runScript(QStringLiteral("avatar.asset()"))
                                     .value("result").toObject();
        if (!open.value("dirty").toBool() && !open.value("pending").toBool()) { settled = true; break; }
        QThread::msleep(50);
    }
    std::printf("info: the coalesced write settled after %lld ms\n",
                static_cast<long long>(settleTimer.elapsed()));
    CHECK(settled,
          "the clip WRITES ITSELF THROUGH into the avatar's stored definition, with nobody "
          "pressing Save");

    // switch away and back
    mcp.runScript(QStringLiteral("avatar.open('%1')").arg(syncAvatar));
    CHECK(!clipNames(mcp, QStringLiteral("avatar.asset().definition.clips.map(function(c){return c.name})"))
               .contains(clip),
          "the OTHER avatar does not have the clip (each avatar keeps its own)");
    mcp.runScript(QStringLiteral("avatar.open('%1')").arg(asyncAvatar));
    CHECK(clipNames(mcp, QStringLiteral("avatar.asset().definition.clips.map(function(c){return c.name})"))
              .contains(clip),
          "the clip is still there after switching away and back");

    // ---- 7. the SWITCH WINDOW refuses every definition edit ---------------
    //
    // Fail-before (measured on the previous tip): `loadAnimation` during an
    // async switch matched the clip against the OUTGOING character's skeleton,
    // appended it to the INCOMING avatar's definition and wrote it to that
    // avatar's file — permanently, and invisibly the moment the preview swapped.
    {
        const QString victim = syncAvatar;     // the avatar being switched TO
        const QString beforeVersion =
            mcp.string(QStringLiteral("(function(){var a = avatar.open('%1'); var v = a.version; "
                                      "return v;})()").arg(victim));
        // back to the other one, then switch to the victim ASYNCHRONOUSLY and
        // edit inside the window.
        mcp.runScript(QStringLiteral("avatar.open('%1')").arg(asyncAvatar));
        const QJsonObject duringSwitch = mcp.runScript(
            QStringLiteral("var opened = avatar.open('%1', {async: true});"
                           "var refusals = [];"
                           "function refused(f) { try { f(); return false; } catch (e) { return true; } }"
                           "refusals.push(refused(function(){ avatar.loadAnimation('%2'); }));"
                           "refusals.push(refused(function(){ avatar.setDefaultClip(''); }));"
                           "refusals.push(refused(function(){ avatar.removeClip('%3'); }));"
                           "refusals.push(refused(function(){ avatar.setClipOptions('%3', {looping: true}); }));"
                           "refusals.push(refused(function(){ avatar.setCharacterHeight(1.9); }));"
                           "refusals.push(refused(function(){ avatar.setClip('%3'); }));"
                           "({running: avatar.progress().running, open: opened.guid,"
                           "  refusals: refusals, clips: avatar.asset().definition.clips.length});")
                .arg(victim, walk, clip)).value("result").toObject();
        std::printf("info: during the switch: %s\n",
                    QJsonDocument(duringSwitch).toJson(QJsonDocument::Compact).constData());
        const QJsonArray refusals = duringSwitch.value("refusals").toArray();
        bool allRefused = refusals.size() == 6;
        for (const QJsonValue &v : refusals) allRefused = allRefused && v.toBool();
        CHECK(duringSwitch.value("running").toBool(),
              "the switch was still in flight while the edits were attempted");
        CHECK(allRefused, "every definition edit REFUSED inside the switch window");
        CHECK(duringSwitch.value("clips").toInt() == 0,
              "... and the incoming avatar's clip list did not grow in memory");

        const JobStats afterWindow = waitForJob(mcp, "switch-window");
        CHECK(afterWindow.done, "the switch finished normally afterwards");
        const QString afterVersion =
            mcp.string(QStringLiteral("avatar.asset().version"));
        CHECK(!beforeVersion.isEmpty() && afterVersion == beforeVersion,
              "the incoming avatar's STORED definition is byte-identical (no version moved)");
        CHECK(mcp.integer(QStringLiteral("avatar.asset().definition.clips.length")) == 0,
              "... and it still has no clips after the parse landed");
        // The same gesture from the UI is greyed out rather than refused: the
        // page reads the same progress() the verbs guard on (avatarpage.cpp).
    }

    // ---- 8. a SYNCHRONOUS open supersedes a parse in flight ---------------
    {
        mcp.runScript(QStringLiteral("avatar.open('%1')").arg(syncAvatar));
        const QJsonObject raced = mcp.runScript(
            QStringLiteral("var parsesBefore = avatar.progress().parsesFinished;"
                           "avatar.open('%1', {async: true});"
                           "var sync = avatar.open('%2');"
                           "({guid: sync.guid, running: avatar.progress().running,"
                           "  parses: parsesBefore,"
                           "  preview: avatar.preview().name});").arg(asyncAvatar, syncAvatar))
                .value("result").toObject();
        std::printf("info: sync open during an async one: %s\n",
                    QJsonDocument(raced).toJson(QJsonDocument::Compact).constData());
        CHECK(raced.value("guid").toString() == syncAvatar,
              "the synchronous open wins the definition");
        CHECK(!raced.value("running").toBool(),
              "... and ends the job it superseded (the dialog cannot hang)");
        // WAIT FOR THE ABANDONED PARSE, do not sleep at it (CLEANUP-1 item 9).
        // This was `QThread::msleep(3000)` and then the assertion — and a parse
        // that was still running (12 s on this box before the fix, and any
        // amount under a -j4 gate) made the assertion pass because NOTHING had
        // landed yet, which is precisely the defect the case exists to catch.
        // A superseded parse never returns to `running`, so the seam is
        // progress().parsesFinished: it counts completions whether or not the
        // result was applied. The baseline is read INSIDE the script, before
        // the async open starts — reading it afterwards would race the very
        // completion this waits for.
        const int parsesBefore = raced.value("parses").toInt();
        bool parseLanded = false;
        {
            QElapsedTimer waiting;
            waiting.start();
            while (waiting.elapsed() < kOpBudgetMs) {
                if (mcp.integer(QStringLiteral("avatar.progress().parsesFinished")) > parsesBefore) {
                    parseLanded = true;
                    break;
                }
                QThread::msleep(50);
            }
            std::printf("info: the abandoned parse completed after %lld ms (parses %d -> %d)\n",
                        static_cast<long long>(waiting.elapsed()), parsesBefore,
                        mcp.integer(QStringLiteral("avatar.progress().parsesFinished")));
        }
        CHECK(parseLanded, "the superseded parse really did finish (so the assertions below mean something)");
        CHECK(mcp.string(QStringLiteral("avatar.asset().guid")) == syncAvatar,
              "the superseded parse did not move the open definition");
        const QString previewName = mcp.string(QStringLiteral("avatar.preview().name"));
        const QString openName = mcp.string(QStringLiteral("avatar.asset().name"));
        std::printf("info: preview='%s' definition='%s'\n",
                    previewName.toUtf8().constData(), openName.toUtf8().constData());
        CHECK(previewName == openName,
              "the PREVIEW and the DEFINITION are the same character (the stale-apply defect)");
    }

    // ---- 6. quitting with an import IN FLIGHT -----------------------------
    //
    // AND WITH AN UNSAVED DEFINITION EDIT INSIDE ITS COALESCING WINDOW
    // (CLEANUP-1 item 2). The three statements below are ONE script on purpose:
    // the definition edit arms a 250 ms single-shot, the import starts on the
    // global thread pool, and the quit is posted — with no round trip between
    // them, so the window is open, with certainty, when the close runs.
    //
    // What this used to do: the shell aborted the Assets page's runners and
    // then waited 3 s on the pool, while the avatar module's abort lived in
    // AvatarApi::detachModel, reached only from shutdownModules() — AFTER that
    // wait, and after the std::_Exit(0) behind it. So a parse over three
    // seconds took the process out from under the module: no ordered shutdown,
    // and the pending write simply gone. Both halves are asserted here.
    mcp.runScript(QStringLiteral("avatar.open('%1')").arg(asyncAvatar));
    // ReplyOptional for the same reason McpClient::quit() uses it: the script
    // ENDS in app.quit(), so the queued close may win the race with the
    // response and a missing reply is the expected outcome, not a transport
    // failure.
    mcp.runScript(QStringLiteral("avatar.setClipOptions('%1', {looping: true});"
                                 "avatar.importAvatar('%2', {async: true});"
                                 "app.quit();").arg(clip, rig),
                  McpClient::ReplyOptional);
    QElapsedTimer exitTimer;
    exitTimer.start();
    const bool exited = jahshaka.waitForFinished(kExitBudgetMs);
    std::printf("info: exit after %lld ms\n", static_cast<long long>(exitTimer.elapsed()));
    CHECK(exited, "process terminated within the exit budget with an avatar import in flight");
    const QByteArray quitLog = jahshaka.readAll();
    if (!exited) { jahshaka.kill(); jahshaka.waitForFinished(5000); }
    else {
        const bool clean = jahshaka.exitStatus() == QProcess::NormalExit && jahshaka.exitCode() == 0;
        if (!clean) {
            std::printf("info: exitStatus=%s exitCode=%d\n",
                        jahshaka.exitStatus() == QProcess::CrashExit ? "CrashExit" : "NormalExit",
                        jahshaka.exitCode());
            std::printf("---- app output tail ----\n%s\n-------------------------\n",
                        quitLog.right(3000).constData());
        }
        CHECK(clean, "process exited normally with code 0");
    }

    // THE ORDERED SHUTDOWN RAN, all eight steps (src/shell/shutdownorder.h) —
    // the thing a forced exit skips. app.shutdown_order asserts the ORDER on a
    // quiet app; this asserts that the sequence happens AT ALL with a module's
    // background job in flight, which is the case that used to lose it.
    {
        QVector<int> steps;
        QRegularExpression re(QStringLiteral(R"(\[shutdown\] step (\d)/8)"));
        auto it = re.globalMatch(QString::fromUtf8(quitLog));
        while (it.hasNext()) steps.append(it.next().captured(1).toInt());
        std::printf("info: shutdown steps recorded during the quit: %d\n", int(steps.size()));
        bool ordered = steps.size() == 8;
        for (int i = 0; i < steps.size(); ++i) if (steps.at(i) != i + 1) ordered = false;
        CHECK(ordered,
              "all eight shutdown steps ran, in order, with an avatar import in flight");
        CHECK(!quitLog.contains("background workers did not stop in time"),
              "... and the shell never had to force the exit");
    }

    // ---- 5b. ... AND SURVIVES A RESTART -----------------------------------
    // The same HOME, a second process: what the module stored is what a user
    // finds when they come back tomorrow.
    QProcess second;
    QString secondToken;
    const quint16 secondPort = freePort();
    CHECK(spawn(second, secondPort, &secondToken, QStringList(), 180000), "the app booted a second time on the same library");
    if (!secondToken.isEmpty()) {
        McpClient restarted;
        restarted.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(secondPort));
        restarted.token = secondToken;
        restarted.clientName = QStringLiteral("avatar-test");
        restarted.transferTimeoutMs = 300000;
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
        // `looping` is TRUE here, and that is the point (item 2): it was set
        // false in section 5, and flipped to true by the same script that
        // started the import and quit — inside the 250 ms write-coalescing
        // window. Reading true after a restart is the proof that the quit path
        // FLUSHES the pending definition write before anything can take the
        // process away. `rootMotion` is section 5's, unchanged, so the row is
        // the same row and not a fresh default.
        CHECK(entry.value("looping").toBool() == true && entry.value("rootMotion").toBool() == true,
              "the definition edit made inside the coalescing window SURVIVED the quit");
        CHECK(restarted.string(QStringLiteral("avatar.asset().definition.defaultClip")) == clip,
              "... and it is still the default clip");
        restarted.quit();
        if (!second.waitForFinished(kExitBudgetMs)) { second.kill(); second.waitForFinished(5000); }
    }

    std::printf(failures ? "FAILED: %d check(s)\n" : "ALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
