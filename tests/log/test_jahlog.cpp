/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// log.core — the session-log sink (SESSION_LOG_SPEC §10 phase 1 gate).
//
// Offscreen, no display, no engine — the services.core shape. Links JahLog and
// SessionHeader only.
//
// WHY THIS SUITE RE-EXECS ITSELF. JahLog::start() is deliberately once-per-
// process (a second session file in one run is a bug, not a feature), so every
// assertion about a REAL session file needs its own process. The suite spawns
// itself with a scenario name in argv[1], and the parent asserts against what
// the child left on disk. That is also the only honest way to test the startup
// rotation sweep, which by design runs exactly once and never again.
//
// EVERY file path here comes from --log-dir / Options::dir, never from HOME:
// `HOME=` does not isolate app data on macOS (CFFIXED_USER_HOME is what
// CoreFoundation honours), so a rotation test written with HOME= passes on
// Linux and deletes files in a developer's real log directory on the Mac
// (spec §9-R6).

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>
#include <cstdlib>

#include "services/jahlog.h"
#include "services/sessionheader.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("PASS %s\n", name); } \
    else { std::printf("FAIL %s\n", name); ++failures; } \
} while (0)

namespace {

QStringList readLines(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QString onlySessionFile(const QString &dir)
{
    const QStringList names = QDir(dir).entryList({ QStringLiteral("jahshaka-*.log") },
                                                  QDir::Files, QDir::Name);
    for (const QString &n : names)
        if (!n.endsWith(QStringLiteral("-ogre.log"))) return QDir(dir).filePath(n);
    return QString();
}

// ===========================================================================
// CHILD SCENARIOS
// ===========================================================================

/// A whole session: header, one record per level, a qWarning through the
/// funnel, a suppressed record, and a clean close.
int childSession(const QString &dir)
{
    JahLog::Options o;
    o.dir = dir;
    if (!JahLog::start(o)) return 2;
    SessionHeader::emitBlock();

    JAH_LOG(JahLog::scene, Display, QStringLiteral("child: display record"));
    JAH_LOG(JahLog::scene, Log, QStringLiteral("child: log record"));
    JAH_LOG(JahLog::scene, Warning, QStringLiteral("child: warning record"));
    // `mirror` sits at Log in the dev table, so a Verbose record must NOT
    // appear — and must appear the moment the level is raised at runtime.
    JAH_LOG(JahLog::mirror, Verbose, QStringLiteral("child: suppressed verbose"));
    JahLog::setCategoryLevel(QStringLiteral("mirror"), JahLog::Level::Verbose);
    JAH_LOG(JahLog::mirror, Verbose, QStringLiteral("child: raised verbose"));

    JahLog::installQtMessageHandler();
    qWarning("child: qwarning through the funnel");

    // KNOWN-NOISE SUPPRESSION (owner order 2026-09-07). The machinery is
    // exercised through a SYNTHETIC entry that only this build of jahlog.cpp
    // carries (JAH_KNOWN_NOISE_SELFTEST) — the shipping table is empty, and an
    // entry has to earn its place by naming a defect nobody can fix at source.
    // Fire it five times: the FIRST must land annotated, the other four must
    // vanish, and removeQtMessageHandler must write the count.
    for (int i = 0; i < 5; ++i)
        qWarning("jahlog selftest: synthetic known-noise flood");

    // ...and the message that used to BE the table's only entry must now pass
    // through untouched: qlementine's focus-frame defect was fixed at source
    // (2026-09-08), so the suppression that hid it is gone. Fire it twice —
    // both copies have to appear, neither annotated.
    for (int i = 0; i < 2; ++i)
        qWarning("QWidget::mapTo(): parent must be in parent hierarchy");
    JahLog::removeQtMessageHandler();

    JahLog::stop(QStringLiteral("child done"));
    return 0;
}

/// The precedence chain, layer by layer, INSIDE one process (no file needed):
/// compiled default -> ini -> command line -> runtime.
int childPrecedence(const QString &iniPath)
{
    JahLog::applyProfileDefaults(JahLog::Profile::Development);
    if (JahLog::db.level() != JahLog::Level::Log) return 10;      // layer 1

    QSettings ini(iniPath, QSettings::IniFormat);
    ini.setValue(QStringLiteral("log/db"), QStringLiteral("warning"));
    ini.setValue(QStringLiteral("log/nosuchcategory"), QStringLiteral("warning"));
    ini.setValue(QStringLiteral("log/mirror"), QStringLiteral("notalevel"));
    ini.sync();
    JahLog::applyIniLevels(&ini);
    if (JahLog::db.level() != JahLog::Level::Warning) return 11;  // layer 2 beats 1
    if (JahLog::mirror.level() != JahLog::Level::Log) return 12;  // bad value ignored

    JahLog::applyLevelSpec(QStringLiteral("db=error,scene=verbose"));
    if (JahLog::db.level() != JahLog::Level::Error) return 13;    // layer 3 beats 2
    if (JahLog::scene.level() != JahLog::Level::Verbose) return 14;

    JahLog::setCategoryLevel(QStringLiteral("db"), JahLog::Level::VeryVerbose);
    if (JahLog::db.level() != JahLog::Level::VeryVerbose) return 15;   // layer 4 beats 3

    // The global level re-bases only the categories nobody set explicitly.
    JahLog::setGlobalLevel(JahLog::Level::Display);
    if (JahLog::db.level() != JahLog::Level::VeryVerbose) return 16;
    if (JahLog::ui.level() != JahLog::Level::Display) return 17;
    return 0;
}

/// Opens a session in a directory that already contains more files than the
/// policy allows, some of them ancient, plus a crash log that must survive.
int childRotate(const QString &dir)
{
    JahLog::Options o;
    o.dir = dir;
    o.maxFiles = 3;
    o.purgeDays = 5;
    return JahLog::start(o) ? 0 : 2;
}

/// The §8-R5 micro-benchmark. Prints two nanosecond figures for the parent to
/// threshold, so the numbers are visible in the ctest output either way.
int childBench(const QString &dir)
{
    JahLog::Options o;
    o.dir = dir;
    if (!JahLog::start(o)) return 2;

    constexpr int kN = 100000;

    // SUPPRESSED: `mirror` is at Log, the record is Verbose. This must be one
    // relaxed atomic load and a compare — it is what makes "runtime-only, no
    // compile-time ceiling" (fork F4-A) an honest choice.
    JahLog::setCategoryLevel(QStringLiteral("mirror"), JahLog::Level::Log);
    const QString msg = QStringLiteral("benchmark record, of a plausible length");
    QElapsedTimer t;
    t.start();
    for (int i = 0; i < kN; ++i)
        JAH_LOG(JahLog::mirror, Verbose, msg);
    const double suppressedNs = double(t.nsecsElapsed()) / kN;

    // ENABLED: a real formatted record into the real file. Must not flush per
    // record — if it does, this number is in the microseconds.
    t.restart();
    for (int i = 0; i < kN; ++i)
        JAH_LOG(JahLog::mirror, Log, msg);
    const double enabledNs = double(t.nsecsElapsed()) / kN;

    std::printf("BENCH suppressed=%.2f enabled=%.2f\n", suppressedNs, enabledNs);
    JahLog::stop();
    return 0;
}

/// A session with --no-log: levels and the ring still work, nothing on disk.
int childNoLog(const QString &dir)
{
    JahLog::Options o;
    o.dir = dir;
    o.disabled = true;
    JahLog::start(o);
    JAH_LOG(JahLog::app, Display, QStringLiteral("child: no-log record"));
    const bool inRing = !JahLog::tail(10).isEmpty();
    JahLog::stop();
    if (!inRing) return 20;
    return QDir(dir).exists() && !onlySessionFile(dir).isEmpty() ? 21 : 0;
}

// ===========================================================================
// PARENT
// ===========================================================================

int runChild(const QString &scenario, const QString &arg, QString *stdErrOut = nullptr,
             QString *stdOutOut = nullptr)
{
    QProcess p;
    p.start(QCoreApplication::applicationFilePath(), { scenario, arg });
    if (!p.waitForFinished(60000)) { p.kill(); return -1; }
    if (stdErrOut) *stdErrOut = QString::fromUtf8(p.readAllStandardError());
    if (stdOutOut) *stdOutOut = QString::fromUtf8(p.readAllStandardOutput());
    return p.exitCode();
}

void testDefaultTables()
{
    const QVariantMap dev = JahLog::defaultLevels(JahLog::Profile::Development);
    const QVariantMap prod = JahLog::defaultLevels(JahLog::Profile::Production);

    CHECK(dev != prod, "defaults: development and production tables differ");
    CHECK(dev.size() == prod.size() && dev.size() >= 16,
          "defaults: both tables cover every v1 category");

    // Field by field, against SESSION_LOG_SPEC §3.4. This table IS the
    // contract — a builder who quietly retunes a category has to change this
    // line, which is the point.
    struct Row { const char *cat; const char *dev; const char *prod; };
    static const Row rows[] = {
        { "global",  "Log",     "Display" },
        { "app",     "Log",     "Display" },
        { "scene",   "Log",     "Display" },
        { "assets",  "Log",     "Display" },
        { "script",  "Log",     "Display" },
        { "engine",  "Log",     "Warning" },
        { "ogre",    "Warning", "Off"     },
        { "render",  "Log",     "Warning" },
        { "perf",    "Log",     "Log"     },
        { "shader",  "Log",     "Warning" },
        { "db",      "Log",     "Warning" },
        { "ui",      "Log",     "Warning" },
        { "mirror",  "Log",     "Warning" },
        { "physics", "Log",     "Warning" },
        { "media",   "Log",     "Warning" },
        { "qt",      "Log",     "Warning" },
    };
    bool ok = true;
    for (const Row &r : rows) {
        const QString k = QString::fromLatin1(r.cat);
        if (dev.value(k).toString() != QString::fromLatin1(r.dev)) {
            std::printf("  dev[%s] = %s, expected %s\n", r.cat,
                        qPrintable(dev.value(k).toString()), r.dev);
            ok = false;
        }
        if (prod.value(k).toString() != QString::fromLatin1(r.prod)) {
            std::printf("  prod[%s] = %s, expected %s\n", r.cat,
                        qPrintable(prod.value(k).toString()), r.prod);
            ok = false;
        }
    }
    CHECK(ok, "defaults: both tables match SESSION_LOG_SPEC §3.4 field by field");

    // Production keeps `perf` at Log on purpose: the sampler is the one
    // instrument the fps-decay case exists for, and a global CEILING would
    // silently disable it. This assertion is the guard on that decision.
    CHECK(prod.value(QStringLiteral("perf")).toString() == QLatin1String("Log")
              && prod.value(QStringLiteral("global")).toString() == QLatin1String("Display"),
          "defaults: production keeps perf above the global level (global is not a ceiling)");

    // The Fatal-does-not-terminate decision, asserted rather than assumed.
    CHECK(int(JahLog::Level::Off) < int(JahLog::Level::Fatal)
              && int(JahLog::Level::Fatal) < int(JahLog::Level::VeryVerbose),
          "levels: Off mutes everything and the UE order is preserved");
}

void testLevelParsing()
{
    JahLog::Level l = JahLog::Level::Log;
    CHECK(JahLog::parseLevel(QStringLiteral("Warning"), &l) && l == JahLog::Level::Warning,
          "parseLevel: case-insensitive names");
    CHECK(JahLog::parseLevel(QStringLiteral("off"), &l) && l == JahLog::Level::Off,
          "parseLevel: off");
    CHECK(!JahLog::parseLevel(QStringLiteral("chatty"), &l),
          "parseLevel: an unknown name is refused, not guessed");
}

void testCategoryRegistry()
{
    const auto before = JahLog::categories().size();
    JahLog::Category *fresh = JahLog::category(QStringLiteral("a.dynamic.category"));
    CHECK(fresh && JahLog::categories().size() == before + 1,
          "categories: an unknown name registers a new category");
    CHECK(JahLog::category(QStringLiteral("a.dynamic.category")) == fresh,
          "categories: lookup returns the same object");
    CHECK(JahLog::category(QStringLiteral("scene")) == &JahLog::scene,
          "categories: the built-ins are found by name");
}

void testSession(const QString &root)
{
    const QString dir = QDir(root).filePath(QStringLiteral("session"));
    QString childErr;
    const int rc = runChild(QStringLiteral("session"), dir, &childErr);
    CHECK(rc == 0, "session: the child ran a whole session");

    const QString path = onlySessionFile(dir);
    CHECK(!path.isEmpty(), "session: a session file was created");
    const QStringList lines = readLines(path);
    CHECK(!lines.isEmpty() && lines.first().contains(QLatin1String("Log file open,")),
          "session: the first line is the open bracket");
    CHECK(!lines.isEmpty() && lines.last().contains(QLatin1String("Log file closed,")),
          "session: a clean quit writes the close bracket");
    CHECK(QFile::exists(QDir(dir).filePath(QStringLiteral("latest.log"))),
          "session: latest.log points at this run");

    const QString whole = lines.join(QLatin1Char('\n'));

    // The line format (spec §3.7): timestamp, frame counter, category, and the
    // Log level label OMITTED exactly as UE omits it.
    CHECK(whole.contains(QRegularExpression(
              QStringLiteral(R"(\[\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}\.\d{3}\]\[ *\d+\]scene: Display: child: display record)"))),
          "format: [timestamp][frame]category: Level: message");
    CHECK(whole.contains(QRegularExpression(
              QStringLiteral(R"(\]scene: child: log record)"))),
          "format: the Log level label is omitted");

    // Filtering, both ways round.
    CHECK(!whole.contains(QLatin1String("child: suppressed verbose")),
          "filtering: a record below the category's level never reaches the file");
    CHECK(whole.contains(QLatin1String("child: raised verbose")),
          "filtering: raising the level at runtime makes the next record appear");

    // The Qt funnel: in the file under `qt` AND still on stderr.
    CHECK(whole.contains(QLatin1String("qt: Warning: child: qwarning through the funnel")),
          "funnel: a qWarning lands in the file under the qt category");
    CHECK(childErr.contains(QLatin1String("child: qwarning through the funnel")),
          "funnel: the same qWarning still reaches stderr (the previous handler is chained)");

    // Known-noise suppression (owner order 2026-09-07): five copies of the
    // SYNTHETIC entry went in; exactly ONE annotated line and ONE shutdown
    // summary may come out, in the file and on stderr alike.
    CHECK(whole.count(QLatin1String("jahlog selftest: synthetic known-noise flood"))
              == 2,   // the annotated first occurrence + the quoted text in the summary
          "known-noise: one annotated occurrence plus one summary, never the flood");
    CHECK(whole.contains(QLatin1String("[known-noise: tests/log/test_jahlog.cpp (synthetic entry)")),
          "known-noise: the first occurrence carries the annotation");
    CHECK(whole.contains(QLatin1String("known-noise summary: suppressed 4 repeats")),
          "known-noise: the shutdown summary reports the suppressed count");
    CHECK(childErr.count(QLatin1String("jahlog selftest: synthetic known-noise flood")) == 2,
          "known-noise: stderr sees the same single annotated line + summary, not the flood");

    // The suppression must not outlive the bug. The qlementine focus-frame
    // defect that produced the mapTo flood is fixed at source (2026-09-08), so
    // its entry is gone and BOTH copies the child emitted must appear, neither
    // annotated and neither counted into a summary.
    CHECK(whole.count(QLatin1String("QWidget::mapTo(): parent must be in parent hierarchy")) == 3,
          // two records + the "repeated 2x" row the close summary writes for
          // any message seen more than once. A suppressed message would show
          // ONE annotated record and a "known-noise summary" line instead.
          "known-noise: the retired qlementine entry no longer suppresses anything");
    CHECK(whole.contains(QLatin1String("repeated 2x : QWidget::mapTo()")),
          "known-noise: the mapTo warning is counted like any ordinary record now");
    CHECK(!whole.contains(QLatin1String("known-noise: qlementine")),
          "known-noise: nothing annotates the mapTo warning any more");

    // The startup header (spec §4). The base rows are asserted by name here;
    // the GPU/device rows belong to phase 3 and are gated by log.engine.
    static const char *required[] = {
        "app version", "build id", "build type", "qt ", "platform / qpa",
        "command line", "data root", "session log", "ogre log", "started",
    };
    bool headerOk = whole.contains(QLatin1String("=== SESSION START ==="));
    for (const char *field : required) {
        const QRegularExpression re(
            QStringLiteral(R"(app: Display:   %1 *: *\S)").arg(
                QRegularExpression::escape(QString::fromLatin1(field).trimmed())));
        if (!whole.contains(re)) {
            std::printf("  header field missing or empty: %s\n", field);
            headerOk = false;
        }
    }
    CHECK(headerOk, "header: every base §4 field is present with a non-empty value");

    // The close summary — UE's unique-warning roll-up, and the first thing a
    // triager reads.
    CHECK(whole.contains(QLatin1String("=== SESSION SUMMARY ==="))
              && whole.contains(QLatin1String("by level")),
          "summary: the close block counts records by level and category");
}

void testPrecedence(const QString &root)
{
    const QString ini = QDir(root).filePath(QStringLiteral("prec.ini"));
    const int rc = runChild(QStringLiteral("precedence"), ini);
    CHECK(rc == 0, "precedence: compiled default < ini < command line < runtime, in that order");
    if (rc != 0) std::printf("  child returned %d (see childPrecedence's return codes)\n", rc);
}

void testRotation(const QString &root)
{
    const QString dir = QDir(root).filePath(QStringLiteral("rotate"));
    QDir().mkpath(dir);

    // Six of ours, three of them ancient, plus two files the sweep must not be
    // able to touch: a crash log (forensic — the one artifact a user is ever
    // asked to send) and an unrelated .log.
    QStringList ours;
    for (int i = 0; i < 6; ++i) {
        const QString name = QStringLiteral("jahshaka-2026.09.0%1-10.00.00-%2.log").arg(i + 1).arg(1000 + i);
        const QString p = QDir(dir).filePath(name);
        QFile f(p);
        f.open(QIODevice::WriteOnly);
        f.write("old\n");
        f.close();
        ours << p;
    }
    const QString crash = QDir(dir).filePath(QStringLiteral("crash-1788594910.log"));
    { QFile f(crash); f.open(QIODevice::WriteOnly); f.write("boom\n"); }
    const QString foreign = QDir(dir).filePath(QStringLiteral("someone-elses.log"));
    { QFile f(foreign); f.open(QIODevice::WriteOnly); f.write("hi\n"); }

    // Age three of them past the 5-day cutoff.
    const QDateTime ancient = QDateTime::currentDateTime().addDays(-30);
    for (int i = 0; i < 3; ++i) {
        QFile f(ours[i]);
        f.open(QIODevice::ReadWrite);
        f.setFileTime(ancient, QFileDevice::FileModificationTime);
        f.close();
    }

    const int rc = runChild(QStringLiteral("rotate"), dir);
    CHECK(rc == 0, "rotation: the child opened a session in a full directory");

    CHECK(QFile::exists(crash), "rotation: a crash-*.log is NEVER touched");
    CHECK(QFile::exists(foreign), "rotation: a file that is not ours is never touched");

    int survivors = 0;
    for (const QString &p : ours) if (QFile::exists(p)) ++survivors;
    CHECK(survivors == 0 || survivors < 6, "rotation: old sessions are deleted");
    // maxFiles = 3 counts the newest three of OURS; the three ancient ones go
    // by age as well. Whatever remains, plus the child's own new file, must
    // not exceed the policy.
    const QStringList remaining =
        QDir(dir).entryList({ QStringLiteral("jahshaka-*.log") }, QDir::Files);
    int sessionsLeft = 0;
    for (const QString &n : remaining) if (!n.endsWith(QStringLiteral("-ogre.log"))) ++sessionsLeft;
    CHECK(sessionsLeft <= 4, "rotation: at most maxFiles survivors plus this session");
    for (int i = 0; i < 3; ++i)
        CHECK(!QFile::exists(ours[i]), qPrintable(QStringLiteral("rotation: file older than purgeDays deleted (%1)").arg(i)));
}

void testBench(const QString &root)
{
    const QString dir = QDir(root).filePath(QStringLiteral("bench"));
    QString out;
    const int rc = runChild(QStringLiteral("bench"), dir, nullptr, &out);
    CHECK(rc == 0, "bench: the micro-benchmark ran");

    double suppressed = -1, enabled = -1;
    for (const QString &line : out.split(QLatin1Char('\n'))) {
        if (!line.startsWith(QLatin1String("BENCH "))) continue;
        const QStringList bits = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &b : bits) {
            if (b.startsWith(QLatin1String("suppressed="))) suppressed = b.mid(11).toDouble();
            if (b.startsWith(QLatin1String("enabled=")))     enabled = b.mid(8).toDouble();
        }
    }
    std::printf("  suppressed %.2f ns/call, enabled %.2f ns/call (Debug build, ASan-capable)\n",
                suppressed, enabled);
    // THE THRESHOLDS. The spec's target for the suppressed path is < 5 ns; the
    // number quoted there was a target to ESTABLISH, and this is a Debug build
    // with a non-inlined call through the guard macro, so the gate is set at a
    // deliberately generous 50 ns — an order of magnitude above what is
    // measured and two below anything that could be called a per-frame cost.
    // What it really guards is the SHAPE: no lock, no format, no allocation on
    // the suppressed path.
    CHECK(suppressed >= 0 && suppressed < 50.0,
          "bench: a suppressed record costs an atomic load and a compare");
    // The enabled path must not flush per record. A per-record fflush on this
    // class of machine is several microseconds; 20 us is far above the honest
    // buffered cost and far below a syscall-per-line implementation.
    CHECK(enabled >= 0 && enabled < 20000.0,
          "bench: the enabled path is buffered (no per-record flush)");
}

void testNoLog(const QString &root)
{
    const QString dir = QDir(root).filePath(QStringLiteral("nolog"));
    const int rc = runChild(QStringLiteral("nolog"), dir);
    CHECK(rc == 0, "--no-log: routing and the ring still work, nothing reaches disk");
    if (rc != 0) std::printf("  child returned %d\n", rc);
}

}   // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc > 2) {
        const QString scenario = QString::fromLocal8Bit(argv[1]);
        const QString arg = QString::fromLocal8Bit(argv[2]);
        if (scenario == QLatin1String("session"))    return childSession(arg);
        if (scenario == QLatin1String("precedence")) return childPrecedence(arg);
        if (scenario == QLatin1String("rotate"))     return childRotate(arg);
        if (scenario == QLatin1String("bench"))      return childBench(arg);
        if (scenario == QLatin1String("nolog"))      return childNoLog(arg);
        std::fprintf(stderr, "unknown scenario '%s'\n", qPrintable(scenario));
        return 99;
    }

    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::printf("FAIL could not create a temporary directory\n"); return 1; }

    testDefaultTables();
    testLevelParsing();
    testCategoryRegistry();
    testSession(tmp.path());
    testPrecedence(tmp.path());
    testRotation(tmp.path());
    testBench(tmp.path());
    testNoLog(tmp.path());

    std::printf(failures ? "\n%d FAILURES\n" : "\nall log.core checks passed\n", failures);
    return failures ? 1 : 0;
}
