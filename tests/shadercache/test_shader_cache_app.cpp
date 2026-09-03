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
//   run 2, warm : after run 1 has quit and saved, a second launch compiles
//                 NOTHING and serves everything from the cache. This is the
//                 whole feature, asserted.
//   run 3       : --clear-shader-cache makes run 2's warm launch cold again,
//                 and the run still succeeds. Our r.InvalidateCachedShaders.
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

/// Runs the app with `--script`, returns the app.shaderCache() object the
/// script printed. The marker prefix keeps it findable in a log the engine also
/// writes to.
QJsonObject runApp(const QString &home, const QString &script, const QStringList &extraArgs,
                   int *exitCodeOut)
{
    QProcess app;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HOME", home);
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
    CHECK(cold.value("afterClearFiles").toInt() == 0, "app.clearShaderCache() removed them");

    // ---- run 2: warm ------------------------------------------------------
    // Run 1 cleared its own cache at the end, then quit — and the clean-quit
    // save wrote it back from the session's memory. So run 2 is a genuine warm
    // launch, and it is also a test of that rewrite path.
    const QJsonObject warm = runApp(home, scripts + "e2e_shader_cache_warm.js", {}, &rc);
    CHECK(rc == 0, "run 2 exited cleanly");
    CHECK(warm.value("compiledThisRun").toInt() == 0, "run 2 compiled NOTHING");
    CHECK(warm.value("loadedThisRun").toInt() > 0, "run 2 served its shaders from the cache");
    CHECK(warm.value("microcodeLoaded").toBool(), "the microcode layer loaded");
    CHECK(warm.value("pipelineCacheLoaded").toBool(), "the pipeline layer loaded");
    CHECK(warm.value("hlmsCachesLoaded").toInt() > 0, "the Hlms layer loaded");
    CHECK(warm.value("expectedShaders").toInt() > 0,
          "the progress counter has a denominator on the second launch");
    CHECK(warm.value("fingerprint").toString() == cold.value("fingerprint").toString(),
          "the fingerprint is stable between launches");

    // ---- run 3: --clear-shader-cache --------------------------------------
    const QJsonObject cleared = runApp(home, scripts + "e2e_shader_cache_warm.js",
                                       {QStringLiteral("--clear-shader-cache")}, &rc);
    CHECK(rc == 0, "run 3 exited cleanly with --clear-shader-cache");
    CHECK(cleared.value("compiledThisRun").toInt() > 0,
          "--clear-shader-cache made the next launch cold again");
    // NOT "loadedThisRun == 0": that counter also ticks for an IN-PROCESS
    // microcode hit (two shaders generated from identical source — the SMAA
    // materials do it three times every launch), so a genuinely cold run still
    // reports a few. What a cold run cannot do is LOAD A LAYER.
    CHECK(!cleared.value("microcodeLoaded").toBool() &&
          !cleared.value("pipelineCacheLoaded").toBool(),
          "--clear-shader-cache left no layer to load");

    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
