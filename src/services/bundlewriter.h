/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef BUNDLEWRITER_H
#define BUNDLEWRITER_H

// THE SHARE FILE'S WRITER, OFF THE UI THREAD (EXPORT-THREAD-1).
//
// A share file (services/assetshare.h) is two halves, and the split is the
// owner's rule "no blocking I/O on the UI thread":
//
//   STAGE (the caller's thread — the UI thread, which owns the catalog's
//   default connection): the closure's rows, edges and store paths, read from
//   the database. Queries only; no byte of content is read.
//
//   WRITE (any thread — a worker): every owed file read from the store, the
//   envelope serialised, the manifest written, the zip built and renamed into
//   place. File I/O only: no database, no widget, no event loop.
//
// `BundleExport` runs the write on a QtConcurrent worker. The verbs wait for
// it with the calling thread's event loop turning (`wait`), so a verb still
// returns only when the file is complete while the window keeps drawing; the
// UI hands the same job to a progress dialog (ui/dialogs/bundleexportdialog.h)
// that can cancel it.

#include <QEventLoop>
#include <QFuture>
#include <QString>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>

#include "io/clipboardformat.h"
#include "services/assetclosure.h"

namespace assetshare {

struct ExportResult
{
    QString path;      ///< the archive written
    QString kind;      ///< the manifest's kind ("material", "texture", "pack", …)
    int assets = 0;    ///< rows carried (the assets plus their closure)
    qint64 bytes = 0;  ///< the archive's size
    /// The write ran on a thread other than the application's (the UI) thread
    /// — what the verbs report as `worker`, so a test can count it.
    bool offUiThread = false;
    bool canceled = false;
    QString error;
    bool ok() const { return error.isEmpty() && !path.isEmpty(); }
};

/// What the UI thread hands the writer: the envelope with its rows and file
/// entries, the files whose bytes are still owed, and the manifest's kind.
struct BundleStage
{
    clipboardformat::Envelope envelope;
    QString kind;
    QVector<assetclosure::DeferredRead> reads;
    QString error;
    bool ok() const { return error.isEmpty(); }
};

/// Progress of a write: (steps done, steps total). Runs on the WRITING thread;
/// returning false abandons the write (the result is `canceled`).
using WriteProgressFn = std::function<bool(int done, int total)>;

/// THE ONE WRITER. Reads the owed bytes, writes `jah.manifest.json` and
/// `payload.json`, zips them beside `destPath` and renames over it — a file
/// already there is replaced only by a complete archive. Thread-safe as long
/// as `stage` is the caller's own (it is taken by value).
ExportResult writeBundle(BundleStage stage, const QString &destPath,
                         const WriteProgressFn &progress = WriteProgressFn());

/// A write in flight on a worker. Not copyable; destroying it cancels the
/// write and joins the worker (bounded: file I/O between progress checks).
class BundleExport
{
public:
    BundleExport(BundleStage stage, const QString &destPath);
    ~BundleExport();
    BundleExport(const BundleExport &) = delete;
    BundleExport &operator=(const BundleExport &) = delete;

    /// Starts the worker (once).
    void start();
    /// Asks the worker to stop at its next step. Any thread.
    void cancel();
    bool isRunning() const;
    bool isFinished() const;
    /// Steps done / total so far (the owed files, then the zip's entries).
    int done() const;
    int total() const;
    /// The future the worker answers on (valid after start()).
    QFuture<ExportResult> future() const { return mFuture; }
    /// Waits for the file with the calling thread's event loop turning under
    /// `flags` — frames, repaints and timers keep running while the worker
    /// writes — and returns its result. The verbs pass ExcludeUserInputEvents
    /// (nothing a click could start re-enters them); the progress dialog runs
    /// its own modal loop instead.
    ExportResult wait(QEventLoop::ProcessEventsFlags flags = QEventLoop::ExcludeUserInputEvents);

private:
    struct Shared
    {
        std::atomic<bool> cancel{ false };
        std::atomic<int> done{ 0 };
        std::atomic<int> total{ 0 };
    };
    std::shared_ptr<Shared> mShared;
    BundleStage mStage;
    QString mDest;
    QFuture<ExportResult> mFuture;
    bool mStarted = false;
};

/// The verbs' export: the stage written on a worker, waited for with the
/// event loop turning (user input held back). Returns when the file is
/// complete.
ExportResult exportAndWait(BundleStage stage, const QString &destPath);

}   // namespace assetshare

#endif   // BUNDLEWRITER_H
