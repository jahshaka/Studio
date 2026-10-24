// shader.async_after_open (ASYNC-SHADERS-1) — after a project is open, nothing compiles on the
// UI thread: a new material and a tier change are built by the engine's background compiler
// while the view keeps drawing, the waiting objects show the grey placeholder, the pending
// count ("Compiling shaders (N)") rises and falls back to 0, and the settled picture is the one
// a warm launch draws. Two launches into one scratch home: COLD and WARM (the reference: everything
// is already built, nothing may take the placeholder).
//
// THE COLD LAUNCH STARTS FROM A WARM STARTUP CACHE (TESTING-SPEED-B3, the lead's verdict on the
// profile): the subject is the OPEN's background compile — the new material's and the tier's
// permutations, which no startup set holds — never the startup build behind the splash (53-58 s
// of the old 87-s cold launch, Debug, a loaded box; spikes/testing-speed-b3/profile.txt). So the
// cold home is seeded with THE STARTUP SEED: the shader cache a launch that only boots and quits
// leaves (`e2e-async-after-open-seed/`, kept beside the row's home). A seed the build no longer matches (the cache's
// fingerprint moved) is refused by the app, which then compiles the startup set itself — the run stays valid, only
// slower — and the row deletes the stale seed so the next run makes a fresh one.
//
// THE SEED IS ITS OWN ROW (TOOLING-7 stage 2; the trap file's "split into rows that run parts of the same law"):
// `test_async_after_open --seed` is shader.async_after_open.seed, the FIXTURES_SETUP row that makes the seed when it is
// missing — a boot that only quits, once per build tree (a ship tree is a fresh tree: the row paid that whole startup
// build inside itself, 64 s solo on a fresh tree, 2026-10-20, over the minute). The row the law is about runs the two
// launches only: the startup build is not its subject.
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <cstdio>

static int failures = 0;
/// THE SETTLED PICTURE'S TOLERANCE against the warm launch's (mean |difference| per channel,
/// codes 0-255). Measured: see the evidence file named in the lane report.
static constexpr double kSettledMeanTolerance = 0.5;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

QJsonObject runApp(const QString &home, const QString &outDir)
{
    const QString script = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR) +
                           QStringLiteral("/tests/shadercache/scripts/e2e_async_after_open.js");
    QFile in(script);
    if (!in.open(QIODevice::ReadOnly)) { std::printf("FAIL: no script\n"); ++failures; return {}; }
    const QString wrapped = home + "/run/async_after_open.js";
    {
        QFile outFile(wrapped);
        outFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
        outFile.write(QStringLiteral("var OUTDIR = \"%1\";\n").arg(outDir).toUtf8());
        outFile.write(in.readAll());
    }
    QProcess app;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HOME", home);
    env.insert("JAHSHAKA_DATA_ROOT", home + "/.local/share/Jahshaka");
    env.insert("XDG_CACHE_HOME", home + "/cache");
    app.setProcessEnvironment(env);
    app.setWorkingDirectory(home + "/run");
    app.setProcessChannelMode(QProcess::MergedChannels);
    app.start(QStringLiteral(JAHSHAKA_BINARY), QStringList() << "--script" << wrapped);
    if (!app.waitForStarted(20000)) { std::printf("FAIL: app did not start\n"); ++failures; return {}; }
    if (!app.waitForFinished(500000)) {
        std::printf("FAIL: app did not exit\n"); ++failures; app.kill(); app.waitForFinished(5000);
        return {};
    }
    const QString out = QString::fromUtf8(app.readAll());
    QJsonObject last;
    for (const QString &line : out.split('\n')) {
        const int at = line.indexOf(QStringLiteral("ASYNCAFTEROPEN "));
        if (at >= 0) last = QJsonDocument::fromJson(line.mid(at + 15).toUtf8()).object();
        if (line.contains(QStringLiteral("[shader] ")) && line.contains(QStringLiteral("SHADER-WARM-2")))
            std::printf("    app: %s\n", qPrintable(line.trimmed()));
    }
    if (last.isEmpty()) std::printf("---- app output ----\n%s\n--------------------\n", qPrintable(out));
    return last;
}

QJsonObject phaseOf(const QJsonObject &r, const char *name)
{
    for (const QJsonValue &v : r.value("phases").toArray())
        if (v.toObject().value("phase").toString() == QLatin1String(name)) return v.toObject();
    return {};
}

/// Mean absolute difference per channel (0..255) and the share of pixels that differ at all.
void compare(const QString &a, const QString &b, double &meanAbs, double &differing)
{
    meanAbs = 255.0; differing = 1.0;
    QImage ia(a), ib(b);
    if (ia.isNull() || ib.isNull() || ia.size() != ib.size()) return;
    ia = ia.convertToFormat(QImage::Format_RGB32);
    ib = ib.convertToFormat(QImage::Format_RGB32);
    double sum = 0.0; long long diff = 0;
    const long long n = (long long)ia.width() * ia.height();
    for (int y = 0; y < ia.height(); ++y) {
        const QRgb *ra = reinterpret_cast<const QRgb *>(ia.constScanLine(y));
        const QRgb *rb = reinterpret_cast<const QRgb *>(ib.constScanLine(y));
        for (int x = 0; x < ia.width(); ++x) {
            const int d = std::abs(qRed(ra[x]) - qRed(rb[x])) + std::abs(qGreen(ra[x]) - qGreen(rb[x])) +
                          std::abs(qBlue(ra[x]) - qBlue(rb[x]));
            sum += d / 3.0;
            if (d) ++diff;
        }
    }
    meanAbs = sum / double(n);
    differing = double(diff) / double(n);
}

void report(const char *run, const QJsonObject &r)
{
    for (const QJsonValue &v : r.value("phases").toArray()) {
        const QJsonObject p = v.toObject();
        std::printf("    %-5s %-9s frames %3d  pendingPeak %2d  end %d  placeholderDraws %4d  "
                    "pendingSkips %4d  failed %d  live %d  worstFrame %d ms\n",
                    run, qPrintable(p.value("phase").toString()), p.value("frames").toInt(),
                    p.value("pendingPeak").toInt(), p.value("pendingEnd").toInt(),
                    p.value("placeholderDraws").toInt(), p.value("pendingSkips").toInt(),
                    p.value("failed").toInt(), p.value("live").toInt(), p.value("worstFrameMs").toInt());
    }
}

}  // namespace

/// Launches the app with a script that only quits: the startup shader set is built behind the
/// splash and saved at the clean quit — THE STARTUP SEED's source.
bool seedLaunch(const QString &seedHome)
{
    QDir(seedHome).removeRecursively();
    QDir().mkpath(seedHome + "/run");
    QDir().mkpath(seedHome + "/cache");
    const QString script = seedHome + "/run/seed.js";
    {
        QFile f(script);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        f.write("app.quit();\n");
    }
    QProcess app;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HOME", seedHome);
    env.insert("JAHSHAKA_DATA_ROOT", seedHome + "/.local/share/Jahshaka");
    env.insert("XDG_CACHE_HOME", seedHome + "/cache");
    app.setProcessEnvironment(env);
    app.setWorkingDirectory(seedHome + "/run");
    app.setProcessChannelMode(QProcess::MergedChannels);
    app.start(QStringLiteral(JAHSHAKA_BINARY), QStringList() << "--script" << script);
    if (!app.waitForStarted(20000) || !app.waitForFinished(500000)) { app.kill(); app.waitForFinished(5000); return false; }
    return app.exitCode() == 0;
}

/// Copies a directory tree (the seed into the cold home).
bool copyTree(const QString &from, const QString &to)
{
    QDir src(from);
    if (!src.exists()) return false;
    QDir().mkpath(to);
    for (const QFileInfo &fi : src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const QString dst = to + "/" + fi.fileName();
        if (fi.isDir()) { if (!copyTree(fi.absoluteFilePath(), dst)) return false; }
        else if (!QFile::copy(fi.absoluteFilePath(), dst)) return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString home = QDir::current().absoluteFilePath(QStringLiteral("e2e-home-async-after-open"));
    const QString seed = QDir::current().absoluteFilePath(QStringLiteral("e2e-async-after-open-seed/shadercache"));

    // THE STARTUP SEED'S ROW (the header): made when missing; a present seed is the row's whole answer.
    if (app.arguments().contains(QStringLiteral("--seed"))) {
        if (QDir(seed).exists()) {
            std::printf("    the startup seed is there (made by an earlier run of this row): nothing to do\nPASS\n");
            return 0;
        }
        const QString seedHome = QDir::current().absoluteFilePath(QStringLiteral("e2e-async-after-open-seeding"));
        const bool seeded = seedLaunch(seedHome) &&
                            QDir().mkpath(QFileInfo(seed).absolutePath()) &&
                            QDir().rename(seedHome + "/.local/share/Jahshaka/shadercache", seed);
        QDir(seedHome).removeRecursively();
        std::printf("    the startup seed: %s\n%s\n", seeded ? "made by a boot-and-quit launch" : "COULD NOT be made",
                    seeded ? "PASS" : "FAIL");
        return seeded ? 0 : 1;
    }

    QDir(home).removeRecursively();            // an empty library; the shader cache is the seed's
    QDir().mkpath(home + "/run");
    QDir().mkpath(home + "/cache");
    QDir().mkpath(home + "/cold");
    QDir().mkpath(home + "/warm");
    // ...copied into the cold home every run (no seed — the row run alone without its setup — is the stale seed's case:
    // the cold launch compiles the startup set itself, valid and slower)
    if (!copyTree(seed, home + "/.local/share/Jahshaka/shadercache"))
        std::printf("    no startup seed (run shader.async_after_open.seed first): the cold launch builds the startup set\n");

    const QJsonObject cold = runApp(home, home + "/cold");
    // A STALE SEED (the app's cache fingerprint moved since it was made): the cold launch compiled the
    // startup set itself — valid, slower; the next run makes a fresh seed.
    const int startupCompiled = cold.value("startup").toObject().value("compiledThisRun").toInt(-1);
    std::printf("    the cold launch's startup: compiled %d shader(s) before the script ran\n", startupCompiled);
    if (startupCompiled > 0) {
        QDir(QFileInfo(seed).absolutePath()).removeRecursively();
        std::printf("    the startup seed was stale (the cache fingerprint moved): deleted, the next run re-seeds\n");
    }
    const QJsonObject warm = runApp(home, home + "/warm");
    CHECK(!cold.isEmpty() && !warm.isEmpty(), "both launches reported");
    report("cold", cold);
    report("warm", warm);

    CHECK(cold.value("ready").toObject().value("running").toBool(), "the background compiler runs");
    for (const char *name : { "material", "tier" }) {
        const QJsonObject c = phaseOf(cold, name), w = phaseOf(warm, name);
        const QByteArray n(name);
        CHECK(c.value("live").toInt() == 0, ("cold " + n + ": no shader compiled on the UI thread").constData());
        CHECK(w.value("live").toInt() == 0, ("warm " + n + ": no shader compiled on the UI thread").constData());
        CHECK(c.value("pendingPeak").toInt() > 0, ("cold " + n + ": the pending count rose").constData());
        CHECK(c.value("pendingEnd").toInt() == 0, ("cold " + n + ": ...and fell back to 0").constData());
        CHECK(c.value("failed").toInt() == 0, ("cold " + n + ": no permutation failed").constData());
        // WHILE A SHADER BUILDS THE WAITING OBJECTS DRAW GREY: the PBS and the Atom decode
        // placeholders (a tier's are built by the startup gate's sweep, the placeholders-only
        // pass). Every frame is presented (BUGS-7: the held frame is deleted).
        CHECK(c.value("placeholderDraws").toInt() > 0,
              ("cold " + n + ": the waiting objects drew the grey placeholder").constData());
        CHECK(w.value("placeholderDraws").toInt() == 0 && w.value("pendingSkips").toInt() == 0,
              ("warm " + n + ": nothing took the placeholder or skipped a draw").constData());
        std::printf("    cold %s: placeholder draws %d, pending skips %d\n", name,
                    c.value("placeholderDraws").toInt(), c.value("pendingSkips").toInt());

        // The placeholder is ON SCREEN when one was drawn: that frame differs from the settled one.
        const QString during = c.value("pictureDuring").toString();
        double m = 0.0, d = 0.0;
        if (c.value("placeholderDraws").toInt() > 0) {
            compare(during, c.value("pictureAfter").toString(), m, d);
            std::printf("    cold %s: during vs settled mean |d| %.2f, %.1f %% of pixels\n", name, m, d * 100.0);
            CHECK(QFileInfo::exists(during) && d > 0.001,
                  ("cold " + n + ": the placeholder is in the presented frame").constData());
        }

        // ...and the settled picture is the warm run's.
        compare(c.value("pictureAfter").toString(), w.value("pictureAfter").toString(), m, d);
        std::printf("    %s: cold settled vs warm settled mean |d| %.3f, %.2f %% of pixels\n", name, m,
                    d * 100.0);
        CHECK(m < kSettledMeanTolerance,
              (n + ": the settled picture is the warm run's").constData());
    }
    CHECK(cold.value("live").toInt() == 0 && warm.value("live").toInt() == 0,
          "liveCompiles is 0 for both sessions after the open");
    // THE COMPUTE PARAMETERS FIT THEIR PROGRAMS (PHOTON-7 stage 5): the warm launch is the one whose GI builds while its
    // compute permutations land from the compile threads - a refused dispatch is counted, never dispatched
    {
        const qulonglong mc = cold.value("final").toObject().value("paramsMisfits").toVariant().toULongLong();
        const qulonglong mw = warm.value("final").toObject().value("paramsMisfits").toVariant().toULongLong();
        std::printf("    compute dispatches refused for parameters not fitting their program: cold %llu, warm %llu\n",
                    (unsigned long long)mc, (unsigned long long)mw);
        CHECK(cold.value("final").toObject().contains("paramsMisfits") && mc == 0u && mw == 0u,
              "no compute dispatch had parameters that do not fit its program (both sessions)");
    }

    std::printf("%s (%d failure(s))\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
