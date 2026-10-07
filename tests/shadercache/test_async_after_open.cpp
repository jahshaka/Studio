// shader.async_after_open (ASYNC-SHADERS-1) — after a project is open, nothing compiles on the
// UI thread: a new material and a tier change are built by the engine's background compiler
// while the view keeps drawing, the waiting objects show the grey placeholder, the pending
// count ("Compiling shaders (N)") rises and falls back to 0, and the settled picture is the one
// a warm launch draws. Two launches into one scratch home: COLD (an empty shader cache) and
// WARM (the reference: everything is already built, nothing may take the placeholder).
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
/// THE SAFETY NET'S BOUND per phase: a frame held because an object had neither its shader
/// nor a placeholder. Measured 0 (material and tier, a cold cache); a frame or two of slack
/// for a placeholder that lands one frame late under a loaded box.
static constexpr int kMaxHeldFrames = 2;
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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString home = QDir::current().absoluteFilePath(QStringLiteral("e2e-home-async-after-open"));
    QDir(home).removeRecursively();            // a cold cache, an empty library
    QDir().mkpath(home + "/run");
    QDir().mkpath(home + "/cache");
    QDir().mkpath(home + "/cold");
    QDir().mkpath(home + "/warm");

    const QJsonObject cold = runApp(home, home + "/cold");
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
        // pass). A frame with an object that has none yet is HELD, never presented with a hole
        // — the safety net, which must stay rare: measured 0 frames for both phases.
        CHECK(c.value("placeholderDraws").toInt() > 0,
              ("cold " + n + ": the waiting objects drew the grey placeholder").constData());
        CHECK(c.value("heldFrames").toInt() <= kMaxHeldFrames,
              ("cold " + n + ": the held-frame safety net stayed rare").constData());
        CHECK(c.value("holeyPresented").toInt() == 0,
              ("cold " + n + ": no frame with a hole was presented").constData());
        CHECK(w.value("placeholderDraws").toInt() == 0 && w.value("pendingSkips").toInt() == 0,
              ("warm " + n + ": nothing took the placeholder or skipped a draw").constData());
        std::printf("    cold %s: placeholder draws %d, held frames %d\n", name,
                    c.value("placeholderDraws").toInt(), c.value("heldFrames").toInt());

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

    std::printf("%s (%d failure(s))\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
