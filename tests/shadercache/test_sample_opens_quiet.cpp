// shader.sample_opens_quiet (ASYNC-SHADERS-1) — open every shipped sample on a cold shader cache
// (one launch, the samples in order, the background compiler on as in an interactive session):
// after each open's own window nothing compiles on the UI thread. The owner's smoke caught four
// ~2 s stalls after sample opens — a GI rebuild's compute permutations and the PBS set that
// changes when the lighting arm binds — and this is the row that says they are gone.
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString home = QDir::current().absoluteFilePath(QStringLiteral("e2e-home-sample-opens"));
    QDir(home).removeRecursively();            // a cold cache, an empty library
    QDir().mkpath(home + "/run");
    QDir().mkpath(home + "/cache");
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HOME", home);
    env.insert("JAHSHAKA_DATA_ROOT", home + "/.local/share/Jahshaka");
    env.insert("XDG_CACHE_HOME", home + "/cache");
    proc.setProcessEnvironment(env);
    proc.setWorkingDirectory(home + "/run");
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QStringLiteral(JAHSHAKA_BINARY),
               QStringList() << "--script"
                             << QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR
                                               "/tests/shadercache/scripts/e2e_sample_opens_quiet.js"));
    if (!proc.waitForStarted(20000)) { std::printf("FAIL: app did not start\n"); return 1; }
    if (!proc.waitForFinished(1700000)) {
        std::printf("FAIL: app did not exit\n"); proc.kill(); proc.waitForFinished(5000); return 1;
    }
    const QString out = QString::fromUtf8(proc.readAll());
    QJsonObject r;
    // THE OFFENDERS, PER SAMPLE: what the engine said it built on the main thread inside each
    // sample's watch window (Hlms "built in the frame", HlmsCompute "compiles a permutation
    // in the frame", the compiler's "MAIN-THREAD BUILD of a queued permutation", and the raw
    // shader compiles beside them). Printed on that sample's FAIL line.
    QMap<QString, QStringList> offenders;
    QStringList offStacks;
    int stackLeft = 0;
    QString watching;
    for (const QString &line : out.split('\n')) {
        const int at = line.indexOf(QStringLiteral("SAMPLEOPENS "));
        if (at >= 0) r = QJsonDocument::fromJson(line.mid(at + 12).toUtf8()).object();
        if (line.contains(QStringLiteral("[shader] ")) && line.contains(QStringLiteral("SHADER-WARM-2")))
            std::printf("    app: %s\n", qPrintable(line.trimmed()));
        const int w = line.indexOf(QStringLiteral("SAMPLEOPENS_WATCH "));
        if (w >= 0) { watching = line.mid(w + 18).trimmed(); continue; }
        if (line.contains(QStringLiteral("SAMPLEOPENS_WATCHEND "))) { watching.clear(); continue; }
        // The engine's own stack for a compile made off the background compiler while a view
        // compiles in the background (OgreShaderCache's counter): the line and its frames.
        if (line.contains(QStringLiteral("OFF-SERVICE COMPILE"))) {
            offStacks.append(QStringLiteral("[%1] ").arg(watching.isEmpty() ? QStringLiteral("-") : watching) +
                             line.trimmed());
            stackLeft = 24;
            continue;
        }
        if (stackLeft > 0) {
            --stackLeft;
            offStacks.append(QStringLiteral("    ") + line.trimmed().left(220));
        }
        if (!watching.isEmpty() &&
            (line.contains(QStringLiteral("built in the frame")) ||
             line.contains(QStringLiteral("compiles a permutation in the frame")) ||
             line.contains(QStringLiteral("MAIN-THREAD BUILD")) ||
             (line.startsWith(QStringLiteral("Shader ")) &&
              line.contains(QStringLiteral(" compiled successfully")))))
            offenders[watching].append(line.trimmed());
    }
    CHECK(!r.isEmpty(), "the run reported");
    if (r.isEmpty()) { std::printf("%s\n", qPrintable(out.right(4000))); return 1; }
    const QJsonArray samples = r.value("samples").toArray();
    CHECK(samples.size() >= 20, "every shipped sample was opened");
    for (const QJsonValue &v : samples) {
        const QJsonObject s = v.toObject();
        const QByteArray name = s.value("name").toString().toUtf8();
        std::printf("    %-24s ok %d  live before the open's window %d  after it %d\n", name.constData(),
                    s.value("ok").toBool(), s.value("beforeOpen").toInt(), s.value("after").toInt());
        CHECK(s.value("ok").toBool(), ("opened: " + name).constData());
        const bool quiet = s.value("after").toInt() == 0 && s.value("beforeOpen").toInt() == 0;
        CHECK(quiet, ("no UI-thread compile around the open of " + name).constData());
        if (!quiet)
            for (const QString &o : offenders.value(QString::fromUtf8(name)))
                std::printf("      offender (%s): %s\n", name.constData(), qPrintable(o.left(300)));
    }
    CHECK(r.value("live").toInt() == 0, "liveCompiles is 0 for the whole session");
    if (r.value("live").toInt() != 0)
        for (const QString &l : offStacks) std::printf("      %s\n", qPrintable(l));
    CHECK(r.value("async").toObject().value("failed").toInt() == 0, "no permutation failed to build");
    std::printf("%s (%d failure(s))\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
