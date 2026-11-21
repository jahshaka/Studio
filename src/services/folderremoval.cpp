/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/folderremoval.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QThreadPool>

namespace {

enum class Removed { Whole, Partly, Stopped };

/// QDir::removeRecursively's walk (a link is removed, never followed; a read-only file is made writable once),
/// with the stop read before every entry - the only difference, and the reason it is not the Qt call.
Removed removeTree(const QString &path, const std::atomic_bool &stop)
{
    bool whole = true;
    const QFileInfoList entries =
        QDir(path).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (stop.load(std::memory_order_relaxed)) return Removed::Stopped;
        const QString file = entry.absoluteFilePath();
        if (entry.isDir() && !entry.isSymLink()) {
            const Removed sub = removeTree(file, stop);
            if (sub == Removed::Stopped) return sub;
            whole = whole && sub == Removed::Whole;
        } else if (!QFile::remove(file)) {
            whole = QFile::setPermissions(file, QFile::permissions(file) | QFile::WriteUser) && QFile::remove(file) && whole;
        }
    }
    if (!QDir().rmdir(path)) whole = false;
    return whole ? Removed::Whole : Removed::Partly;
}

}   // namespace

FolderRemoval::~FolderRemoval()
{
    drain(kQuitCapMs);
}

bool FolderRemoval::owns(const QString &path) const
{
    QMutexLocker locked(&mShared->lock);
    return mShared->owned.contains(path);
}

int FolderRemoval::start(const QStringList &paths)
{
    QStringList taken;
    {
        QMutexLocker locked(&mShared->lock);
        for (const QString &path : paths) {
            if (mShared->owned.contains(path)) continue;
            mShared->owned.insert(path);
            taken.append(path);
        }
    }
    if (taken.isEmpty()) return 0;
    ++mRunning;
    QThreadPool::globalInstance()->start([taken, shared = mShared]() {
        for (const QString &path : taken) {
            const Removed removed = removeTree(path, shared->stop);
            if (removed == Removed::Stopped)
                qWarning("project delete: '%s' was not removed in full before the quit's limit; it stays aside and "
                         "the next start removes it", qUtf8Printable(path));
            else if (removed == Removed::Partly)
                qWarning("project delete: '%s' could not be removed in full; the next delete in its directory, or "
                         "the next start, tries again", qUtf8Printable(path));
            QMutexLocker locked(&shared->lock);
            shared->owned.remove(path);
        }
        shared->done.release();
    });
    return int(taken.size());
}

FolderRemoval::Drained FolderRemoval::drain(int capMs)
{
    Drained drained;
    drained.waited = mRunning;
    if (mRunning == 0) return drained;
    int finished = 0;
    // Every worker that ends inside the cap is counted one by one: the stopped ones are the rest.
    const QDeadlineTimer deadline(capMs);
    while (finished < mRunning && mShared->done.tryAcquire(1, deadline)) ++finished;
    if (finished < mRunning) {
        drained.stopped = mRunning - finished;
        mShared->stop.store(true, std::memory_order_relaxed);
        mShared->done.acquire(mRunning - finished);   // one file or one rmdir each, at most
        mShared->stop.store(false, std::memory_order_relaxed);
    }
    mRunning = 0;
    return drained;
}
