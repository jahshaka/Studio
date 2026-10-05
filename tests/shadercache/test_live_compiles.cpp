// shader.live_compiles — a compile on the UI thread after the splash is a named defect
// (SHADER-WARM-2; services/livecompiles.h). Two launches into a scratch home: COLD (the
// shader cache wiped — the owner's first launch after a driver update) and WARM. Each
// creates a Basic and a World project, renders their frames and re-opens the first; the
// script prints app.shaderCache().liveCompiles per phase and this asserts every phase 0.
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

QJsonObject runApp(const QString &home, const QString &script)
{
    QProcess app;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HOME", home);
    env.insert("JAHSHAKA_DATA_ROOT", home + "/.local/share/Jahshaka");
    env.insert("XDG_CACHE_HOME", home + "/cache");
    app.setProcessEnvironment(env);
    app.setWorkingDirectory(home + "/run");
    app.setProcessChannelMode(QProcess::MergedChannels);
    app.start(QStringLiteral(JAHSHAKA_BINARY), QStringList() << "--script" << script);
    if (!app.waitForStarted(20000)) { std::printf("FAIL: app did not start\n"); ++failures; return {}; }
    if (!app.waitForFinished(300000)) {
        std::printf("FAIL: app did not exit\n"); ++failures; app.kill(); app.waitForFinished(5000);
        return {};
    }
    const QString out = QString::fromUtf8(app.readAll());
    QJsonObject last;
    for (const QString &line : out.split('\n')) {
        const int at = line.indexOf(QStringLiteral("LIVECOMPILES "));
        if (at >= 0) last = QJsonDocument::fromJson(line.mid(at + 13).toUtf8()).object();
        // The named defect, as the log says it — printed so a red names its stage.
        if (line.contains(QStringLiteral("[shader] ")) && line.contains(QStringLiteral("SHADER-WARM-2")))
            std::printf("    app: %s\n", qPrintable(line.trimmed()));
    }
    if (last.isEmpty()) std::printf("---- app output ----\n%s\n--------------------\n", qPrintable(out));
    return last;
}

void assertRun(const char *name, const QJsonObject &r)
{
    CHECK(!r.isEmpty(), name);
    for (const QJsonValue &v : r.value("phases").toArray()) {
        const QJsonObject p = v.toObject();
        std::printf("    %-8s %-20s live %d  compiled %d\n", name,
                    qPrintable(p.value("phase").toString()), p.value("live").toInt(),
                    p.value("compiled").toInt());
        const QByteArray msg = QByteArray(name) + ": no UI-thread compile in '" +
                               p.value("phase").toString().toUtf8() + "'";
        CHECK(p.value("live").toInt() == 0, msg.constData());
    }
    CHECK(r.value("live").toInt() == 0, "liveCompiles is 0 for the whole session");
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString home = QDir::current().absoluteFilePath(QStringLiteral("e2e-home-livecompiles"));
    QDir(home).removeRecursively();            // a cold cache, an empty library
    QDir().mkpath(home + "/run");
    QDir().mkpath(home + "/cache");
    const QString script = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR) +
                           QStringLiteral("/tests/shadercache/scripts/e2e_live_compiles.js");

    const QJsonObject cold = runApp(home, script);
    assertRun("cold", cold);
    CHECK(cold.value("compiledAtStart").toInt() > 0, "the cold launch compiled its global set behind the splash");

    const QJsonObject warm = runApp(home, script);
    assertRun("warm", warm);

    std::printf("%s (%d failure(s))\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
