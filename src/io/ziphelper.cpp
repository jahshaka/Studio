/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "io/ziphelper.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include "zip.h"

namespace ZipHelper
{

bool zipDirectory(const QString &dirPath, const QString &zipPath, QString *errorOut,
                  const EntryFn &onEntry)
{
    const QDir baseDir(dirPath);
    if (!baseDir.exists()) {
        if (errorOut) *errorOut = QStringLiteral("no such directory %1").arg(dirPath);
        return false;
    }
    QFile::remove(zipPath);

    // The entry count first, so the callback can report a real percentage.
    // A second QDirIterator pass over an already-warm directory listing costs
    // nothing next to compressing the files it found.
    int total = 0;
    if (onEntry) {
        QDirIterator count(dirPath, QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs,
                           QDirIterator::Subdirectories);
        while (count.hasNext()) { count.next(); ++total; }
    }

    struct zip_t *zip = zip_open(zipPath.toStdString().c_str(),
                                 ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');
    if (!zip) {
        if (errorOut) *errorOut = QStringLiteral("cannot create %1").arg(zipPath);
        return false;
    }

    QDirIterator it(dirPath, QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs,
                    QDirIterator::Subdirectories);
    bool ok = true;
    bool canceled = false;
    int index = 0;
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        const QString relative = baseDir.relativeFilePath(path);
        if (info.isDir()) {
            // A trailing '/' makes zip create the (possibly empty) directory.
            zip_entry_open(zip, QString(relative + "/").toStdString().c_str());
            zip_entry_close(zip);
        } else {
            zip_entry_open(zip, relative.toStdString().c_str());
            if (zip_entry_fwrite(zip, path.toStdString().c_str()) != 0) {
                if (errorOut) *errorOut = QStringLiteral("failed writing %1").arg(relative);
                ok = false;
            }
            zip_entry_close(zip);
            if (!ok) break;
        }
        // BETWEEN entries, never inside one: a half-written entry is not
        // something the caller can be given a choice about.
        if (onEntry && !onEntry(relative, ++index, total)) { canceled = true; break; }
    }

    zip_close(zip);
    // A cancelled archive is an incomplete archive: it goes the same way a
    // failed one does, so nothing is left claiming to be a project.
    if (!ok || canceled) QFile::remove(zipPath);
    if (canceled && errorOut && errorOut->isEmpty())
        *errorOut = QStringLiteral("cancelled");
    return ok && !canceled;
}

namespace {

struct ExtractContext
{
    const ZipHelper::EntryFn *onEntry = nullptr;
    int index = 0;
    int total = 0;
    bool canceled = false;
};

// zip_extract's contract: a NEGATIVE return aborts the extraction.
int onExtractEntry(const char *filename, void *arg)
{
    auto *ctx = static_cast<ExtractContext *>(arg);
    if (!ctx || !ctx->onEntry) return 0;
    if (!(*ctx->onEntry)(QString::fromUtf8(filename ? filename : ""), ++ctx->index, ctx->total)) {
        ctx->canceled = true;
        return -1;
    }
    return 0;
}

}   // namespace

bool extract(const QString &zipPath, const QString &destDir, QString *errorOut,
             const EntryFn &onEntry)
{
    if (!QFileInfo::exists(zipPath)) {
        if (errorOut) *errorOut = QStringLiteral("no such archive %1").arg(zipPath);
        return false;
    }
    QDir().mkpath(destDir);

    ExtractContext ctx;
    ctx.onEntry = onEntry ? &onEntry : nullptr;
    if (onEntry) {
        // The denominator, read from the archive's own directory.
        if (struct zip_t *probe = zip_open(zipPath.toStdString().c_str(), 0, 'r')) {
            const ssize_t n = zip_entries_total(probe);
            if (n > 0) ctx.total = int(n);
            zip_close(probe);
        }
    }

    if (zip_extract(zipPath.toStdString().c_str(), destDir.toStdString().c_str(),
                    onEntry ? onExtractEntry : nullptr, onEntry ? &ctx : nullptr) != 0) {
        if (errorOut)
            *errorOut = ctx.canceled ? QStringLiteral("cancelled")
                                     : QStringLiteral("could not extract %1").arg(zipPath);
        return false;
    }
    return true;
}

} // namespace ZipHelper
