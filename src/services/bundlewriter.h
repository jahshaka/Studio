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

// THE SHARE FILE'S WRITER (JAF-EXPORTS-1: every export in the app writes this
// one format).
//
// A share file (services/assetshare.h) is two halves:
//
//   STAGE (the caller's thread — the UI thread, which owns the catalog's
//   default connection): the closure's rows, edges and store paths, read from
//   the database. Queries only; no byte of content is read.
//
//   WRITE (file I/O only: no database, no widget, no event loop): every owed
//   file read from the store, the envelope serialised, the manifest written,
//   the zip built and renamed into place.

#include <QString>
#include <QVector>

#include <functional>

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
    /// — what the verbs report as `worker`.
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

}   // namespace assetshare

#endif   // BUNDLEWRITER_H
