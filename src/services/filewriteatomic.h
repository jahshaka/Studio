/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef FILEWRITEATOMIC_H
#define FILEWRITEATOMIC_H

// THE ONE ATOMIC FILE WRITE (deep audit 2026-09, area 6).
//
// AssetCas::storeObject earned this the hard way: a file written in place is
// a file that LIES about its content between the truncate and the last byte.
// For a content-addressed object that is fatal (the name is the sha256 and
// nothing re-hashes on read); for a sidecar, a store.json or a baked map it
// is merely a corrupt artifact that survives forever, because every reader
// checks existence, not integrity. The audit found three writers still doing
// exactly what storeObject exists to prevent — AssetCas::writeSidecar,
// AssetCas::writeStoreInfo and the materials baker's PNG writes — so the
// tail of storeObject lives here, header-only, reachable from services/ and
// from modules/.
//
// The shape is always: stage into a SIBLING temp (same directory ⇒ same
// filesystem ⇒ rename(2) is atomic), flush the bytes, then one rename that
// REPLACES the target. A crash therefore leaves either the old file or the
// new one, never a half of either. The directory entry is deliberately NOT
// fsynced: losing the rename loses the write, and a missing artifact is a
// re-write, not a corruption.
//
// THE FLUSH IS A POLICY, NOT A CONSTANT (FSYNC-2, 2026-09-17). fsync() is the
// only call in this file that waits for hardware, and on this box it waits
// for the WHOLE filesystem's journal: measured 220-1 153 ms per call, per
// artifact, while anything else was writing to the same device. So the
// question every writer has to answer is what a LOST or TORN file costs, and
// the two answers are different enough to name:
//
//   Durability::Flush    the bytes ARE the artifact and nothing re-derives
//                        them — a CAS object (its name is a sha256 claim
//                        about content nobody re-hashes on read), store.json,
//                        a baked map. Torn = silent corruption that outlives
//                        the crash. These flush, always.
//   Durability::Derived  the file is a PROJECTION of something else that is
//                        itself durable, and re-writing it is a function call
//                        — the CAS sidecar is literally a SELECT over the
//                        catalog. Losing it costs a re-write; tearing it
//                        cannot be mistaken for content, because a truncated
//                        JSON document does not parse and every reader of
//                        these files skips what does not parse (the rebuild's
//                        `guid.isEmpty()` guard, assetmigration.cpp). These
//                        keep the atomic rename and skip the wait — the same
//                        trade this header already makes for the directory
//                        entry, one level up.
//
// std::filesystem::rename, not QFile::rename: Qt's refuses when the target
// exists (documented), which is precisely the case that must work.

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <atomic>
#include <filesystem>
#include <functional>
#include <system_error>

#ifdef Q_OS_UNIX
#include <unistd.h>   // fsync(2)
#endif

namespace FileWrite
{

/// What a lost or torn copy of this file costs — see the header comment.
enum class Durability
{
    Flush,    ///< the bytes are the artifact: fsync before the rename
    Derived   ///< a projection of durable state: atomic rename, no wait
};

/// A unique sibling temp path for `finalPath` — same directory, so the
/// rename below stays within one filesystem. pid + serial keeps concurrent
/// writers (and the import worker) from colliding.
inline QString stagingTempPath(const QString &finalPath)
{
    static std::atomic<unsigned> sSerial{ 0 };
    return QStringLiteral("%1.tmp-%2-%3")
        .arg(finalPath)
        .arg(QCoreApplication::applicationPid())
        .arg(sSerial.fetch_add(1, std::memory_order_relaxed));
}

/// Flush a staged temp's bytes to the device. No-op off UNIX (Windows/macOS
/// callers get the rename's ordering only — the same guarantee storeObject
/// shipped with).
inline void fsyncPath(const QString &path)
{
#ifdef Q_OS_UNIX
    QFile flushed(path);
    if (flushed.open(QIODevice::ReadWrite)) {
        ::fsync(flushed.handle());
        flushed.close();
    }
#else
    Q_UNUSED(path);
#endif
}

/// The atomic publish: rename `tmpPath` over `finalPath`, removing the temp
/// on failure. The path conversion is per-platform on purpose — a narrow
/// std::string reaches std::filesystem::path in the platform's NARROW
/// encoding, which on Windows is not UTF-8, so a non-ASCII path would
/// silently rename the wrong thing.
inline bool atomicRename(const QString &tmpPath, const QString &finalPath,
                         QString *errorOut = nullptr)
{
    std::error_code ec;
#ifdef Q_OS_WIN
    const std::filesystem::path tmpFs(tmpPath.toStdWString());
    const std::filesystem::path dstFs(finalPath.toStdWString());
#else
    const std::filesystem::path tmpFs(QFile::encodeName(tmpPath).toStdString());
    const std::filesystem::path dstFs(QFile::encodeName(finalPath).toStdString());
#endif
    std::filesystem::rename(tmpFs, dstFs, ec);
    if (ec) {
        QFile::remove(tmpPath);
        if (errorOut)
            *errorOut = QStringLiteral("could not place %1: %2")
                            .arg(finalPath, QString::fromStdString(ec.message()));
        return false;
    }
    return true;
}

/// Write `path` through a staged temp: the caller's `writer` fills the OPEN
/// temp QFile (QImage::save(&file, "PNG"), a QDataStream, anything), then the
/// bytes are flushed and renamed into place. Returning false from `writer`
/// aborts the write and leaves the existing file untouched.
inline bool writeFileAtomic(const QString &path,
                            const std::function<bool(QFile &)> &writer,
                            QString *errorOut = nullptr,
                            Durability durability = Durability::Flush)
{
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir)) {
        if (errorOut) *errorOut = QStringLiteral("cannot create %1").arg(dir);
        return false;
    }

    const QString tmpPath = stagingTempPath(path);
    QFile::remove(tmpPath);   // a leftover from a dead run that had our pid

    {
        QFile out(tmpPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (errorOut) *errorOut = QStringLiteral("cannot write %1").arg(path);
            return false;
        }
        if (!writer(out)) {
            out.close();
            QFile::remove(tmpPath);
            if (errorOut && errorOut->isEmpty())
                *errorOut = QStringLiteral("nothing written for %1").arg(path);
            return false;
        }
        out.flush();
        out.close();
    }

    if (durability == Durability::Flush) fsyncPath(tmpPath);
    return atomicRename(tmpPath, path, errorOut);
}

/// The byte-array overload — sidecars, store.json, any serialized document.
inline bool writeFileAtomic(const QString &path, const QByteArray &bytes,
                            QString *errorOut = nullptr,
                            Durability durability = Durability::Flush)
{
    return writeFileAtomic(path, [&bytes](QFile &out) {
        return out.write(bytes) == bytes.size();
    }, errorOut, durability);
}

} // namespace FileWrite

#endif // FILEWRITEATOMIC_H
