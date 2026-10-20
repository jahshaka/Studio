// bugs.gi_open_frame (BUGS-6 stage 0) — the driver of tests/bugs/gi_open_frame.js (its header has the
// law and the derivation of the frame count). Spawns the REAL binary into the row's warm home, reads the
// script's markers and, between them, the engine's own lines:
//   BUGS6_CREATE_BEGIN .. BUGS6_WATCHEND   the create and the frames after it: every Photon/Scroll* clone
//                                           named there is a scroll target made across the create (c);
//   BUGS6_WATCH .. BUGS6_WATCHEND           the frames after the create returned: every HlmsCompute
//                                           "compiles a permutation in the frame" there is a compile on a
//                                           presented frame (b); the script's own report carries (a).
// The longest of those frames is PRINTED, never barred (a measure is the lead's row, not this guard's).
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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QProcess proc;
    proc.setProcessEnvironment(QProcessEnvironment::systemEnvironment());   // HOME / the data root: the row's
    proc.setWorkingDirectory(QDir::currentPath());
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QStringLiteral(JAHSHAKA_BINARY),
               QStringList() << "--script"
                             << QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/bugs/gi_open_frame.js"));
    if (!proc.waitForStarted(20000)) { std::printf("FAIL: app did not start\n"); return 1; }
    if (!proc.waitForFinished(55000)) {
        std::printf("FAIL: app did not exit inside the row's 60 s\n");
        proc.kill(); proc.waitForFinished(5000); return 1;
    }
    const QString out = QString::fromUtf8(proc.readAll());
    QJsonObject r;
    enum { Before, Creating, Watching, After } phase = Before;
    QStringList compiles, clones, pipelines;
    int lastCloneBefore = 0;
    static const QRegularExpression cloneRe(QStringLiteral("Photon/Scroll(Copy|Position)/(\\d+)"));
    for (const QString &line : out.split('\n')) {
        if (line.contains(QStringLiteral("BUGS6_CREATE_BEGIN"))) { phase = Creating; continue; }
        if (line.contains(QStringLiteral("BUGS6_WATCHEND"))) { phase = After; continue; }
        if (line.contains(QStringLiteral("BUGS6_WATCH"))) { phase = Watching; continue; }
        const int at = line.indexOf(QStringLiteral("BUGS6 {"));
        if (at >= 0) { r = QJsonDocument::fromJson(line.mid(at + 6).toUtf8()).object(); continue; }
        const QRegularExpressionMatch m = cloneRe.match(line);
        if (m.hasMatch()) {
            if (phase == Before) lastCloneBefore = std::max(lastCloneBefore, m.captured(2).toInt());
            else if (phase == Creating || phase == Watching) clones.append(line.trimmed());
        }
        if (phase == Watching && line.contains(QStringLiteral("compiles a permutation in the frame")))
            compiles.append(line.trimmed());
        if ((phase == Creating || phase == Watching) && line.contains(QStringLiteral("HlmsCompute: job")))
            pipelines.append(line.trimmed());
    }
    CHECK(!r.isEmpty(), "the run reported");
    if (r.isEmpty()) { std::printf("%s\n", qPrintable(out.right(4000))); return 1; }
    const QJsonObject atReveal = r.value("atReveal").toObject(), after = r.value("after").toObject();
    std::printf("    gi at the reveal: mode %s bound %d rebuilds %d; after %d frames: rebuilds %d (per frame %s)\n",
                qPrintable(atReveal.value("mode").toString()), atReveal.value("vctBound").toBool(),
                atReveal.value("rebuilds").toInt(), int(r.value("frames").toString().count(',') + 1),
                after.value("rebuilds").toInt(), qPrintable(r.value("frames").toString()));
    std::printf("    the open's longest frame after the reveal: %d ms (frame %d) — printed, not barred\n",
                r.value("worstFrameMs").toInt(), r.value("worstFrameAt").toInt());
    std::printf("    HlmsCompute lines across the create: %d; scroll clones %d (the last id before it %d); "
                "compiles after the reveal %d\n", int(pipelines.size()), int(clones.size()), lastCloneBefore,
                int(compiles.size()));
    CHECK(atReveal.value("vctBound").toBool(), "(a) the lighting arm is bound when the create returns");
    CHECK(after.value("rebuilds").toInt() == atReveal.value("rebuilds").toInt(),
          "(a) no arm rebuild on the frames after the reveal");
    CHECK(compiles.isEmpty(), "(b) no compute permutation compiled on a frame after the reveal");
    for (const QString &c : compiles) std::printf("      %s\n", qPrintable(c.left(200)));
    CHECK(clones.isEmpty(), "(c) no scroll target cloned across the create (the targets were kept)");
    for (int i = 0; i < clones.size() && i < 6; ++i) std::printf("      %s\n", qPrintable(clones[i].left(200)));
    std::printf("%s (%d failure(s))\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
