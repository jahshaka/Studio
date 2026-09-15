/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEOPENRUNNER_H
#define SCENEOPENRUNNER_H

// SceneOpenRunner — opening a world without freezing the window.
//
// The synchronous open blocked the UI thread long enough for GNOME to offer
// to force-quit the app (owner, 2026-09-03). Measured on this machine
// (Debug+ASAN, the owner's build), the Matcaps sample — ONE dragon — spent
// 12.5 s in MainWindow::openProject, and after this lane's three cost fixes
// (services/loadtimeline.h has the ledger) 1.2 s, of which ~0.9 s is assimp
// re-parsing the same model the panel is about to parse a second time.
//
// So the split is drawn where the evidence puts it:
//
//   plan   (UI thread)  — the DB reads: the scene blob, the project's asset
//                         membership, and the CAS paths of every model the
//                         blob and the membership reference. Measured in
//                         single-digit milliseconds; and it CANNOT move,
//                         because Database's methods all run through the
//                         implicit default QSqlDatabase connection, which is
//                         bound to the thread that opened it. ImportBatchRunner
//                         reached the same conclusion and hops its DB half back
//                         to the UI thread rather than opening a second
//                         connection; this follows that precedent.
//   parse  (worker)     — assimp, on every planned model file, into a
//                         MeshPrewarm. Pure file work: no DB, no AssetManager,
//                         no Qt widgets, no engine. This is the second that
//                         used to freeze the window.
//   install(UI thread,  — session registrations, the document read, the panels,
//           SLICED)       the engine push, the page switch. Each slice is a
//                         separate event-loop turn (the ImportTailQueue
//                         precedent), so the loop pumps between them and the
//                         window keeps painting, moving and answering pings.
//
// SHUTDOWN, and why this shape (the import.shutdown scars): the worker NEVER
// blocks on the UI thread — it produces a prewarm and exits, and the queued
// completion checks an abort flag. There is no BlockingQueuedConnection to a
// loop that may have stopped pumping, and nothing this runner owns is deleted
// from inside one of its own slices. requestAbort() + waitForDone() is the
// bounded join the close path calls.
//
// The progress dialog is driven by SIGNALS ONLY. No event-loop pumping from
// inside a slice (ProgressDialog::setPumpsEventLoop's documentation says why:
// a pump re-enters the loop and can destroy the very object mid-call).

#include <QFuture>
#include <QObject>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>

#include "irisgl/import/meshprewarm.h"

class Database;
class Project;

class SceneOpenRunner : public QObject
{
    Q_OBJECT

public:
    explicit SceneOpenRunner(Database *db, Project *project, QObject *parent = nullptr);
    ~SceneOpenRunner() override;

    /// The install slices, in order. Each runs on the UI thread, one per
    /// event-loop turn. Supplied by the shell (they touch widgets and the
    /// engine, which this class deliberately knows nothing about).
    struct Slice
    {
        QString label;              ///< shown in the progress dialog
        int     percent = 0;        ///< dialog value when this slice starts
        std::function<void()> run;
    };

    /// Queue an open. `meshGuidPaths` is the prewarm plan (already
    /// CAS-resolved on the UI thread); `slices` is the install.
    void setPlan(const QStringList &modelPaths, const QVector<Slice> &slices,
                 const QString &label);

    /// WHAT RUNS BETWEEN TWO SLICES, AND WHY THIS EXISTS (lane OPEN-FRAMES-1,
    /// 2026-09-15).
    ///
    /// THE INSTALL MUST NOT DEPEND ON THE APP'S RENDER TICK. The slices upload
    /// a whole world to the GPU and destroy the previous one, and the renderer
    /// only recycles what it was handed when a FRAME turns its resource
    /// bookkeeping (jahshaka::engine::Engine::advanceResources explains which
    /// bookkeeping and why). A frame is a 16 ms timer, and a chain of posted
    /// events — a script polling a verb, a user driving a panel — outranks a
    /// timer in Qt's dispatcher: measured, NO frame at all renders between the
    /// slices of an open driven that way; the backend then runs the whole
    /// install with nothing advanced — and the process ends up with a corrupt
    /// heap (the 512 MB emergency flush one sees in the log is a SYMPTOM that
    /// fires in clean runs too; the writer is unnamed). A frame per slice
    /// cures it (the diagnosis: 33 of 59 frameless scripted opens crashed, 0 of
    /// 24 with one frame per turn; the lane: 9/12 base, 0/12 with the frame).
    ///
    /// So the runner advances the renderer ITSELF, at every slice boundary,
    /// instead of hoping somebody drew. The shell supplies the call (this class
    /// deliberately knows nothing about the engine); a runner with no boundary
    /// set behaves exactly as it did before.
    ///
    /// IT IS NOT A FRAME and must not become one: it draws nothing, presents
    /// nothing and blocks on nothing, so it adds no picture, no vsync wait and
    /// no cost worth measuring to an install that is already the responsiveness
    /// budget's owner.
    void setSliceBoundary(std::function<void()> fn) { mSliceBoundary = std::move(fn); }

    /// How many times the slice boundary has run in this runner's life. The
    /// deterministic half of `open.frames`: an install driven with no frames at
    /// all must still show this rising.
    unsigned boundaryRuns() const { return mBoundaryRuns; }

    /// Start: the worker parses the plan, then the slices run one per turn.
    /// Must be called on the UI thread.
    void start();

    bool isRunning() const { return mRunning.load(); }

    /// The prewarm the worker filled — handed to the readers by the slices.
    const iris::MeshPrewarmPtr &prewarm() const { return mPrewarm; }

    /// Shutdown-grade stop: the worker abandons the rest of the plan and no
    /// further slice runs. Safe from any thread.
    void requestAbort() { mAborted.store(true); }
    bool wasAborted() const { return mAborted.load(); }

    /// Bounded join for the UI thread: pumps queued events (the worker's
    /// completion hop needs servicing) until the runner is idle or msTimeout
    /// elapses. Returns true when it is done.
    ///
    /// This is ALSO how the blocking open waits (MainWindow::openProject,
    /// OPEN-ASSIMP-1): the caller is promised a loaded world, the window is
    /// promised a pumping event loop, and this is both. `idleSleepMs` is the
    /// nap between pumps — the shutdown join can afford five, an open that a
    /// script is waiting on pays it once per slice, so it passes one.
    bool waitForDone(int msTimeout, int idleSleepMs = 5);

signals:
    /// Progress for the dialog: (percent, text). Queued from the worker,
    /// direct from the slices — never pumped.
    void progress(int percent, const QString &text);
    /// Every slice ran (or the run was abandoned). `aborted` says which.
    void finished(bool aborted);

private:
    void runWorker();     ///< worker-thread body
    void runNextSlice();  ///< UI thread, one slice per event-loop turn
    void crossSliceBoundary();   ///< see setSliceBoundary

    Database *db = nullptr;
    Project *project;

    QStringList mModelPaths;
    /// The same paths with their bakes resolved (MESH_BAKE_SPEC phase 1),
    /// filled by setPlan on the UI thread because bake lookup is a database
    /// query and QSqlDatabase connections are per-thread.
    QVector<iris::PrewarmItem> mPlan;
    QVector<Slice> mSlices;
    QString mLabel;
    int mNextSlice = 0;

    iris::MeshPrewarmPtr mPrewarm;
    std::function<void()> mSliceBoundary;
    unsigned mBoundaryRuns = 0;
    QFuture<void> mFuture;
    std::atomic<bool> mRunning { false };
    std::atomic<bool> mAborted { false };
    std::atomic<bool> mParsed { false };
};

#endif   // SCENEOPENRUNNER_H
