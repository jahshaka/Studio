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
    bool waitForDone(int msTimeout);

signals:
    /// Progress for the dialog: (percent, text). Queued from the worker,
    /// direct from the slices — never pumped.
    void progress(int percent, const QString &text);
    /// Every slice ran (or the run was abandoned). `aborted` says which.
    void finished(bool aborted);

private:
    void runWorker();     ///< worker-thread body
    void runNextSlice();  ///< UI thread, one slice per event-loop turn

    Database *db;
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
    QFuture<void> mFuture;
    std::atomic<bool> mRunning { false };
    std::atomic<bool> mAborted { false };
    std::atomic<bool> mParsed { false };
};

#endif   // SCENEOPENRUNNER_H
