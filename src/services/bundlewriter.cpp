/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/bundlewriter.h"
#include "services/filewriteatomic.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QTemporaryDir>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>

#include "export/exportmanifest.h"
#include "io/ziphelper.h"
#include "zip.h"

namespace assetshare {

namespace {
QString manifestName() { return QStringLiteral("jah.manifest.json"); }
QString payloadName()  { return QStringLiteral("payload.json"); }

/// The bytes the stage owes (assetclosure::describe's deferred list), read into
/// the envelope. A file that cannot be opened travels by its oid — the same
/// outcome describe's inline path gives it. False only when `step` stops it.
bool readOwed(QMap<QString, clipboardformat::ClipAsset> &assets,
              const QVector<assetclosure::DeferredRead> &reads, const std::function<bool()> &step)
{
    for (const assetclosure::DeferredRead &read : reads) {
        auto asset = assets.find(read.guid);
        if (asset != assets.end() && read.fileIndex >= 0 && read.fileIndex < asset->files.size()) {
            QFile source(read.path);
            if (source.open(QIODevice::ReadOnly))
                asset->files[read.fileIndex].inlineData = source.readAll();
        }
        if (!step()) return false;
    }
    return true;
}
}   // namespace

ExportResult writeBundle(BundleStage stage, const QString &destPath, const WriteProgressFn &progress)
{
    ExportResult result;
    const auto fail = [&result](const QString &why) { result.error = why; return result; };
    result.offUiThread = QCoreApplication::instance()
                         && QThread::currentThread() != QCoreApplication::instance()->thread();
    if (!stage.ok()) return fail(stage.error);
    if (destPath.isEmpty()) return fail(QStringLiteral("a destination is required"));

    // THE STEPS a progress reader counts: each owed file, then the payload,
    // then the archive.
    const int total = int(stage.reads.size()) + 2;
    int done = 0;
    const auto step = [&]() { return !progress || progress(++done, total); };
    const auto canceled = [&result]() {
        result.canceled = true;
        result.error = QStringLiteral("cancelled");
        return result;
    };

    // 1. THE BYTES (file I/O; the stage already chose which travel inline).
    if (!readOwed(stage.envelope.assets, stage.reads, step)) return canceled();

    // 2. THE MANIFEST — the readable header, v2 — and THE PAYLOAD, the
    // envelope itself.
    const clipboardformat::Envelope &envelope = stage.envelope;
    exportformat::ExportManifest manifest;
    manifest.version = 2;
    manifest.kind = stage.kind;
    manifest.generator = QStringLiteral("Jahshaka");
    manifest.created = envelope.created;
    for (auto it = envelope.assets.constBegin(); it != envelope.assets.constEnd(); ++it) {
        exportformat::ManifestAsset entry;
        entry.guid = it.key();
        entry.name = it->name;
        entry.type = it->type;
        entry.typeId = it->typeId;
        entry.dependencies = it->dependencies;
        for (const clipboardformat::ClipFile &file : it->files) {
            exportformat::ManifestFile mf;
            mf.role = file.role;
            mf.name = file.name;
            mf.size = file.size;
            mf.oid = file.oid;
            entry.files.append(mf);
        }
        manifest.assets.append(entry);
    }

    QTemporaryDir staging;
    if (!staging.isValid()) return fail(QStringLiteral("cannot create a staging directory"));
    QString manifestError;
    if (!manifest.write(QDir(staging.path()).filePath(manifestName()), &manifestError))
        return fail(manifestError.isEmpty() ? QStringLiteral("could not write the manifest")
                                            : manifestError);
    {
        QFile payload(QDir(staging.path()).filePath(payloadName()));
        if (!payload.open(QIODevice::WriteOnly))
            return fail(QStringLiteral("could not write the payload"));
        const QByteArray bytes = envelope.toText();
        if (payload.write(bytes) != bytes.size())
            return fail(QStringLiteral("a short write"));
    }
    if (!step()) return canceled();

    // 3. THE ARCHIVE, WRITTEN BESIDE AND RENAMED OVER: a file already at
    // `destPath` is replaced only by a complete archive, so a failed export
    // never leaves the user with less than they had.
    const QString partial = destPath + QStringLiteral(".partial");
    QFile::remove(partial);
    QString zipError;
    if (!ZipHelper::zipDirectory(staging.path(), partial, &zipError)) {
        QFile::remove(partial);
        return fail(zipError.isEmpty() ? QStringLiteral("could not write the archive") : zipError);
    }
    if (progress && !progress(total, total)) {
        QFile::remove(partial);
        return canceled();
    }
    // ONE rename(2) THAT REPLACES (RENAME-ATOMIC-1): the destination is the
    // old file until the instant it is the new one. It used to be removed
    // first and the .partial renamed in after (QFile::rename refuses to
    // overwrite), which left a window with no file at all — and a rename that
    // failed inside it lost the user's file for good. FileWrite::atomicRename
    // is the house's one atomic publish; it removes the .partial on failure.
    QString renameError;
    if (!FileWrite::atomicRename(partial, destPath, &renameError))
        return fail(QStringLiteral("the archive could not be moved into place: %1").arg(renameError));

    result.path = destPath;
    result.kind = stage.kind;
    result.assets = envelope.assets.size();
    result.bytes = QFileInfo(destPath).size();
    return result;
}

BundleExport::BundleExport(BundleStage stage, const QString &destPath)
    : mShared(std::make_shared<Shared>()), mStage(std::move(stage)), mDest(destPath)
{
    mShared->total.store(int(mStage.reads.size()) + 2);
}

BundleExport::~BundleExport()
{
    // Nothing the worker touches lives here (it holds its own copy of the
    // stage and a share of the counters), so the join is for order, not
    // memory: the caller that dropped the job never sees a file appear later.
    if (mFuture.isValid() && !mFuture.isFinished()) {
        mShared->cancel.store(true);
        mFuture.waitForFinished();
    }
}

void BundleExport::start()
{
    if (mStarted) return;
    mStarted = true;
    std::shared_ptr<Shared> shared = mShared;
    BundleStage stage = std::move(mStage);
    const QString dest = mDest;
    mFuture = QtConcurrent::run([shared, stage = std::move(stage), dest]() mutable {
        return writeBundle(std::move(stage), dest, [shared](int done, int total) {
            shared->done.store(done);
            shared->total.store(total);
            return !shared->cancel.load();
        });
    });
}

void BundleExport::cancel() { mShared->cancel.store(true); }
bool BundleExport::isRunning() const { return mFuture.isValid() && !mFuture.isFinished(); }
bool BundleExport::isFinished() const { return mFuture.isValid() && mFuture.isFinished(); }
int BundleExport::done() const { return mShared->done.load(); }
int BundleExport::total() const { return mShared->total.load(); }

ExportResult BundleExport::wait(QEventLoop::ProcessEventsFlags flags)
{
    if (!mStarted) start();
    if (!mFuture.isFinished() && QCoreApplication::instance()) {
        QEventLoop loop;
        QFutureWatcher<ExportResult> watcher;
        QObject::connect(&watcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(mFuture);
        // The future may have finished between the check and the connection;
        // the watcher then reports it on its first event, which the loop runs.
        if (!mFuture.isFinished()) loop.exec(flags);
    }
    // An application quitting ends every loop early: the write is still
    // bounded file I/O, so it is joined rather than abandoned mid-rename.
    mFuture.waitForFinished();
    return mFuture.result();
}

namespace {
/// One entry of an open archive, read whole into memory — refused (false,
/// `why`) when it is absent or its uncompressed size exceeds `cap`.
bool readEntry(struct zip_t *zip, const QString &name, qint64 cap, QByteArray *out, QString *why)
{
    if (zip_entry_open(zip, name.toUtf8().constData()) != 0) {
        *why = QStringLiteral("this archive carries no %1").arg(name);
        return false;
    }
    const qint64 size = qint64(zip_entry_size(zip));
    bool ok = size >= 0 && size <= cap;
    if (!ok) {
        *why = QStringLiteral("%1 is %2 bytes, more than its manifest accounts for (%3)")
                   .arg(name).arg(size).arg(cap);
    } else {
        out->resize(size);
        ok = size == 0 || zip_entry_noallocread(zip, out->data(), size_t(size)) == ssize_t(size);
        if (!ok) *why = QStringLiteral("%1 could not be read").arg(name);
    }
    zip_entry_close(zip);
    return ok;
}
}   // namespace

UnpackedBundle unpackBundle(const QString &path)
{
    UnpackedBundle out;
    if (!QFileInfo::exists(path)) {
        out.error = QStringLiteral("no such file '%1'").arg(path);
        return out;
    }
    // READ FROM THE ARCHIVE, BOUNDED — never extracted (a share file lands in a
    // QTemporaryDir that is RAM on this box) and never read past what its own
    // manifest declares. The manifest is small by construction; the payload
    // is the closure's bytes as base64 plus the rows' JSON, so it may be at
    // most 4/3 of the manifest's summed file sizes plus a per-row allowance and
    // a fixed allowance for the row blobs. A payload larger than that is a
    // file that is not what its header says, and it is refused with the reason
    // before a byte of it is read.
    struct zip_t *zip = zip_open(path.toUtf8().constData(), 0, 'r');
    if (!zip) {
        out.error = QStringLiteral("this file is not a readable archive");
        return out;
    }
    constexpr qint64 kManifestCap = 16ll * 1024 * 1024;
    constexpr qint64 kPerRowAllowance = 64ll * 1024;
    constexpr qint64 kBlobAllowance = 64ll * 1024 * 1024;
    QByteArray manifestBytes, payload;
    QString why;
    if (!readEntry(zip, manifestName(), kManifestCap, &manifestBytes, &why)) {
        zip_close(zip);
        out.error = why;
        return out;
    }
    QString manifestError;
    const exportformat::ExportManifest manifest =
        exportformat::ExportManifest::fromBytes(manifestBytes, &manifestError);
    if (!manifestError.isEmpty()) {
        zip_close(zip);
        out.error = QStringLiteral("the manifest is unreadable: %1").arg(manifestError);
        return out;
    }
    qint64 declared = 0;
    for (const auto &asset : manifest.assets)
        for (const auto &file : asset.files)
            if (file.size > 0) declared += file.size;
    const qint64 cap = declared / 3 * 4 + 4 + qint64(manifest.assets.size()) * kPerRowAllowance
                       + kBlobAllowance;
    const bool read = readEntry(zip, payloadName(), cap, &payload, &why);
    zip_close(zip);
    if (!read) {
        out.error = why;
        return out;
    }
    QString envelopeError;
    // NOT THE CLIPBOARD'S 64 MB CAP: a share file carries its whole closure
    // inline on purpose (the portability rule); its bound is the manifest's.
    out.envelope = clipboardformat::Envelope::fromText(payload, &envelopeError, cap);
    if (out.envelope.items.isEmpty())
        out.error = envelopeError.isEmpty() ? QStringLiteral("the payload names no asset")
                                            : envelopeError;
    return out;
}

ExportResult exportAndWait(BundleStage stage, const QString &destPath)
{
    if (!stage.ok()) {
        ExportResult result;
        result.error = stage.error;
        return result;
    }
    BundleExport job(std::move(stage), destPath);
    job.start();
    return job.wait(QEventLoop::ExcludeUserInputEvents);
}

}   // namespace assetshare
