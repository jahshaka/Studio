/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROJECTARCHIVER_H
#define PROJECTARCHIVER_H

// Project archives, pin-world edition (ASSET_PIPELINE_SPEC §3.1.5/§3.3,
// phase 4). A project archive (.zip) is self-contained:
//
//   jah.manifest.json      manifest v2, kind "project": per-asset file lists
//                          {role, name, oid} — the pinned content ids
//   <projectGuid>.db       the catalog snapshot (projects/assets/deps rows —
//                          the same blob db Database::createExportScene has
//                          always written, now pin-aware)
//   objects/<oid>.<ext>    the pinned bytes, materialized from the CAS
//
// Export materializes pinned oids (a reference-based project leaves the
// machine self-contained); import ingests the objects CAS-first, imports the
// catalog rows, and writes fresh pins — NO flat project-folder file copies
// exist any more, on either side. The bundled sample scenes ship in exactly
// this format.
//
// ===========================================================================
// THREADING (STABILITY_PROGRAM_SPEC.md Lane 4, REBASED shape)
// ===========================================================================
// Zipping a project froze the window for as long as it took. The split is the
// one SceneOpenRunner and ImportBatchRunner already landed, for the same
// reason they landed it:
//
//   plan     (UI thread) — every DATABASE read: the catalog snapshot, the
//                          membership sweep, the dependency edges, the pins,
//                          and the CAS paths those resolve to. It cannot move.
//                          Database's methods all ride the implicit default
//                          QSqlDatabase connection, which is bound to the
//                          thread that opened it; ImportBatchRunner hops its
//                          DB half BACK to the UI thread rather than opening
//                          a second connection, and SceneOpenRunner reached
//                          the same conclusion independently. So do we. There
//                          is no per-thread connection here and there must
//                          not be one.
//   worker   (worker)    — the file half: copying CAS objects into the stage,
//                          writing the manifest, and ZipHelper compress /
//                          extract. No DB, no widgets, no Qt event loop. This
//                          is the part that used to freeze the window.
//   install  (UI thread,  — the catalog writes an import makes, in CHUNKS: one
//             SLICED)      asset per event-loop turn, 1 ms apart (not 0 — a
//                          chain of zero timers is always "due" and crowds out
//                          the render tick; measured in the open lane). Export
//                          has no install work beyond publishing the result.
//
// SCARS ARE LAW (import.shutdown, and SceneOpenRunner's header): no
// BlockingQueuedConnection anywhere — a UI loop that stopped pumping must
// never be able to strand the worker; nothing this class owns is deleted from
// inside one of its own slices; every queued lambda uses `this` as its context
// object so Qt drops it when the runner dies. The progress dialog is driven by
// SIGNALS with setPumpsEventLoop(false).
//
// CANCEL is honoured between zip/extract entries and between install slices.
// An abandoned export deletes its partial archive (ZipHelper does it); an
// abandoned import deletes the project row it had started building, so a
// cancel leaves no orphan either side.

#include <QFuture>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>

#include "export/exportmanifest.h"

class Database;
class Project;
class QTemporaryDir;

class ProjectArchiver : public QObject
{
    Q_OBJECT

public:
    struct Result
    {
        QString error;
        QString path;          // export: the archive; import: extracted temp
        QString projectGuid;   // import: the NEW project guid
        QString worldName;     // import: the project's display name
        int assets = 0;
        int objects = 0;
        bool canceled = false;
        bool ok() const { return error.isEmpty(); }
    };

    /// `project` is only needed for exports (it names the open project).
    explicit ProjectArchiver(Database *db, Project *project = nullptr,
                             QObject *parent = nullptr);
    ~ProjectArchiver() override;

    // ---- synchronous: the same three phases, inline on the calling thread --
    // What scripts, headless runs and the correctness suites use. Identical
    // output to the threaded path by construction — they are the same phase
    // functions in the same order.
    Result exportArchive(const QString &destZipPath);
    Result importArchive(const QString &zipPath);

    // ---- threaded: plan here, file work on a worker, install back here -----
    /// Both start on the UI thread and return immediately. finished() carries
    /// the outcome; result() holds the details.
    bool startExport(const QString &destZipPath);
    bool startImport(const QString &zipPath);

    bool isRunning() const { return mRunning.load(); }
    const Result &result() const { return mResult; }

    /// Cooperative, safe from any thread. Honoured between zip/extract entries
    /// and between install slices.
    void requestCancel() { mCanceled.store(true); }
    bool wasCanceled() const { return mCanceled.load(); }

    /// Bounded join for the UI thread: pumps queued events (the worker's
    /// completion hop and the install slices need servicing) until this
    /// archiver is idle or msTimeout elapses. Returns true when it is done.
    bool waitForDone(int msTimeout);

    /// Every live archiver, cancelled and joined within msTimeout TOTAL.
    /// Step 2 of the shutdown order (shell/shutdownorder.h) calls this; it is
    /// the archive twin of AssetWidget::shutdownImports.
    static bool shutdownArchives(int msTimeout);

signals:
    /// (percent 0..100, text). Queued from the worker, direct from the UI
    /// phases — never pumped. ProgressDialog listens with
    /// setPumpsEventLoop(false).
    void progress(int percent, const QString &text);
    /// The operation ended. `canceled` distinguishes an abandoned run from a
    /// failed one; read result() for both.
    void finished(bool canceled);

private:
    // The three phases, per direction. Each returns false on failure and
    // fills mResult.error.
    bool planExport(const QString &destZipPath);
    bool workExport();                 ///< worker thread (or inline)
    void installExport();

    bool planImport(const QString &zipPath);
    bool workImport();                 ///< worker thread (or inline)
    void installImportSlice();         ///< UI thread, one asset per turn
    void beginInstallImport();

    void emitProgress(int percent, const QString &text);
    void finish(bool canceled);
    void runWorkerThread();

    Database *db = nullptr;
    Project *project = nullptr;

    Result mResult;
    std::atomic<bool> mRunning { false };
    std::atomic<bool> mCanceled { false };
    bool mThreaded = false;
    bool mExporting = false;

    // ---- the plan, handed from phase to phase --------------------------
    QString mDestZip;                        ///< export destination
    QString mSourceZip;                      ///< import source
    QTemporaryDir *mStage = nullptr;         ///< the staging directory
    exportformat::ExportManifest mManifest;  ///< export: built in plan, written in worker
    struct Copy { QString src; QString dst; };
    QVector<Copy> mCopies;                   ///< export: CAS objects to materialize

    // import: what the worker found on disk, ready for the install slices
    struct IngestFile { QString path; QString role; QString name; };
    struct IngestAsset { QString archiveGuid; QVector<IngestFile> files; };
    QVector<IngestAsset> mIngest;
    QFuture<void> mFuture;
    QString mBlobDbBase;
    int mNextIngest = 0;
    QMap<QString, QString> mGuidMap;

    static QVector<ProjectArchiver *> sLive;   ///< UI thread only
};

#endif // PROJECTARCHIVER_H
