// The shader cache as the APPLICATION exposes it: the verbs, the CLI flag, and
// the startup shader build that now runs behind the launch screen.
//
// The container suite next door proves the file format survives abuse. This one
// proves the product-level contract, and it does it the only way that is worth
// anything — by launching the REAL binary, twice, into a scratch HOME, and
// reading what `app.shaderCache()` says each time (the import.shutdown /
// open.responsive pattern).
//
//   run 1, cold : the cache reports itself enabled and located, the startup
//                 gate builds shaders BEFORE the window (compiledThisRun > 0),
//                 app.saveShaderCache() writes files, app.clearShaderCache()
//                 removes them, and the session survives its own cache being
//                 deleted underneath it.
//   run 2, warm : after run 1 has quit and saved, a second launch compiles a
//                 small fraction of what the cold one did and serves the rest
//                 from the cache. This is the whole feature, asserted.
//   run 3       : steady state — NOTHING compiles. (Run 2 is not zero because
//                 it also replays the warm-up set run 1 recorded, whose
//                 degenerate vertex formats are permutations of their own.)
//   run 4       : --clear-shader-cache makes a warm launch cold again, and the
//                 run still succeeds. Our r.InvalidateCachedShaders.
//   run 5       : THE SAVE UNDER CHURN. Every World post row through every
//                 value, the player in and out, 300 SKY CHANGES, a save after
//                 each step — the path that crashed three instances on
//                 2026-09-14. Asserts the process survives it AND that neither
//                 ogre-patch 0035's guard nor its pass-cache overflow warning
//                 ever had to fire. The sky phase is the one that makes this
//                 run a REGRESSION test rather than a hope: before the fix
//                 (lane shadercache-2) those 300 captures minted 300 permanent
//                 Hlms pass-cache entries and the warning fired at 256.
//                 (The field is 13 bits since ogre-patch 0046 and run 5 asserts
//                 the live count instead of the warning — see run 5.)
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

/// The last run's merged stdout+stderr, for the assertions that are about what
/// the app SAID rather than about what app.shaderCache() reported.
QString gLastOutput;

/// Runs the app with `--script`, returns the app.shaderCache() object the
/// script printed. The marker prefix keeps it findable in a log the engine also
/// writes to.
QJsonObject runApp(const QString &home, const QString &script, const QStringList &extraArgs,
                   int *exitCodeOut)
{
    QProcess app;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HOME", home);
    // The data-root override (hygiene batch, 2026-09-09) moves the settings file
    // beside the DB/store/cache. Without it a QT_DEBUG build writes
    // build-linux/bin/jahsettings.ini — the OWNER's file — from this suite (the
    // final gate of 2026-09-09 caught it as the last remaining writer).
    env.insert("JAHSHAKA_DATA_ROOT", home + "/.local/share/Jahshaka");
    // The NVIDIA driver keeps a shader cache of its own and it is worth ~9% of
    // a launch (SHADER_CACHE_SPEC §2.6). It cannot change this test's PASS/FAIL
    // — we assert compile COUNTS, not wall time — but pinning it keeps the
    // suite from writing into the user's real cache directory.
    env.insert("XDG_CACHE_HOME", home + "/cache");
    app.setProcessEnvironment(env);
    app.setWorkingDirectory(home + "/run");
    app.setProcessChannelMode(QProcess::MergedChannels);
    app.start(QStringLiteral(JAHSHAKA_BINARY), QStringList() << extraArgs << "--script" << script);
    if (!app.waitForStarted(20000)) { std::printf("FAIL: app did not start\n"); ++failures; return {}; }
    if (!app.waitForFinished(240000)) {
        std::printf("FAIL: app did not exit\n"); ++failures; app.kill(); app.waitForFinished(5000);
        return {};
    }
    if (exitCodeOut) *exitCodeOut = app.exitCode();
    const QString out = QString::fromUtf8(app.readAll());
    gLastOutput = out;
    QJsonObject last;
    for (const QString &line : out.split('\n')) {
        const int at = line.indexOf(QStringLiteral("SHADERCACHE "));
        if (at < 0) continue;
        last = QJsonDocument::fromJson(line.mid(at + 12).toUtf8()).object();
    }
    if (last.isEmpty()) {
        std::printf("---- app output ----\n%s\n--------------------\n", qPrintable(out));
    }
    return last;
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString home = QDir::current().absoluteFilePath(QStringLiteral("e2e-home-shadercache"));
    QDir().mkpath(home + "/run");
    QDir().mkpath(home + "/cache");
    // A fresh start every time the suite runs, or "cold" would be a lie.
    QDir(home + "/.local/share/Jahshaka/shadercache").removeRecursively();

    const QString scripts = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR) +
                            QStringLiteral("/tests/shadercache/scripts/");

    // ---- run 1: cold ------------------------------------------------------
    int rc = -1;
    const QJsonObject cold = runApp(home, scripts + "e2e_shader_cache_cold.js", {}, &rc);
    CHECK(rc == 0, "run 1 exited cleanly");
    CHECK(cold.value("enabled").toBool(), "the cache is enabled by default");
    CHECK(cold.value("dir").toString().endsWith(QStringLiteral("/shadercache")),
          "the cache lives in AppDataLocation/shadercache");
    CHECK(cold.value("dir").toString().startsWith(home),
          "the test's cache is inside the scratch HOME, never the user's");
    CHECK(!cold.value("fingerprint").toString().isEmpty(), "a fingerprint is reported");
    // THE OWNER'S REQUIREMENT, asserted: the startup shader build happens
    // before the window, so by the time a script runs, the shaders are built.
    CHECK(cold.value("compiledThisRun").toInt() > 0,
          "run 1 compiled its shaders during startup, before the script ran");
    CHECK(cold.value("expectedShaders").toInt() == 0,
          "a first-ever run has no progress denominator yet");
    // The script also asserted save -> files, clear -> no files; it prints its
    // own ok:/FAIL: lines, which the exit code folds in.
    CHECK(cold.value("afterSaveFiles").toInt() > 0, "app.saveShaderCache() wrote files");
    // EXACTLY ONE FILE SURVIVES A CLEAR, AND IT IS THE LOCK (audit F10).
    //
    // This used to assert 0, because clearShaderCacheOnDisk() called
    // removeRecursively() and took `cache.lock` with everything else. Deleting
    // a locked file releases nothing on Linux: this session keeps its fcntl
    // lock on an unlinked inode, the next process creates a fresh cache.lock,
    // locks THAT, and two processes both believe they are the single writer.
    // So the number is 1, and 1 is the assertion — 0 would mean the regression
    // is back, and 2 would mean something else survived that should not have.
    CHECK(cold.value("afterClearFiles").toInt() == 1,
          "app.clearShaderCache() removed every cache file and kept only the writer lock");
    // The warm-up set's reporting surface (F1b/F12).
    CHECK(cold.value("warmUpEnabled").toBool(),
          "app.warmUpSet() reports the automatic record/replay armed with the cache on");
    CHECK(cold.value("warmUpShapeSamples").toInt() >= 1,
          "app.warmUpSet() reports the pass shape the startup warm-up will match");

    // ---- run 2: warm ------------------------------------------------------
    // Run 1 cleared its own cache at the end, then quit — and the clean-quit
    // save wrote it back from the session's memory. So run 2 is a genuine warm
    // launch, and it is also a test of that rewrite path.
    const QJsonObject warm = runApp(home, scripts + "e2e_shader_cache_warm.js", {}, &rc);
    CHECK(rc == 0, "run 2 exited cleanly");
    // NOT "compiled nothing", and the reason is a real (small, one-off) cost
    // worth pinning: run 1 also RECORDED a warm-up set, and run 2 replays it.
    // The replay applies the recorded materials to DEGENERATE 4-vertex buffers,
    // whose vertex format is not byte-identical to the originals — so the first
    // launch that replays a set compiles a handful of permutations of its own,
    // once, and caches them. Measured: 64 cold, 2 on the first replay, 0 from
    // then on. What must hold is that the cache did nearly all the work.
    CHECK(warm.value("compiledThisRun").toInt() * 4 < cold.value("compiledThisRun").toInt(),
          "run 2 compiled a small fraction of what the cold run did");
    CHECK(warm.value("loadedThisRun").toInt() > 0, "run 2 served its shaders from the cache");
    CHECK(warm.value("microcodeLoaded").toBool(), "the microcode layer loaded");
    CHECK(warm.value("pipelineCacheLoaded").toBool(), "the pipeline layer loaded");
    CHECK(warm.value("hlmsCachesLoaded").toInt() > 0, "the Hlms layer loaded");
    CHECK(warm.value("expectedShaders").toInt() > 0,
          "the progress counter has a denominator on the second launch");
    CHECK(warm.value("fingerprint").toString() == cold.value("fingerprint").toString(),
          "the fingerprint is stable between launches");

    // ---- run 3: steady state ----------------------------------------------
    // The launch after the warm-up set's own permutations have been cached: the
    // number that describes every launch a user ever sees after the first two.
    const QJsonObject steady = runApp(home, scripts + "e2e_shader_cache_warm.js", {}, &rc);
    CHECK(rc == 0, "run 3 exited cleanly");
    CHECK(steady.value("compiledThisRun").toInt() == 0, "run 3 compiled NOTHING AT ALL");
    CHECK(steady.value("loadedThisRun").toInt() > 0, "and served everything from the cache");

    // ---- run 4: --clear-shader-cache --------------------------------------
    const QJsonObject cleared = runApp(home, scripts + "e2e_shader_cache_warm.js",
                                       {QStringLiteral("--clear-shader-cache")}, &rc);
    CHECK(rc == 0, "run 4 exited cleanly with --clear-shader-cache");
    CHECK(cleared.value("compiledThisRun").toInt() > 0,
          "--clear-shader-cache made the next launch cold again");
    // NOT "loadedThisRun == 0": that counter also ticks for an IN-PROCESS
    // microcode hit (two shaders generated from identical source — the SMAA
    // materials do it three times every launch), so a genuinely cold run still
    // reports a few. What a cold run cannot do is LOAD A LAYER.
    CHECK(!cleared.value("microcodeLoaded").toBool() &&
          !cleared.value("pipelineCacheLoaded").toBool(),
          "--clear-shader-cache left no layer to load");

    // ---- run 5: THE SAVE UNDER CHURN (SMOKE-ENGINE-1 item 3) --------------
    // The periodic save crashed the owner's editor and two rig instances on
    // 2026-09-14, inside HlmsDiskCache::copyFrom, which subscripts Ogre's
    // renderable and pass caches with indices unpacked from a shader hash and
    // checks neither (ogre-patch 0035 checks them now). Both indices grow with
    // CHURN — new material/mesh permutations, new pass property sets, and every
    // World post row is a compositor rebuild that produces some.
    //
    // THE CAUSE IS KNOWN SINCE 2026-09-14 (lane shadercache-2) and this run is
    // the regression test for it. It was never a renderable-cache reset: the
    // PASS index has eight bits, `HlmsPbs::preparePassHash` puts the cube
    // render target's NAME in the pass properties (`target_envprobe_map`), and
    // this engine gave every sky capture a fresh name — one permanent pass-cache
    // entry per capture, 1847 of them in the owner's session, and past 256 the
    // pass index spills into the renderable field so copyFrom subscripts
    // mRenderableCache out of range.
    //
    // The script drives that churn — the post rows, the player, and 300 sky
    // changes — and saves after every step. This run asserts:
    //   * the process SURVIVES it (before 0035 an out-of-range index was a
    //     SIGSEGV, not a skipped entry),
    //   * the guard never had to fire, and
    //   * the pass cache stayed SMALL. That last one is the regression: with the
    //     capture cube named uniquely again, 300 sky changes mint 300 permanent
    //     pass-cache entries (measured, A/B).
    //
    // IT IS A NUMBER NOW, NOT THE ABSENCE OF A WARNING (lane HLMSBITS-1).
    // ogre-patch 0046 rebalanced the shader hash to [3][16][13], so the pass
    // field holds 8,192 entries and 300 stray ones would no longer trip any
    // log line — the assertion that caught this regression would have gone
    // quiet while still printing "ok". app.shaderCache() reports the live cache
    // sizes, so the bound is stated directly, and it is deliberately 256: the
    // size of the field that actually overflowed, which the churn must stay
    // inside whatever the hash's split becomes later.
    const QJsonObject churn = runApp(home, scripts + "e2e_shader_cache_churn.js", {}, &rc);
    CHECK(rc == 0, "run 5 survived the churn and exited cleanly");
    CHECK(!churn.isEmpty(), "run 5 reported its cache state after the churn");
    CHECK(!gLastOutput.contains(QStringLiteral("skipping shader cache entry")),
          "no shader-cache entry had an out-of-range index or a missing PSO during the churn");
    const int passEntries = churn.value(QStringLiteral("passCacheEntries")).toInt(-1);
    const int passCapacity = churn.value(QStringLiteral("passCacheCapacity")).toInt(-1);
    std::printf("      pass cache after the churn: %d of %d\n", passEntries, passCapacity);
    CHECK(passEntries > 0 && passEntries < 256,
          "the pass cache stayed inside 256 entries through 300 sky captures "
          "(the recycled capture name)");
    CHECK(passCapacity >= 8192,
          "the shader hash's pass field addresses at least 8192 entries (ogre-patch 0046)");
    CHECK(!gLastOutput.contains(QStringLiteral("distinct pass property combinations")),
          "no Hlms reported its pass cache filling up");
    CHECK(!gLastOutput.contains(QStringLiteral("save already in progress")),
          "no re-entrant save was attempted");

    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
