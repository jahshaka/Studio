// shader.async_soak (ASYNC-SHADERS-1) — the background compiler under use with the Vulkan
// validation layer on: 50 material applies while flying, a blocking render that meets a
// pending permutation, a scene switch with compiles pending, a quit with compiles pending
// (scripts/e2e_async_soak.js). Asserts a clean exit, the layer loaded and silent, no failed
// permutation, no UI-thread compile from the asynchronous view.
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString home = QDir::current().absoluteFilePath(QStringLiteral("e2e-home-async-soak"));
    QDir(home).removeRecursively();
    QDir().mkpath(home + "/run");
    QDir().mkpath(home + "/cache");
    QDir().mkpath(home + "/out");
    QFile in(QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/shadercache/scripts/e2e_async_soak.js"));
    if (!in.open(QIODevice::ReadOnly)) { std::printf("FAIL: no script\n"); return 1; }
    const QString script = home + "/run/async_soak.js";
    {
        QFile f(script);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(QStringLiteral("var OUTDIR = \"%1\";\n").arg(home + "/out").toUtf8());
        f.write(in.readAll());
    }
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HOME", home);
    env.insert("JAHSHAKA_DATA_ROOT", home + "/.local/share/Jahshaka");
    env.insert("XDG_CACHE_HOME", home + "/cache");
    // The layer through the loader's own switch (the legacy VK_INSTANCE_LAYERS does not load it
    // on this loader — DOCS/traps/GATE_AND_RIG.md).
    env.insert("VK_LOADER_LAYERS_ENABLE", "VK_LAYER_KHRONOS_validation");
    proc.setProcessEnvironment(env);
    proc.setWorkingDirectory(home + "/run");
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QStringLiteral(JAHSHAKA_BINARY), QStringList() << "--script" << script);
    if (!proc.waitForStarted(20000)) { std::printf("FAIL: app did not start\n"); return 1; }
    if (!proc.waitForFinished(1100000)) {
        std::printf("FAIL: app did not exit\n"); proc.kill(); proc.waitForFinished(5000); return 1;
    }
    const QString out = QString::fromUtf8(proc.readAll());
    QJsonObject r;
    int validation = 0;
    for (const QString &line : out.split('\n')) {
        const int at = line.indexOf(QStringLiteral("AS1SOAK "));
        if (at >= 0) r = QJsonDocument::fromJson(line.mid(at + 8).toUtf8()).object();
        if (line.contains(QStringLiteral("VUID-")) || line.contains(QStringLiteral("Validation Error"))) {
            if (validation++ < 10) std::printf("    app: %s\n", qPrintable(line.trimmed().left(300)));
        }
    }
    CHECK(proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0,
          "the app exits cleanly (it quit with compiles pending)");
    CHECK(out.contains(QStringLiteral("Found instance layer: VK_LAYER_KHRONOS_validation")),
          "the validation layer was loaded");
    std::printf("    validation messages: %d\n", validation);
    CHECK(validation == 0, "no validation message");
    CHECK(!r.isEmpty(), "the run reported");
    if (r.isEmpty()) { std::printf("%s\n", qPrintable(out.right(4000))); return 1; }
    const QJsonObject st = r.value("stats").toObject(), after = r.value("after").toObject(),
                      start = r.value("start").toObject();
    // The soak's own draws (the session's counters minus the startup gate's sweep).
    const double ownPlaceholders = st.value("placeholderDraws").toDouble() -
                                   start.value("placeholderDraws").toDouble();
    std::printf("    applied %d  maxPending %d  completed %d  placeholderDraws %lld  live %d  "
                "pending at the shot %d  at the switch %d\n",
                r.value("applied").toInt(), r.value("maxPending").toInt(), st.value("completed").toInt(),
                (long long)ownPlaceholders, r.value("live").toInt(),
                r.value("pendingAtShot").toInt(), r.value("pendingAtSwitch").toInt());
    CHECK(r.value("applied").toInt() == 50, "50 material applies");
    CHECK(st.value("completed").toInt() > start.value("completed").toInt() && ownPlaceholders > 0,
          "the background compiler built permutations and the view drew placeholders");
    CHECK(r.value("live").toInt() == 0, "no UI-thread compile while the asynchronous view flew");
    CHECK(QFileInfo::exists(r.value("shot").toObject().value("path").toString()),
          "the blocking screenshot that met a pending permutation rendered");
    CHECK(after.value("failed").toInt() == 0, "no permutation failed to build");
    CHECK(after.value("pending").toInt() == 0, "everything pending across the scene switch landed");
    std::printf("%s (%d failure(s))\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
