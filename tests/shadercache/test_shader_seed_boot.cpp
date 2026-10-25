// shadercache.seed_boot.twin / shadercache.seed_boot — THE SHIPPED MICROCODE SEED, at the app (COLD-BOOT-1 S1).
//
// THE ACCEPTANCE: from a virgin data root and a virgin HOME (the NVIDIA driver's own cache lives in it, so it is cold
// too), with the seed present, glslang compiles at most 5 % of the cold count; the startup gate's wall and its
// longest stall are printed against an unseeded twin.
//
// TWO ROWS, ONE FIXTURE (the shader.async_after_open.seed pattern): a cold boot of the gate is ~29 s and a seeded one
// ~23 s on this box, so one row holding both would sit at the 60-s cap.
//   --twin   THE UNSEEDED TWIN: a virgin home, JAHSHAKA_SHADER_SEED unset — so the app resolves the BUILD's own seed
//            directory (<exe dir>/shadercache-seed, asserted), which a lane's build does not carry (asserted "absent":
//            a tree whose bin/ holds a seed cannot run an unseeded twin this way, and says so). Its numbers go to
//            twin.json; the cache its clean quit saved becomes the seed through scripts/shadercache/make-seed.sh, the
//            producer the ship uses — so the row proves the producer's output loads.
//   (none)   THE SEEDED BOOT: another virgin home, JAHSHAKA_SHADER_SEED = that seed. The seed must load, serve the
//            gate (compiles <= 5 % of the twin's) and come out of the run byte-identical (never written).
// --app <exe>: the binary booted (default: this tree's) — the red-before run pointed it at a build without the seed.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <algorithm>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } } while (0)

namespace {

struct Launch {
    QJsonObject report;     ///< the script's SHADERSEED object
    QString output;         ///< merged stdout + stderr
    int rc = -1;
    double wallS = 0.0;
    // the gate's own line: "startup shader build: N shaders (C compiled, F from cache) in T ms"
    bool gateSeen = false;
    long long gateMs = -1;
    int gateCompiled = -1, gateFromCache = -1;
    // the heartbeat's named stalls before the gate's line (>= 400 ms each, mainthreadheartbeat.cpp)
    int stalls = 0;
    long long stallSumMs = 0, stallMaxMs = 0;
};

Launch runApp(const QString &app, const QString &home, const QString *seedEnv)
{
    Launch l;
    QDir(home).removeRecursively();   // A VIRGIN HOME AND DATA ROOT, every run (the subject is the first launch)
    QDir().mkpath(home + "/run");
    QDir().mkpath(home + "/cache");
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HOME", home);
    env.insert("JAHSHAKA_DATA_ROOT", home + "/.local/share/Jahshaka");
    // The driver's own pipeline cache in the virgin home too: cold on both launches, so the difference is the seed's.
    env.insert("XDG_CACHE_HOME", home + "/cache");
    if (seedEnv) env.insert("JAHSHAKA_SHADER_SEED", *seedEnv);
    else         env.remove("JAHSHAKA_SHADER_SEED");
    p.setProcessEnvironment(env);
    p.setWorkingDirectory(home + "/run");
    p.setProcessChannelMode(QProcess::MergedChannels);
    const QString script = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR) + "/tests/shadercache/scripts/e2e_shader_seed.js";
    QElapsedTimer t;
    t.start();
    p.start(app, { QStringLiteral("--script"), script });
    if (!p.waitForStarted(20000)) { std::printf("FAIL: %s did not start\n", qPrintable(app)); ++failures; return l; }
    if (!p.waitForFinished(110000)) {
        std::printf("FAIL: the app did not exit\n"); ++failures; p.kill(); p.waitForFinished(5000);
    }
    l.wallS = t.elapsed() / 1000.0;
    l.rc = p.exitCode();
    l.output = QString::fromUtf8(p.readAll());
    const QRegularExpression gate(QStringLiteral(
        "startup shader build: (\\d+) shaders \\((\\d+) compiled, (\\d+) from cache\\) in (\\d+) ms"));
    const QRegularExpression stall(QStringLiteral("\\[heartbeat\\] UI thread blocked (\\d+) ms"));
    for (const QString &line : l.output.split('\n')) {
        const int at = line.indexOf(QStringLiteral("SHADERSEED "));
        if (at >= 0) l.report = QJsonDocument::fromJson(line.mid(at + 11).toUtf8()).object();
        if (l.gateSeen) continue;
        const QRegularExpressionMatch g = gate.match(line);
        if (g.hasMatch()) {
            l.gateSeen = true;
            l.gateCompiled = g.captured(2).toInt();
            l.gateFromCache = g.captured(3).toInt();
            l.gateMs = g.captured(4).toLongLong();
            continue;
        }
        const QRegularExpressionMatch s = stall.match(line);
        if (s.hasMatch()) {
            const long long ms = s.captured(1).toLongLong();
            ++l.stalls;
            l.stallSumMs += ms;
            l.stallMaxMs = std::max(l.stallMaxMs, ms);
        }
    }
    if (l.report.isEmpty())
        std::printf("---- app output (no SHADERSEED line) ----\n%s\n----\n", qPrintable(l.output.right(6000)));
    std::printf("   launch %s: rc %d, %.1f s; the gate: %lld ms (%d compiled, %d from cache), %d stall(s) >= 400 ms "
                "summing %lld ms, longest %lld ms\n",
                seedEnv ? "seeded" : "twin", l.rc, l.wallS, l.gateMs, l.gateCompiled, l.gateFromCache, l.stalls,
                l.stallSumMs, l.stallMaxMs);
    return l;
}

/// The seed directory, by name -> sha256: what "never written" compares.
QMap<QString, QByteArray> snapshot(const QString &dir)
{
    QMap<QString, QByteArray> m;
    const QFileInfoList files = QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : files) {
        QFile f(fi.absoluteFilePath());
        QByteArray h = "unreadable";
        if (fi.isFile() && f.open(QIODevice::ReadOnly)) {
            QCryptographicHash sha(QCryptographicHash::Sha256);
            sha.addData(&f);
            h = sha.result().toHex();
        }
        m.insert(fi.fileName(), h);
    }
    return m;
}

bool sameDir(const QString &a, const QString &b)
{
    return QFileInfo(a).canonicalFilePath() == QFileInfo(b).canonicalFilePath() && !QFileInfo(a).canonicalFilePath().isEmpty();
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication qapp(argc, argv);
    const QStringList args = qapp.arguments();
    const bool twinRow = args.contains(QStringLiteral("--twin"));
    QString app = QStringLiteral(JAHSHAKA_BINARY);
    const int appAt = args.indexOf(QStringLiteral("--app"));
    if (appAt > 0 && appAt + 1 < args.size()) app = args.at(appAt + 1);
    const QString root = QDir::current().absoluteFilePath(QStringLiteral("e2e-seed-boot"));
    const QString seed = root + "/seed";
    const QString twinJson = root + "/twin.json";
    QDir().mkpath(root);
    std::printf("== %s (%s)\n", twinRow ? "shadercache.seed_boot.twin" : "shadercache.seed_boot", qPrintable(app));

    if (twinRow) {
        QDir(seed).removeRecursively();
        QFile::remove(twinJson);
        const Launch t = runApp(app, root + "/twin", nullptr);
        const QJsonObject &r = t.report;
        CHECK(t.rc == 0 && !r.isEmpty(), "the twin booted, ran its script and quit cleanly (rc %d)", t.rc);
        CHECK(r.value("enabled").toBool(), "the shader cache is on");
        // THE BUILD'S OWN SEED, RESOLVED BESIDE THE BINARY (EngineHost::shaderCacheSeedDirectory): the parent is the
        // binary's directory (canonical: the app reads its own path through /proc) and the name is the seed's.
        const QString seedDir = r.value("seedDir").toString();
        CHECK(QFileInfo(seedDir).fileName() == QStringLiteral("shadercache-seed") &&
                  sameDir(QFileInfo(seedDir).absolutePath(), QFileInfo(app).absolutePath()),
              "with JAHSHAKA_SHADER_SEED unset the seed is <exe dir>/shadercache-seed (%s)", qPrintable(seedDir));
        CHECK(r.value("seedState").toString() == QStringLiteral("absent"),
              "this build carries no seed, so the twin is unseeded (seedState '%s'; a tree whose bin/ holds a seed "
              "cannot run the twin)", qPrintable(r.value("seedState").toString()));
        CHECK(!r.value("microcodeLoaded").toBool(), "the twin loaded no microcode layer: a cold first launch");
        const int compiled = r.value("compiledThisRun").toInt(-1);
        CHECK(t.gateSeen && compiled > 0, "the twin's gate compiled its shaders cold (%d)", compiled);
        // THE SEED, BY THE SHIP'S PRODUCER, from the cache the twin's clean quit saved.
        QProcess mk;
        mk.setProcessChannelMode(QProcess::MergedChannels);
        mk.start(QStringLiteral("sh"), { QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR) + "/scripts/shadercache/make-seed.sh",
                                         root + "/twin/.local/share/Jahshaka/shadercache", seed });
        mk.waitForFinished(30000);
        std::printf("   %s", mk.readAll().constData());
        CHECK(mk.exitCode() == 0 && QFileInfo::exists(seed + "/microcode.cache") &&
                  QFileInfo::exists(seed + "/cache-manifest.txt"),
              "make-seed.sh made the seed from the twin's saved cache (rc %d)", mk.exitCode());
        QJsonObject j;
        j["compiled"] = compiled;
        j["gateMs"] = double(t.gateMs);
        j["maxGapMs"] = r.value("maxGapMs").toDouble();
        j["stalls"] = t.stalls;
        j["stallSumMs"] = double(t.stallSumMs);
        j["wallS"] = t.wallS;
        QFile f(twinJson);
        CHECK(f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(j).toJson(QJsonDocument::Compact)) > 0,
              "the twin's numbers recorded for the seeded row");
        f.close();
        std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
        return failures ? 1 : 0;
    }

    // ---- THE SEEDED BOOT ---------------------------------------------------------------------------------------
    QFile tf(twinJson);
    const QJsonObject twin = tf.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(tf.readAll()).object() : QJsonObject();
    if (twin.isEmpty() || !QFileInfo::exists(seed + "/cache-manifest.txt")) {
        std::printf("FAIL: no twin (twin.json and the seed are shadercache.seed_boot.twin's; `ctest -R "
                    "shadercache.seed_boot` runs it first)\n");
        return 1;
    }
    const QMap<QString, QByteArray> before = snapshot(seed);
    const Launch s = runApp(app, root + "/seeded", &seed);
    const QJsonObject &r = s.report;
    CHECK(s.rc == 0 && !r.isEmpty(), "the seeded launch booted, ran its script and quit cleanly (rc %d)", s.rc);
    CHECK(sameDir(r.value("seedDir").toString(), seed), "the engine read the seed JAHSHAKA_SHADER_SEED named (%s)",
          qPrintable(r.value("seedDir").toString()));
    CHECK(r.value("seedState").toString() == QStringLiteral("loaded"), "the seed loaded (seedState '%s')",
          qPrintable(r.value("seedState").toString()));
    const int entries = r.value("seedEntries").toInt(0);
    CHECK(entries > 0 && r.value("microcodeLoaded").toBool() && r.value("microcodeEntries").toInt() == entries,
          "the live microcode map is the seed's (%d entries)", entries);
    const int twinCompiled = twin.value("compiled").toInt(0), seededCompiled = r.value("compiledThisRun").toInt(-1);
    CHECK(twinCompiled > 0 && seededCompiled >= 0 && seededCompiled * 20 <= twinCompiled,
          "glslang compiled %d with the seed against the twin's %d cold: at most 5 %% (%d)", seededCompiled, twinCompiled,
          twinCompiled / 20);
    const QMap<QString, QByteArray> after = snapshot(seed);
    CHECK(before == after && before.size() == 2, "the seed came out of the run byte-identical, nothing added (%d file(s))",
          int(after.size()));
    const bool ownLayer = QFileInfo::exists(root + "/seeded/.local/share/Jahshaka/shadercache/microcode.cache");
    std::printf("   the seeded launch's own data root %s after its quit (it saves one only when something "
                "compiled: %d)\n", ownLayer ? "holds a microcode layer" : "holds no microcode layer", seededCompiled);

    std::printf("\n   THE STARTUP GATE, a virgin data root and HOME        unseeded twin     seeded\n");
    std::printf("   the gate's wall (ms)                                 %8.0f   %8lld\n",
                twin.value("gateMs").toDouble(), s.gateMs);
    std::printf("   its longest UI stall (ms, app.heartbeatStats)        %8.0f   %8.0f\n",
                twin.value("maxGapMs").toDouble(), r.value("maxGapMs").toDouble());
    std::printf("   stalls >= 400 ms before the gate's end (n / sum ms)  %3d / %6.0f   %3d / %6lld\n",
                twin.value("stalls").toInt(), twin.value("stallSumMs").toDouble(), s.stalls, s.stallSumMs);
    std::printf("   glslang compiles (compiledThisRun)                   %8d   %8d\n", twinCompiled, seededCompiled);
    std::printf("   the launch's wall (s)                                %8.1f   %8.1f\n\n",
                twin.value("wallS").toDouble(), s.wallS);
    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
