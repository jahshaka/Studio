/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef FRAMEMONITOR_H
#define FRAMEMONITOR_H

// FrameMonitor — THE HOST HALF of the render-loop monitor
// (SPECS/RENDER_LOOP_MONITOR_SPEC.md §4.8, lane MON-P1b).
//
// The engine records what each frame DID and why (Engine::setFrameMonitor,
// takeFrameRecords, takeMonitorEvents, captureSnapshot — lane MON-P1a). This
// object is everything around that:
//
//   * the CAPTURE STATE MACHINE — idle -> recording -> writing -> idle, with a
//     length (20 s by default) and an early stop;
//   * the HOST'S OWN STAGES — the driver's tick, the gap between ticks split
//     into idle and UI work, the viewport's document tick / mirror / env /
//     camera, and the mirror's sub-stages — pushed through
//     Engine::noteHostStage so `frames.jsonl` carries ONE stage tree per frame
//     rather than two half-trees nobody can add up;
//   * the CAPTURE BUNDLE writer (§4.8): machine.json, snapshot_start.json,
//     snapshot_end.json, frames.jsonl, events.jsonl, trace.json, ogre.log.
//
// WHAT IT IS NOT. It judges nothing (owner, 2026-09-12: "we aren't really
// looking at targets, we are looking for problems ... the profiler shouldn't
// care about the targets"), it computes no verdict, and it draws NOTHING. The
// only thing a user ever sees is two toasts — one when a capture starts and one
// naming the bundle when it stops — and this object does not show them either:
// it asks (toastRequested) and the shell answers.
//
// OFF BY DEFAULT, AND FREE WHEN OFF. `framemonitor::active()` is one plain
// bool, read on the frame path; every hook below is an early return on it, and
// nothing is allocated, connected or timed until a capture starts. Forward
// only: a capture records the frames that come AFTER the key, never a history.
//
// WHY A SINGLETON. The frame path is spread across the render driver, the
// editor viewport and (through the engine's own hook) the mirror; they have no
// common owner to be injected from, exactly like EngineHost. One capture at a
// time, one process, one UI thread — the engine's own contract.
//
// THREAD AFFINITY: the UI thread, like every engine call (Engine.h).

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <memory>

class QTimer;

namespace jahshaka { namespace engine { class Engine; } }

namespace framemonitor {

namespace detail {
/// THE ONE BRANCH ON THE FRAME PATH, and it lives in the HEADER (an inline
/// variable) rather than in framemonitor.cpp for a build reason worth stating:
/// the frame-cause tag in bridge/stableoffscreenrender.h is read by several
/// test targets that link the engine and Qt but nothing of the shell, and a
/// call into the service's TU would drag EngineHost, the settings manager and
/// the session log into each of them. An inline bool costs those targets
/// nothing and reads identically.
///
/// NOT ATOMIC, deliberately: every reader and the only writer are on the UI
/// thread (Engine.h's thread-affinity contract), and a fence here would sit in
/// the middle of the frame path guarding a value nothing else can touch.
inline bool gActive = false;
}   // namespace detail

/// True only while a capture is recording. Read it before doing anything that
/// costs a clock read.
inline bool active() { return detail::gActive; }

/// A host stage, exclusive of its children (the spec's stage tree). Measures
/// only while a capture runs; otherwise it is a constructor and a destructor
/// that test one bool.
///
///     { framemonitor::Stage s("host.mirror"); mMirror->sync(); }
///
/// Nested scopes subtract: an inner scope's wall time is removed from its
/// parent's, so the stages of one frame sum to the frame without
/// double-counting. Stages are folded into the NEXT frame record when they end
/// outside a frame (which is the normal case: the host syncs, then renders).
class Stage
{
public:
    explicit Stage(const char *name);
    ~Stage();
    Stage(const Stage &) = delete;
    Stage &operator=(const Stage &) = delete;

    /// Ends the stage early (the destructor then does nothing). For the one
    /// case a scope cannot express: a stage that ends inside a branch.
    void end();

private:
    const char *mName = nullptr;
    qint64      mStart = 0;      ///< steady nanoseconds
    qint64      mChildrenNs = 0; ///< accumulated by nested scopes
    Stage      *mParent = nullptr;
    bool        mOpen = false;
};

}   // namespace framemonitor

class FrameMonitor : public QObject
{
    Q_OBJECT
public:
    static FrameMonitor &instance();

    enum class Phase { Idle, Recording, Writing };

    /// What a capture was asked for. Everything has a default; the verb, the
    /// key and the MCP tool all come through here.
    struct Request {
        double  seconds = 0.0;      ///< 0 = the preference (20 s by default)
        QString label;              ///< names the bundle directory; "" = the scene's name
        QString outDir;             ///< "" = <capture root>/<date>-<time>-<label>
        qint64  maxBytes = 0;       ///< 0 = the default cap (§4.8 "size-capped")
    };

    // ---- the state machine ------------------------------------------------
    /// Starts a capture. False (with `error` filled) when one is already
    /// running, there is no engine, or the bundle directory cannot be made.
    bool start(const Request &request, QString *error = nullptr);
    /// Stops early and writes the bundle. False when nothing was running.
    bool stop(QString *error = nullptr);
    bool isRecording() const { return mPhase == Phase::Recording; }
    Phase phase() const { return mPhase; }

    /// `perf.status()` — the whole state, including the engine's MonitorStatus
    /// and the last bundle's path and size.
    QVariantMap status() const;

    /// A named instant in the record (`perf.mark`). Refused (false) when no
    /// capture is running: a mark nobody records is a lie, not a no-op.
    bool mark(const QString &label);

    QString lastBundlePath() const { return mLastBundle; }

    /// The capture-length preference (`perf/captureSeconds`), and its default.
    static double defaultSeconds();
    static double preferredSeconds();
    static void setPreferredSeconds(double seconds);
    /// Where bundles are written: $JAHSHAKA_PERF_ROOT, else the `perf/captureRoot`
    /// preference, else ~/Developer/spikes/perf (the workspace's spikes tree —
    /// real disk, never /tmp, per the scratch law).
    static QString captureRoot();

    // ---- the host's hooks (all no-ops unless a capture is running) --------
    /// The render driver, at the top of a tick: closes the gap since the last
    /// tick ended and splits it into idle (the event loop was blocked, waiting)
    /// and UI (the event loop was busy with something else).
    void noteTickStart();
    /// The render driver, at the end of a tick: drains what the engine has
    /// published so far and re-arms the gap clock.
    void noteTickEnd();
    /// The shell, after it really showed a toast this object asked for — so the
    /// event log carries the toast's LIFETIME and analysis knows which frames
    /// it overlapped (owner, 2026-09-12).
    void noteToastShown(const QString &title, const QString &text, int holdMs);

    /// The engine, or null when none is running. Public because the perf verbs
    /// report the engine's own MonitorStatus.
    std::shared_ptr<jahshaka::engine::Engine> engine() const;

signals:
    /// A capture started; `seconds` is its planned length.
    void started(double seconds);
    /// A capture finished and the bundle at `path` is complete.
    void stopped(const QString &path);
    /// SHOW THIS, please — the monitor's only visible output, and the shell's
    /// job (this object owns no widgets). `holdMs` 0 = the toast's own default.
    void toastRequested(const QString &title, const QString &text, int holdMs);

private:
    FrameMonitor();
    ~FrameMonitor() override;

    class Bundle;    ///< the writer (framemonitor.cpp)

    /// Moves everything the engine has published into the bundle. Called on
    /// every tick end, on a slow timer (so a capture with no ticks still
    /// drains) and in a LOOP at stop — the engine's holding queue hands back
    /// its tail AFTER the monitor goes off (Engine.h takeFrameRecords).
    void drain();
    /// One pass over the engine's two queues; returns how many records moved.
    unsigned drainOnce();
    void finish(bool early);
    void connectDispatcher();
    void disconnectDispatcher();

    Phase   mPhase = Phase::Idle;
    std::unique_ptr<Bundle> mBundle;
    QTimer *mAutoStop = nullptr;
    QTimer *mDrainTimer = nullptr;
    QString mLastBundle;
    /// The closed bundle's totals: `status()` must still answer "how many
    /// frames did that capture get" after the writer is gone.
    double  mLastFrames = 0.0, mLastEvents = 0.0;
    double  mPlannedSeconds = 0.0;
    /// The worst GPU-sample overflow the engine reported during this capture.
    unsigned mGpuSamplesTruncated = 0;

    // ---- what the shell last showed, for the verbs (and their suites) -----
    QString mLastToastTitle, mLastToastText;
    int     mLastToastHoldMs = 0;
    int     mToastsShown = 0;

    // ---- the gap between ticks (§4.2 gap.idle / gap.ui) -------------------
    QElapsedTimer mSinceTickEnd;
    qint64 mBlockedNs = 0;       ///< time the event dispatcher spent blocked
    qint64 mBlockedAt = 0;
    bool   mDispatcherConnected = false;
    QMetaObject::Connection mAboutToBlock, mAwake;
};

#endif   // FRAMEMONITOR_H
