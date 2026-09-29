/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/bundlewriter.h"

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
    if (QFile::exists(destPath) && !QFile::remove(destPath)) {
        QFile::remove(partial);
        return fail(QStringLiteral("the existing file at %1 could not be replaced").arg(destPath));
    }
    if (!QFile::rename(partial, destPath)) {
        QFile::remove(partial);
        return fail(QStringLiteral("the archive could not be moved into place at %1").arg(destPath));
    }

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
