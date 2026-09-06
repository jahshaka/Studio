/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef JAHLOG_H
#define JAHLOG_H

// JahLog — the session log (SPECS/SESSION_LOG_SPEC.md).
//
// ONE file per app RUN (fork F1-A), in a `logs/` subdirectory of the directory
// the app already writes to (F2-B), with a `latest.log` pointer beside it.
// Opening a scene writes a prominent BLOCK into that one file rather than
// starting a new file, so the startup header (build id, GPU, settings) and the
// failure that happened forty minutes later live in the same artifact.
//
// WHY A HOUSE LOGGER AND NOT QLoggingCategory: Qt gives five levels, no
// Display-vs-file routing split, no runtime per-category retune from a script,
// and no file sink. We install Qt's message handler and FUNNEL INTO this sink
// instead (installQtMessageHandler below), which is what makes the ~61 existing
// qDebug/qWarning call sites land in the file with zero edits.
//
// THE THREE DISCIPLINES, all load-bearing (spec §3.8, §9):
//   1. ZERO log calls on the frame path at default verbosity. A suppressed
//      record costs one relaxed atomic load and a compare; an emitted one takes
//      a mutex and formats a string, and that does not belong in renderOneFrame.
//   2. BUFFERED, never per-record flush. Warnings and above flush immediately;
//      everything else rides the record count / idle timer / block ends / quit.
//   3. SIGNAL HANDLERS NEVER CALL THIS. It takes a mutex and allocates. The
//      crash handler and the watchdog stall handler get rawFd() and write(2).
//
// stderr is NEVER taken away: the Qt funnel chains to the previous handler, so
// the stream `app.startup_quiet` greps stays bit-identical (spec §9-R1).

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <atomic>
#include <functional>

class QSettings;

namespace JahLog {

/// Unreal's level set, verbatim — anyone with UE muscle memory reads our log
/// correctly on sight. `Off` is the per-category mute sentinel and sorts below
/// everything, so "is this record active" is one integer compare.
///
/// NOTE Fatal does NOT terminate here (spec §3.1). We are a content tool;
/// inventing a new abort path would be a stability regression, not a logging
/// feature. Fatal means "write, flush, mirror to stderr".
enum class Level {
    Off = 0,
    Fatal,
    Error,
    Warning,
    Display,
    Log,
    Verbose,
    VeryVerbose
};

/// Which default table applies. Chosen by QT_DEBUG at startup — the same
/// discriminator the settings file and the window title already use — but kept
/// as an argument so a Debug-build test can assert BOTH tables (spec §3.4).
enum class Profile { Development, Production };

QString levelName(Level l);
/// Parses "warning"/"Warning"/"warn"/"off"… Returns false on an unknown name.
bool parseLevel(const QString &text, Level *out);

/// A logging category: a name and an ATOMIC level. Categories are created by
/// static construction (the built-ins below) or on demand by name (the Qt
/// funnel's own categories), and are never destroyed.
class Category
{
public:
    explicit Category(const char *name);

    const char *name() const { return mName; }

    Level level() const
    {
        return static_cast<Level>(mLevel.load(std::memory_order_relaxed));
    }
    void setLevel(Level l) { mLevel.store(int(l), std::memory_order_relaxed); }

    /// THE HOT PATH. One relaxed load and a compare — this is what makes
    /// "runtime-only, no compile-time ceiling" (fork F4-A) honest.
    bool isActive(Level l) const { return int(l) <= mLevel.load(std::memory_order_relaxed); }

    /// The level the profile's default table gave this category, for
    /// log.categories() and for "has a human retuned this".
    Level defaultLevel() const { return static_cast<Level>(mDefault.load(std::memory_order_relaxed)); }
    void setDefaultLevel(Level l) { mDefault.store(int(l), std::memory_order_relaxed); }

    /// True once something above the compiled default (ini, CLI or a verb) set
    /// this category explicitly — `global` then stops re-levelling it.
    bool isExplicit() const { return mExplicit.load(std::memory_order_relaxed); }
    void setExplicit(bool e) { mExplicit.store(e, std::memory_order_relaxed); }

    /// Records emitted through this category this session (suppressed ones are
    /// not counted — they never became records).
    quint64 records() const { return mRecords.load(std::memory_order_relaxed); }
    void bumpRecords() const { mRecords.fetch_add(1, std::memory_order_relaxed); }

private:
    const char *mName;
    std::atomic<int> mLevel { int(Level::Log) };
    std::atomic<int> mDefault { int(Level::Log) };
    std::atomic<bool> mExplicit { false };
    mutable std::atomic<quint64> mRecords { 0 };
};

// ---- The v1 categories (spec §3.3). Registry-extensible: category(name)
// creates one on demand, and an unknown name in an ini/CLI setting is REPORTED
// INTO THE LOG rather than silently dropped (a typo is the usual cause).
extern Category app;        ///< lifecycle, the startup header, quit
extern Category scene;      ///< open/save/close blocks
extern Category engine;     ///< EngineErrorPump + forwarded Ogre criticals
extern Category ogre;       ///< forwarded Ogre non-criticals (Off by default)
extern Category render;     ///< driver, pacing, slow frames
extern Category perf;       ///< the periodic sampler (spec §8)
extern Category shader;     ///< shader cache + warm-up
extern Category assets;     ///< import pipeline, store, CAS
extern Category db;
extern Category script;
extern Category ui;
extern Category mirror;
extern Category physics;
extern Category media;
extern Category qt;         ///< the Qt message-handler funnel
extern Category legacy;     ///< iris::Logger, absorbed at the sink (fork F5-A)

/// The category with this name, creating it if it does not exist. Never null.
/// Dynamically created categories are heap-allocated and intentionally leaked:
/// a category that dies is a dangling pointer in every record that named it.
Category *category(const QString &name);
/// Every registered category, registration order.
QList<Category *> categories();

// ---------------------------------------------------------------------------
// Emission
// ---------------------------------------------------------------------------

/// Writes one record. Cheap to call when suppressed, but the ARGUMENT is still
/// evaluated — use the macro below on any path where building the string costs
/// something.
void write(const Category &cat, Level level, const QString &message);

/// Multi-line block helper: writes `title`, then each line indented, then
/// flushes (spec §3.8-2 "at the end of each block"). One lock acquisition for
/// the whole block, so a scene-open ledger can never be interleaved by another
/// thread's record.
void writeBlock(const Category &cat, Level level, const QString &title, const QStringList &lines);

/// The guard-then-emit form. `LEVEL` is the unqualified enumerator.
#define JAH_LOG(cat, LEVEL, msg)                                                \
    do {                                                                        \
        if ((cat).isActive(::JahLog::Level::LEVEL))                             \
            ::JahLog::write((cat), ::JahLog::Level::LEVEL, (msg));              \
    } while (0)

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

struct Options
{
    /// Directory the session files go in. Empty = the platform default
    /// (`<cwd>/logs` in Debug, `<AppDataLocation>/logs` in release). This is
    /// the flag hermetic tests use — NOT `HOME=`, which CoreFoundation ignores
    /// on macOS (spec §9-R6).
    QString dir;
    /// Explicit session-file path. Overrides the generated name; its parent
    /// directory becomes the log dir (so the ogre sibling lands beside it).
    QString file;
    /// --no-log: everything still routes and counts, nothing reaches disk.
    bool disabled = false;
    /// Keep at most this many of OUR files per pattern, and delete ours older
    /// than purgeDays. Never touches anything that is not ours — a crash-*.log
    /// is the one artifact a user is asked to send (spec §3.9).
    int maxFiles = 10;
    int purgeDays = 5;
    /// local | utc | sinceStart | none
    QString timestamps = QStringLiteral("local");
};

/// Layer 2 of the precedence chain for the OPTIONS (as applyIniLevels is for
/// the levels): `log/dir`, `log/maxFiles`, `log/purgeDays`, `log/timestamps`.
/// The CLI then overrides whatever it names, and start() takes the result.
Options optionsFromSettings(QSettings *settings);

/// Applies the profile's default table (spec §3.4). Pure data, exposed so a
/// test can assert both tables from one Debug build.
QVariantMap defaultLevels(Profile p);

/// Layer 1 of the precedence chain, applied. start() calls this before the
/// file is even open; it is public so a suite can assert layers 1-4 in order
/// without opening a file at all.
void applyProfileDefaults(Profile p);
/// The profile this build ships (QT_DEBUG → Development).
Profile buildProfile();

/// Opens the session file, sweeps old ones, writes the open bracket.
/// Idempotent: a second call is ignored. Returns false when the file could not
/// be opened — logging then routes to stderr/ring only, and the app runs on.
bool start(const Options &opts);
/// Writes the close bracket + the session summary and flushes. Safe to call
/// twice; safe to call when start() failed.
void stop(const QString &reason = QString());
bool isStarted();

/// Layer 2 of the precedence chain (compiled default → INI → CLI → runtime).
/// Reads `log/global`, `log/<category>`, `log/timestamps`,
/// `log/perfSampleSeconds`, `log/maxFiles`, `log/purgeDays`.
void applyIniLevels(QSettings *settings);
/// Layer 3: `--log-level=<level>` or `--log-level=<cat>=<lvl>[,<cat>=<lvl>…]`,
/// repeated as often as you like. Unknown names are reported into the log.
void applyLevelSpec(const QString &spec);

/// The global level = the default for every category nobody has set
/// explicitly. Deliberately NOT a ceiling: production sets global=Display but
/// keeps `perf` at Log, and a ceiling would silently kill the perf sampler —
/// the one instrument the fps-decay case exists for.
void setGlobalLevel(Level l, bool explicitOverride = true);
Level globalLevel();
/// Sets one category. `explicitOverride` marks it as retuned so a later
/// global change leaves it alone.
bool setCategoryLevel(const QString &name, Level l, bool explicitOverride = true);

// ---------------------------------------------------------------------------
// Read-back (the ring, spec §3.8-5) — answers without re-reading the file
// ---------------------------------------------------------------------------

/// Where the artifacts are: {session, ogre, dir, latest}.
QVariantMap paths();
/// The session file's path, or empty when disabled/unopened.
QString sessionFilePath();
/// The per-session ogre sibling this run should use (F3-B). Computed at
/// start(); EngineHost reads it when it builds EngineConfig::logFile.
QString ogreFilePath();

/// Last `n` formatted records, oldest first. Filters are optional.
QStringList tail(int n, const QString &categoryName = QString(),
                 Level minLevel = Level::VeryVerbose);
/// A named point in the record stream. Returns the marker's id.
quint64 mark(const QString &label);
/// Everything recorded since `markerId` (0 = the whole ring).
QStringList since(quint64 markerId, Level minLevel = Level::VeryVerbose);
/// {byCategory, byLevel, topRepeats, records, dropped}
QVariantMap counts();

/// Writes the buffer to disk now.
void flush();

/// The session file's raw descriptor, or -1. THE ONLY thing a signal handler
/// may take from this module: write(2) into it is async-signal-safe, calling
/// anything else here is not (spec §9-R3).
int rawFd();

// ---------------------------------------------------------------------------
// The Qt funnel (spec §3.6)
// ---------------------------------------------------------------------------

/// Installs qInstallMessageHandler. The PREVIOUS handler is called
/// unconditionally afterwards, so stderr is bit-identical to a build without
/// this program — which is what keeps `app.startup_quiet`'s three ABSENCE
/// assertions from passing vacuously (spec §9-R1). Do not "optimise" that away.
void installQtMessageHandler();
void removeQtMessageHandler();

/// While one of these is alive on this thread, the Qt funnel forwards to the
/// previous handler (stderr) but writes NOTHING to the file. Exists for exactly
/// one caller: the iris::Logger forwarder, which has already written the record
/// under `legacy` and only calls qInfo/qWarning/qCritical to keep stderr the
/// same as it always was. Without it every legacy line would appear twice.
class ScopedQtFunnelBypass
{
public:
    ScopedQtFunnelBypass();
    ~ScopedQtFunnelBypass();
};

/// Installs the iris::Logger → `legacy` forwarder (fork F5-A: absorb at the
/// SINK, so all ~72 irisLog call sites gain timestamps, categories, routing and
/// rotation without touching one of them). Studio-side because irisgl must not
/// depend on Studio: this is the only place the two meet.
void absorbIrisLogger();

/// The header block (spec §4). `rows` are label/value pairs; groups separate.
void writeHeaderBlock(const QString &title, const QList<QPair<QString, QString>> &rows);

/// The rendered-frame counter that goes in every line's second column. The
/// render driver pushes it; 0 before the engine starts. Atomic, written from
/// the loop thread and read from every logging thread.
void setFrameCounter(quint64 frame);
quint64 frameCounter();

}   // namespace JahLog

#endif   // JAHLOG_H
