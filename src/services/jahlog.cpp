/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/jahlog.h"

#include "irisgl/core/logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVector>

#include <cstdio>

namespace JahLog {

// ---------------------------------------------------------------------------
// Categories
// ---------------------------------------------------------------------------

namespace {

/// The category registry. A plain vector behind a mutex: registration happens
/// during static construction and (rarely) when the Qt funnel meets a category
/// name it has not seen; lookup is not on any hot path — isActive() on the
/// Category object is, and that touches nothing here.
QMutex &registryMutex()
{
    static QMutex m;
    return m;
}
QVector<Category *> &registryList()
{
    static QVector<Category *> v;
    return v;
}

std::atomic<int> gGlobalLevel { int(Level::Log) };

}   // namespace

Category::Category(const char *name) : mName(name)
{
    QMutexLocker lock(&registryMutex());
    registryList().append(this);
}

// The v1 set (spec §3.3). Order is the order log.categories() reports.
Category app("app");
Category scene("scene");
Category engine("engine");
Category ogre("ogre");
Category render("render");
Category perf("perf");
Category shader("shader");
Category assets("assets");
Category db("db");
Category script("script");
Category ui("ui");
Category mirror("mirror");
Category physics("physics");
Category media("media");
Category qt("qt");
Category legacy("legacy");

Category *category(const QString &name)
{
    const QByteArray key = name.toUtf8();
    {
        QMutexLocker lock(&registryMutex());
        for (Category *c : registryList())
            if (key == c->name()) return c;
    }
    // Not found: create it. The name has to outlive the Category (which is
    // never destroyed), so the byte array is leaked deliberately.
    char *owned = qstrdup(key.constData());
    Category *fresh = new Category(owned);   // registers itself
    fresh->setLevel(globalLevel());
    fresh->setDefaultLevel(globalLevel());
    return fresh;
}

QList<Category *> categories()
{
    QMutexLocker lock(&registryMutex());
    return QList<Category *>(registryList().cbegin(), registryList().cend());
}

// ---------------------------------------------------------------------------
// Levels
// ---------------------------------------------------------------------------

QString levelName(Level l)
{
    switch (l) {
    case Level::Off:         return QStringLiteral("Off");
    case Level::Fatal:       return QStringLiteral("Fatal");
    case Level::Error:       return QStringLiteral("Error");
    case Level::Warning:     return QStringLiteral("Warning");
    case Level::Display:     return QStringLiteral("Display");
    case Level::Log:         return QStringLiteral("Log");
    case Level::Verbose:     return QStringLiteral("Verbose");
    case Level::VeryVerbose: return QStringLiteral("VeryVerbose");
    }
    return QStringLiteral("Log");
}

bool parseLevel(const QString &text, Level *out)
{
    const QString t = text.trimmed().toLower();
    Level l = Level::Log;
    if      (t == QLatin1String("off"))          l = Level::Off;
    else if (t == QLatin1String("none"))         l = Level::Off;
    else if (t == QLatin1String("fatal"))        l = Level::Fatal;
    else if (t == QLatin1String("error"))        l = Level::Error;
    else if (t == QLatin1String("warning"))      l = Level::Warning;
    else if (t == QLatin1String("warn"))         l = Level::Warning;
    else if (t == QLatin1String("display"))      l = Level::Display;
    else if (t == QLatin1String("log"))          l = Level::Log;
    else if (t == QLatin1String("verbose"))      l = Level::Verbose;
    else if (t == QLatin1String("veryverbose"))  l = Level::VeryVerbose;
    else if (t == QLatin1String("very_verbose")) l = Level::VeryVerbose;
    else return false;
    if (out) *out = l;
    return true;
}

// ---------------------------------------------------------------------------
// The default tables (spec §3.4)
// ---------------------------------------------------------------------------
//
// Development is the VERBOSE one: the Debug build is the daily driver (owner),
// so the tool that has to earn its keep there is the one that says the most.
// Production cuts VERBOSITY, never the artifact — we deliberately do NOT copy
// UE's NO_LOGGING strip, because a shipped Jahshaka's users file bugs and the
// file has to contain the header, the scene blocks and every warning.

QVariantMap defaultLevels(Profile p)
{
    QVariantMap m;
    const bool dev = (p == Profile::Development);
    auto put = [&m](const char *k, Level l) { m.insert(QString::fromLatin1(k), levelName(l)); };

    put("global",  dev ? Level::Log : Level::Display);
    put("app",     dev ? Level::Log : Level::Display);
    put("scene",   dev ? Level::Log : Level::Display);
    put("assets",  dev ? Level::Log : Level::Display);
    put("script",  dev ? Level::Log : Level::Display);
    put("engine",  dev ? Level::Log : Level::Warning);
    put("ogre",    dev ? Level::Warning : Level::Off);
    put("render",  dev ? Level::Log : Level::Warning);
    put("perf",    Level::Log);
    put("shader",  dev ? Level::Log : Level::Warning);
    put("db",      dev ? Level::Log : Level::Warning);
    put("ui",      dev ? Level::Log : Level::Warning);
    put("mirror",  dev ? Level::Log : Level::Warning);
    put("physics", dev ? Level::Log : Level::Warning);
    put("media",   dev ? Level::Log : Level::Warning);
    put("qt",      dev ? Level::Log : Level::Warning);
    put("legacy",  dev ? Level::Log : Level::Warning);
    return m;
}

Profile buildProfile()
{
#ifdef QT_DEBUG
    return Profile::Development;
#else
    return Profile::Production;
#endif
}

Level globalLevel() { return static_cast<Level>(gGlobalLevel.load(std::memory_order_relaxed)); }

void setGlobalLevel(Level l, bool explicitOverride)
{
    gGlobalLevel.store(int(l), std::memory_order_relaxed);
    // The global level is the DEFAULT for categories nobody retuned — not a
    // ceiling. A ceiling would clamp `perf` to Display in production and
    // silently disable the sampler that this whole program exists for.
    for (Category *c : categories()) {
        if (!c->isExplicit()) {
            c->setLevel(l);
            if (!explicitOverride) c->setDefaultLevel(l);
        }
    }
}

bool setCategoryLevel(const QString &name, Level l, bool explicitOverride)
{
    if (name.compare(QLatin1String("global"), Qt::CaseInsensitive) == 0) {
        setGlobalLevel(l, explicitOverride);
        return true;
    }
    const QByteArray key = name.toUtf8();
    QMutexLocker lock(&registryMutex());
    for (Category *c : registryList()) {
        if (key == c->name()) {
            c->setLevel(l);
            if (explicitOverride) c->setExplicit(true);
            else                  c->setDefaultLevel(l);
            return true;
        }
    }
    return false;
}

/// Layer 1 of the precedence chain. Applied inside start(), before the file is
/// even open, so nothing can be emitted at a level the table did not authorise.
void applyProfileDefaults(Profile p)
{
    const QVariantMap table = defaultLevels(p);
    Level g = Level::Log;
    parseLevel(table.value(QStringLiteral("global")).toString(), &g);
    gGlobalLevel.store(int(g), std::memory_order_relaxed);
    for (Category *c : categories()) {
        const QString name = QString::fromLatin1(c->name());
        Level l = g;
        if (table.contains(name)) parseLevel(table.value(name).toString(), &l);
        c->setLevel(l);
        c->setDefaultLevel(l);
        c->setExplicit(false);
    }
}

// ---------------------------------------------------------------------------
// The sink
// ---------------------------------------------------------------------------

namespace {

/// Re-entrancy / duplication guard, thread-local because it is per call stack.
///
/// TWO jobs. (1) The iris::Logger forwarder raises it around its qInfo/qWarning
/// so the funnel keeps writing to stderr but does NOT put the same line in the
/// file a second time. (2) Our own locked sections raise it so that a Qt
/// warning emitted from inside them (QFile can complain) chains to stderr
/// instead of re-entering the sink and deadlocking on a non-recursive mutex.
thread_local int tlBypass = 0;

struct BypassGuard
{
    BypassGuard() { ++tlBypass; }
    ~BypassGuard() { --tlBypass; }
};

struct Record
{
    quint64 seq = 0;
    const char *cat = nullptr;
    Level level = Level::Log;
    QString text;
};

/// The last N formatted records, drop-oldest. ~400 KB at 2,000 entries, which
/// is what makes log.tail()/log.since() answer without re-reading the file.
constexpr int kRingSize = 2000;
/// Flush every N records even when nothing crossed the Warning threshold.
constexpr int kFlushEveryRecords = 64;
/// Cap on distinct messages tracked for the "top repeats" summary. A flooding
/// message keeps counting; a flood of DISTINCT messages stops adding keys
/// rather than growing without bound.
constexpr int kMaxRepeatKeys = 2000;

class FlushTimer : public QObject
{
public:
    FlushTimer() : QObject(nullptr)
    {
        mTimer = new QTimer(this);
        mTimer->setInterval(1000);
        connect(mTimer, &QTimer::timeout, this, [] { JahLog::flush(); });
        mTimer->start();
    }
private:
    QTimer *mTimer = nullptr;
};

struct SinkState
{
    QMutex mutex;
    QFile *file = nullptr;
    QByteArray pending;
    int sinceFlush = 0;
    bool started = false;
    bool disabled = false;
    QString dir;
    QString sessionPath;
    QString ogrePath;
    QString latestPath;
    QString timestamps = QStringLiteral("local");
    QElapsedTimer sinceStart;
    std::atomic<int> fd { -1 };
    std::atomic<quint64> frame { 0 };
    quint64 seq = 0;
    quint64 dropped = 0;
    QVector<Record> ring;
    int ringNext = 0;
    QHash<QString, quint64> byCategory;
    QHash<QString, quint64> byLevel;
    QHash<QString, quint64> repeats;
    FlushTimer *timer = nullptr;
};

SinkState &sink()
{
    static SinkState s;
    return s;
}

/// Caller holds the mutex.
void flushLocked()
{
    SinkState &s = sink();
    if (!s.file || s.pending.isEmpty()) { s.sinceFlush = 0; return; }
    s.file->write(s.pending);
    s.file->flush();
    s.pending.clear();
    s.sinceFlush = 0;
}

QString stampNow(const QString &mode, const QElapsedTimer &since)
{
    if (mode == QLatin1String("none")) return QString();
    if (mode == QLatin1String("sinceStart")) {
        const qint64 ms = since.isValid() ? since.elapsed() : 0;
        return QStringLiteral("%1.%2")
            .arg(ms / 1000, 6, 10, QLatin1Char('0'))
            .arg(ms % 1000, 3, 10, QLatin1Char('0'));
    }
    const QDateTime now = (mode == QLatin1String("utc"))
                              ? QDateTime::currentDateTimeUtc()
                              : QDateTime::currentDateTime();
    return now.toString(QStringLiteral("yyyy.MM.dd-HH.mm.ss.zzz"));
}

/// `[2026.09.06-14.22.31.418][ 1041]scene: Display: opened 'Modern Room'…`
/// The `Log` level label is omitted, exactly as UE omits it.
QString formatRecord(const SinkState &s, const Category &cat, Level level, const QString &message)
{
    QString line;
    const QString ts = stampNow(s.timestamps, s.sinceStart);
    if (!ts.isEmpty()) line += QLatin1Char('[') + ts + QLatin1Char(']');
    line += QStringLiteral("[%1]").arg(s.frame.load(std::memory_order_relaxed), 5);
    line += QString::fromLatin1(cat.name());
    line += QStringLiteral(": ");
    if (level != Level::Log) line += levelName(level) + QStringLiteral(": ");
    line += message;
    return line;
}

/// Caller holds the mutex. Appends one already-formatted record.
void appendLocked(const Category &cat, Level level, const QString &line, const QString &rawMessage)
{
    SinkState &s = sink();
    ++s.seq;
    cat.bumpRecords();

    if (s.ring.size() < kRingSize) {
        s.ring.append(Record { s.seq, cat.name(), level, line });
    } else {
        if (s.ring[s.ringNext].seq) ++s.dropped;
        s.ring[s.ringNext] = Record { s.seq, cat.name(), level, line };
        s.ringNext = (s.ringNext + 1) % kRingSize;
    }

    s.byCategory[QString::fromLatin1(cat.name())] += 1;
    s.byLevel[levelName(level)] += 1;
    if (level <= Level::Warning) {
        auto it = s.repeats.find(rawMessage);
        if (it != s.repeats.end()) ++it.value();
        else if (s.repeats.size() < kMaxRepeatKeys) s.repeats.insert(rawMessage, 1);
    }

    if (s.file) {
        s.pending += line.toUtf8();
        s.pending += '\n';
        ++s.sinceFlush;
        // Warning and above hit the disk immediately: those are the records a
        // crashed session has to have left behind. Everything else rides the
        // count / the idle timer / a block end / quit (spec §3.8-2).
        if (level <= Level::Warning || s.sinceFlush >= kFlushEveryRecords) flushLocked();
    }
}

}   // namespace

// ---------------------------------------------------------------------------

void setFrameCounter(quint64 f) { sink().frame.store(f, std::memory_order_relaxed); }
quint64 frameCounter() { return sink().frame.load(std::memory_order_relaxed); }

namespace {

/// The one emission path. `mirrorStderr` is false for exactly one caller — the
/// iris::Logger forwarder, which reaches stderr through the qInfo/qWarning it
/// has always emitted and must not print the line twice.
void writeImpl(const Category &cat, Level level, const QString &message, bool mirrorStderr)
{
    if (!cat.isActive(level)) return;

    QString line;
    {
        SinkState &s = sink();
        QMutexLocker lock(&s.mutex);
        BypassGuard guard;
        line = formatRecord(s, cat, level, message);
        appendLocked(cat, level, line, message);
    }

    // stderr routing (spec §3.2): Fatal/Error/Warning/Display are mirrored,
    // Log and below are file-only. Written with fputs rather than qWarning so
    // this can never re-enter the Qt funnel.
    if (mirrorStderr && level <= Level::Display) {
        std::fputs(qPrintable(line), stderr);
        std::fputc('\n', stderr);
    }
}

}   // namespace

void write(const Category &cat, Level level, const QString &message)
{
    writeImpl(cat, level, message, /*mirrorStderr*/ true);
}

void writeBlock(const Category &cat, Level level, const QString &title, const QStringList &lines)
{
    if (!cat.isActive(level)) return;

    QStringList formatted;
    {
        SinkState &s = sink();
        QMutexLocker lock(&s.mutex);
        BypassGuard guard;
        const QString head = formatRecord(s, cat, level, title);
        appendLocked(cat, level, head, title);
        formatted << head;
        for (const QString &l : lines) {
            const QString body = formatRecord(s, cat, level, QStringLiteral("  ") + l);
            appendLocked(cat, level, body, l);
            formatted << body;
        }
        flushLocked();   // a block is a unit: it is on disk when it ends
    }

    if (level <= Level::Display)
        for (const QString &l : formatted) {
            std::fputs(qPrintable(l), stderr);
            std::fputc('\n', stderr);
        }
}

void writeHeaderBlock(const QString &title, const QList<QPair<QString, QString>> &rows)
{
    QStringList lines;
    int width = 0;
    for (const auto &r : rows) width = qMax(width, r.first.size());
    for (const auto &r : rows)
        lines << QStringLiteral("%1 : %2").arg(r.first, -width).arg(r.second);
    writeBlock(app, Level::Display, title, lines);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

namespace {

QString defaultLogRoot()
{
    // The log lives in a `logs/` subdirectory of the directory the app ALREADY
    // writes to (fork F2-B), so nothing about "where Jahshaka writes" changes:
    // cwd in Debug (which is what the ogre log's relative name has always
    // meant, and what every doc and habit expects), AppDataLocation in release.
#ifdef QT_DEBUG
    return QDir(QDir::currentPath()).filePath(QStringLiteral("logs"));
#else
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) return QDir(QDir::currentPath()).filePath(QStringLiteral("logs"));
    return QDir(base).filePath(QStringLiteral("logs"));
#endif
}

/// OUR filenames and nothing else. A crash-*.log is forensic and is the one
/// artifact a user is ever asked to send — the sweep must not be able to
/// reach it, whatever the settings say (spec §3.9, §9-R2).
const QRegularExpression &ourFilePattern()
{
    static const QRegularExpression re(
        QStringLiteral("^jahshaka-\\d{4}\\.\\d{2}\\.\\d{2}-\\d{2}\\.\\d{2}\\.\\d{2}-\\d+(-ogre)?\\.log$"));
    return re;
}

/// Startup-only sweep, UE's policy (FMaintenance::DeleteOldLogs): by age and
/// by count, never by size and NEVER mid-session (spec §8-R4).
void sweep(const QString &dir, int maxFiles, int purgeDays)
{
    QDir d(dir);
    if (!d.exists()) return;
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-qMax(0, purgeDays));

    // Counted per pattern so a session file and its ogre sibling never evict
    // one another (they are two halves of one session).
    QFileInfoList sessions, ogres;
    const QFileInfoList all = d.entryInfoList(QDir::Files, QDir::Time);
    for (const QFileInfo &fi : all) {
        const auto m = ourFilePattern().match(fi.fileName());
        if (!m.hasMatch()) continue;
        (m.captured(1).isEmpty() ? sessions : ogres).append(fi);
    }
    auto prune = [&](QFileInfoList &list) {
        for (int i = 0; i < list.size(); ++i) {
            const bool tooOld  = purgeDays > 0 && list[i].lastModified() < cutoff;
            const bool tooMany = maxFiles > 0 && i >= maxFiles;
            if (tooOld || tooMany) QFile::remove(list[i].absoluteFilePath());
        }
    };
    prune(sessions);
    prune(ogres);
}

}   // namespace

bool start(const Options &opts)
{
    SinkState &s = sink();
    {
        QMutexLocker lock(&s.mutex);
        if (s.started) return s.file != nullptr;
        s.started = true;
        s.sinceStart.start();
        s.timestamps = opts.timestamps.isEmpty() ? QStringLiteral("local") : opts.timestamps;
    }

    applyProfileDefaults(buildProfile());

    if (opts.disabled) {
        QMutexLocker lock(&s.mutex);
        s.disabled = true;
        return false;
    }

    QString dir = opts.dir;
    QString path = opts.file;
    if (!path.isEmpty()) {
        dir = QFileInfo(path).absolutePath();
    } else {
        if (dir.isEmpty()) dir = defaultLogRoot();
    }
    if (!QDir().mkpath(dir)) {
        std::fprintf(stderr, "JahLog: cannot create log directory %s — logging to stderr only\n",
                     qPrintable(dir));
        return false;
    }

    // Timestamped AT BIRTH, with the pid: multi-instance-safe by construction,
    // no rename-on-open race (UE's weak spot).
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy.MM.dd-HH.mm.ss"));
    const qint64 pid = QCoreApplication::applicationPid();
    const QString base = QStringLiteral("jahshaka-%1-%2").arg(stamp).arg(pid);
    if (path.isEmpty()) path = QDir(dir).filePath(base + QStringLiteral(".log"));
    const QString ogrePathLocal = QDir(dir).filePath(base + QStringLiteral("-ogre.log"));

    sweep(dir, opts.maxFiles, opts.purgeDays);

    auto *f = new QFile(path);
    // Unbuffered: our own QByteArray IS the buffer, so a flush() is exactly one
    // write(2) and the raw fd's offset is always where the file really ends —
    // which is what makes the signal-handler escape hatch land in the right
    // place instead of inside a QIODevice buffer nobody will ever drain.
    if (!f->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered)) {
        std::fprintf(stderr, "JahLog: cannot open %s — logging to stderr only\n", qPrintable(path));
        delete f;
        return false;
    }

    {
        QMutexLocker lock(&s.mutex);
        s.file = f;
        s.dir = dir;
        s.sessionPath = path;
        s.ogrePath = ogrePathLocal;
        s.fd.store(f->handle(), std::memory_order_relaxed);
    }

    // latest.log — one `tail -f` away for the rig and the docs. A symlink where
    // symlinks are free; a naming file on Windows, where they need privilege.
    const QString latest =
#ifdef Q_OS_WIN
        QDir(dir).filePath(QStringLiteral("latest.txt"));
#else
        QDir(dir).filePath(QStringLiteral("latest.log"));
#endif
    QFile::remove(latest);
#ifdef Q_OS_WIN
    QFile marker(latest);
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        marker.write(path.toUtf8());
        marker.close();
    }
#else
    QFile::link(path, latest);
#endif
    {
        QMutexLocker lock(&s.mutex);
        s.latestPath = latest;
    }

    // The open bracket. An ABSENT close line is itself the signal that the
    // session died (UE's brackets).
    write(app, Level::Display,
          QStringLiteral("Log file open, %1")
              .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)));

    if (QCoreApplication::instance()) {
        QMutexLocker lock(&s.mutex);
        if (!s.timer) s.timer = new FlushTimer();
    }
    return true;
}

bool isStarted()
{
    SinkState &s = sink();
    QMutexLocker lock(&s.mutex);
    return s.started && s.file != nullptr;
}

void stop(const QString &reason)
{
    SinkState &s = sink();
    {
        QMutexLocker lock(&s.mutex);
        if (!s.file) { s.started = false; return; }
    }

    // The session summary — UE prints a unique-warning/error roll-up at exit
    // and it is the single most useful line in the file for triage.
    const QVariantMap c = counts();
    QStringList lines;
    const QVariantMap byLevel = c.value(QStringLiteral("byLevel")).toMap();
    QStringList levelBits;
    for (auto it = byLevel.constBegin(); it != byLevel.constEnd(); ++it)
        levelBits << QStringLiteral("%1=%2").arg(it.key(), it.value().toString());
    lines << QStringLiteral("by level    : %1").arg(levelBits.join(QStringLiteral(" ")));
    const QVariantMap byCat = c.value(QStringLiteral("byCategory")).toMap();
    QStringList catBits;
    for (auto it = byCat.constBegin(); it != byCat.constEnd(); ++it)
        catBits << QStringLiteral("%1=%2").arg(it.key(), it.value().toString());
    lines << QStringLiteral("by category : %1").arg(catBits.join(QStringLiteral(" ")));
    const QVariantList top = c.value(QStringLiteral("topRepeats")).toList();
    for (const QVariant &v : top) {
        const QVariantMap m = v.toMap();
        lines << QStringLiteral("repeated %1x : %2")
                     .arg(m.value(QStringLiteral("count")).toString(),
                          m.value(QStringLiteral("message")).toString());
    }
    writeBlock(app, Level::Display, QStringLiteral("=== SESSION SUMMARY ==="), lines);

    write(app, Level::Display,
          QStringLiteral("Log file closed, %1%2")
              .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                   reason.isEmpty() ? QString() : QStringLiteral(" (") + reason + QLatin1Char(')')));

    iris::Logger::getSingleton()->flush();

    QMutexLocker lock(&s.mutex);
    flushLocked();
    s.fd.store(-1, std::memory_order_relaxed);
    s.file->close();
    delete s.file;
    s.file = nullptr;
    s.started = false;
    delete s.timer;
    s.timer = nullptr;
}

void flush()
{
    {
        SinkState &s = sink();
        QMutexLocker lock(&s.mutex);
        flushLocked();
    }
    // The legacy file rides the same schedule now that its per-line flush is
    // gone. Deliberately OUTSIDE our lock: iris::Logger's write path calls our
    // sink (with its own lock released first), so nesting the two the other way
    // round here is the only way to build a cycle.
    iris::Logger::getSingleton()->flush();
}

int rawFd() { return sink().fd.load(std::memory_order_relaxed); }

QString sessionFilePath()
{
    SinkState &s = sink();
    QMutexLocker lock(&s.mutex);
    return s.sessionPath;
}

QString ogreFilePath()
{
    SinkState &s = sink();
    QMutexLocker lock(&s.mutex);
    return s.ogrePath;
}

QVariantMap paths()
{
    SinkState &s = sink();
    QMutexLocker lock(&s.mutex);
    QVariantMap m;
    m["session"] = s.sessionPath;
    m["ogre"] = s.ogrePath;
    m["dir"] = s.dir;
    m["latest"] = s.latestPath;
    m["enabled"] = (s.file != nullptr);
    return m;
}

// ---------------------------------------------------------------------------
// Configuration layers 2 and 3
// ---------------------------------------------------------------------------

Options optionsFromSettings(QSettings *settings)
{
    Options o;
    if (!settings) return o;
    o.dir        = settings->value(QStringLiteral("log/dir")).toString();
    o.maxFiles   = settings->value(QStringLiteral("log/maxFiles"), o.maxFiles).toInt();
    o.purgeDays  = settings->value(QStringLiteral("log/purgeDays"), o.purgeDays).toInt();
    o.timestamps = settings->value(QStringLiteral("log/timestamps"), o.timestamps).toString();
    return o;
}

void applyIniLevels(QSettings *settings)
{
    if (!settings) return;
    settings->beginGroup(QStringLiteral("log"));
    const QStringList keys = settings->childKeys();
    settings->endGroup();
    for (const QString &key : keys) {
        if (key == QLatin1String("timestamps") || key == QLatin1String("maxFiles")
            || key == QLatin1String("purgeDays") || key == QLatin1String("dir")
            || key == QLatin1String("perfSampleSeconds"))
            continue;
        const QString value =
            settings->value(QStringLiteral("log/") + key).toString();
        Level l;
        if (!parseLevel(value, &l)) {
            JAH_LOG(app, Warning,
                    QStringLiteral("log/%1: '%2' is not a level name — ignored").arg(key, value));
            continue;
        }
        // The ini layer sets the DEFAULT, not an explicit override: a later
        // --log-level=global=verbose should still be able to lift a category
        // the ini merely re-based.
        if (!setCategoryLevel(key, l, /*explicitOverride*/ false))
            JAH_LOG(app, Warning,
                    QStringLiteral("log/%1: no such category (typo?) — ignored").arg(key));
    }
}

void applyLevelSpec(const QString &spec)
{
    const QStringList parts = spec.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const int eq = part.indexOf(QLatin1Char('='));
        QString name = (eq < 0) ? QStringLiteral("global") : part.left(eq).trimmed();
        const QString value = (eq < 0) ? part.trimmed() : part.mid(eq + 1).trimmed();
        Level l;
        if (!parseLevel(value, &l)) {
            JAH_LOG(app, Warning,
                    QStringLiteral("--log-level: '%1' is not a level name — ignored").arg(value));
            continue;
        }
        if (!setCategoryLevel(name, l, /*explicitOverride*/ true))
            JAH_LOG(app, Warning,
                    QStringLiteral("--log-level: no category '%1' (typo?) — ignored").arg(name));
    }
}

// ---------------------------------------------------------------------------
// Read-back
// ---------------------------------------------------------------------------

namespace {

/// Caller holds the mutex. The ring in sequence order, oldest first.
QVector<Record> orderedLocked()
{
    const SinkState &s = sink();
    QVector<Record> out;
    if (s.ring.size() < kRingSize) { out = s.ring; return out; }
    out.reserve(kRingSize);
    for (int i = 0; i < kRingSize; ++i) out.append(s.ring[(s.ringNext + i) % kRingSize]);
    return out;
}

}   // namespace

QStringList tail(int n, const QString &categoryName, Level minLevel)
{
    SinkState &s = sink();
    QMutexLocker lock(&s.mutex);
    const QVector<Record> ordered = orderedLocked();
    QStringList out;
    for (const Record &r : ordered) {
        if (r.level > minLevel) continue;
        if (!categoryName.isEmpty() && categoryName != QLatin1String(r.cat)) continue;
        out << r.text;
    }
    if (n > 0 && out.size() > n) out = out.mid(out.size() - n);
    return out;
}

quint64 mark(const QString &label)
{
    write(app, Level::Display, QStringLiteral("MARK: %1").arg(label));
    SinkState &s = sink();
    QMutexLocker lock(&s.mutex);
    return s.seq;
}

QStringList since(quint64 markerId, Level minLevel)
{
    SinkState &s = sink();
    QMutexLocker lock(&s.mutex);
    const QVector<Record> ordered = orderedLocked();
    QStringList out;
    for (const Record &r : ordered) {
        if (r.seq <= markerId) continue;
        if (r.level > minLevel) continue;
        out << r.text;
    }
    return out;
}

QVariantMap counts()
{
    SinkState &s = sink();
    QMutexLocker lock(&s.mutex);
    QVariantMap byCat, byLvl;
    for (auto it = s.byCategory.constBegin(); it != s.byCategory.constEnd(); ++it)
        byCat.insert(it.key(), qulonglong(it.value()));
    for (auto it = s.byLevel.constBegin(); it != s.byLevel.constEnd(); ++it)
        byLvl.insert(it.key(), qulonglong(it.value()));

    QVector<QPair<quint64, QString>> sorted;
    sorted.reserve(s.repeats.size());
    for (auto it = s.repeats.constBegin(); it != s.repeats.constEnd(); ++it)
        if (it.value() > 1) sorted.append({ it.value(), it.key() });
    std::sort(sorted.begin(), sorted.end(),
              [](const QPair<quint64, QString> &a, const QPair<quint64, QString> &b) {
                  return a.first > b.first;
              });
    QVariantList top;
    for (int i = 0; i < sorted.size() && i < 5; ++i) {
        QVariantMap m;
        m["count"] = qulonglong(sorted[i].first);
        m["message"] = sorted[i].second;
        top.append(m);
    }

    QVariantMap out;
    out["byCategory"] = byCat;
    out["byLevel"] = byLvl;
    out["topRepeats"] = top;
    out["records"] = qulonglong(s.seq);
    out["dropped"] = qulonglong(s.dropped);
    return out;
}

// ---------------------------------------------------------------------------
// The Qt funnel (spec §3.6)
// ---------------------------------------------------------------------------

namespace {

QtMessageHandler gPrevHandler = nullptr;
std::atomic<bool> gHandlerInstalled { false };

// KNOWN-NOISE SUPPRESSION (owner order 2026-09-07: "we need our log files
// clean with real errors only"). Each entry is one upstream defect we cannot
// fix at its source, identified by its EXACT message text. The first
// occurrence is logged (with a note that the rest are suppressed) and every
// further one is counted and dropped — file AND stderr — so a 13-minute
// session stops writing 57,000 copies of a warning nobody can act on.
// Honesty rule: nothing here may match by substring or category — exact text
// only, so a genuinely new problem can never hide behind an entry.
struct KnownNoise {
    const char *exactText;   // the full message, byte-exact
    const char *anchor;      // where the upstream defect lives
    std::atomic<quint64> suppressed { 0 };
    std::atomic<bool> announced { false };
};
KnownNoise gKnownNoise[] = {
    // Qlementine v1.4.2 Popover.cpp:538: _frame->mapTo(this, ...) with _frame
    // not parented to the popover — fires once per paint, harmless (Qt
    // returns (0,0) and the popover renders fine). Vendored submodule; fix
    // upstream or at a bump, not here.
    { "QWidget::mapTo(): parent must be in parent hierarchy",
      "qlementine Popover.cpp:538", {}, {} },
};

// Matches msg against the table. Returns true when the message must be
// dropped (already announced once); false when it should pass through —
// including the FIRST occurrence, which passes annotated so the log shows
// the defect exists without drowning in it.
bool suppressKnownNoise(const QString &msg, QString &firstTimeNote)
{
    for (KnownNoise &n : gKnownNoise) {
        if (msg != QLatin1String(n.exactText)) continue;
        if (!n.announced.exchange(true)) {
            firstTimeNote = msg + QStringLiteral(
                "  [known-noise: %1 — further occurrences suppressed; count "
                "reported at shutdown]").arg(QLatin1String(n.anchor));
            return false;
        }
        n.suppressed.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

Level levelForQt(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return Level::Verbose;
    case QtInfoMsg:     return Level::Log;
    case QtWarningMsg:  return Level::Warning;
    case QtCriticalMsg: return Level::Error;
    case QtFatalMsg:    return Level::Fatal;
    }
    return Level::Log;
}

void funnel(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    QString noiseAnnotated;
    if (suppressKnownNoise(msg, noiseAnnotated)) return;   // counted, dropped
    const QString &effectiveMsg = noiseAnnotated.isEmpty() ? msg : noiseAnnotated;

    if (tlBypass == 0) {
        Category *cat = &qt;
        if (ctx.category && *ctx.category && qstrcmp(ctx.category, "default") != 0)
            cat = category(QString::fromLatin1(ctx.category));
        const Level level = levelForQt(type);
        if (cat->isActive(level)) {
            SinkState &s = sink();
            QMutexLocker lock(&s.mutex);
            BypassGuard guard;
            const QString line = formatRecord(s, *cat, level, effectiveMsg);
            appendLocked(*cat, level, line, effectiveMsg);
        }
    }

    // ALWAYS chain. stderr must stay bit-identical to a build without this
    // program: app.startup_quiet's three ABSENCE assertions read that stream
    // and would pass vacuously the day it moves into the file (spec §9-R1).
    // This is not an optimisation opportunity. (Known-noise suppression above
    // only ever REMOVES lines, which no absence assertion can notice.)
    if (gPrevHandler) gPrevHandler(type, ctx, effectiveMsg);
    else qt_message_output(type, ctx, effectiveMsg);
}

}   // namespace

void installQtMessageHandler()
{
    if (gHandlerInstalled.exchange(true)) return;
    gPrevHandler = qInstallMessageHandler(funnel);
}

void removeQtMessageHandler()
{
    if (!gHandlerInstalled.exchange(false)) return;
    // The known-noise accounting the suppression promised: one summary line
    // per table entry that actually fired, written while the funnel is still
    // ours so it lands in the session log like any other record.
    for (KnownNoise &n : gKnownNoise) {
        const quint64 count = n.suppressed.load(std::memory_order_relaxed);
        if (count)
            qWarning("known-noise summary: suppressed %llu repeats of \"%s\" (%s)",
                     static_cast<unsigned long long>(count), n.exactText, n.anchor);
    }
    qInstallMessageHandler(gPrevHandler);
    gPrevHandler = nullptr;
}

ScopedQtFunnelBypass::ScopedQtFunnelBypass() { ++tlBypass; }
ScopedQtFunnelBypass::~ScopedQtFunnelBypass() { --tlBypass; }

// ---------------------------------------------------------------------------
// iris::Logger absorption (fork F5-A)
// ---------------------------------------------------------------------------

void absorbIrisLogger()
{
    // The dependency direction is the reason this lives here and not in
    // irisgl/core/logger.cpp: IrisGL is the document library and must never
    // link Studio. iris::Logger grew a sink hook; this is its one installer.
    iris::Logger::setSink([](int severity, const QString &text) {
        Level l = Level::Log;
        if (severity == 1) l = Level::Warning;
        else if (severity >= 2) l = Level::Error;
        // No stderr mirror here: the q* call below is the stderr copy, and it
        // is the one that has always been there.
        writeImpl(legacy, l, text, /*mirrorStderr*/ false);
        // Keep the stderr duplication iris::Logger has always done, but stop
        // the funnel writing the SAME line into the file a second time.
        ScopedQtFunnelBypass bypass;
        switch (severity) {
        case 0:  qInfo().noquote() << text; break;
        case 1:  qWarning().noquote() << text; break;
        default: qCritical().noquote() << text; break;
        }
    });
}

}   // namespace JahLog
