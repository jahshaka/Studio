/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef FOLDERREMOVAL_H
#define FOLDERREMOVAL_H

#include <atomic>
#include <memory>

#include <QMutex>
#include <QSemaphore>
#include <QSet>
#include <QString>
#include <QStringList>

/// THE BYTES OF A DELETED PROJECT, REMOVED ON A WORKER - AND A QUIT NEVER WAITS FOR THEM WITHOUT A LIMIT
/// (STUDIO-HYGIENE-1; the final read 2's M1). A project's folder is renamed aside on the calling thread
/// (ProjectService::removeProject); its bytes go here, one worker per start() on Qt's global pool (the background
/// tier). A drain - the shutdown's, the destructor's - waits at most its cap, then STOPS every worker between two
/// entries and joins it (one file or one rmdir at most): what is left stays aside under its `.removing-` name, and
/// the next start's sweep (ProjectService's constructor) removes it.
class FolderRemoval
{
public:
    /// The quit's cap: every other wait of the shutdown's (ShellLifecycle::stopBackgroundWork) is 3000 ms.
    static constexpr int kQuitCapMs = 3000;

    FolderRemoval() = default;
    FolderRemoval(const FolderRemoval &) = delete;
    FolderRemoval &operator=(const FolderRemoval &) = delete;
    /// Drains at kQuitCapMs: a worker never outlives its owner.
    ~FolderRemoval();

    /// Removes every path of `paths` no worker of this owner holds yet, on ONE worker; returns how many it took.
    /// The calling thread only (the owner's).
    int start(const QStringList &paths);
    /// True while a worker of this owner holds `path`.
    bool owns(const QString &path) const;

    struct Drained {
        int waited = 0;    ///< workers waited for
        int stopped = 0;   ///< ...of which STOPPED at the cap, their folders left aside for the next start's sweep
    };
    /// Waits for every worker at most `capMs` (0: not at all), then stops the rest and joins them.
    Drained drain(int capMs);

private:
    /// What a worker shares with its owner (a worker may outlive nothing, but holds no `this`).
    struct Shared {
        mutable QMutex lock;
        QSet<QString> owned;           ///< the paths a worker holds (under `lock`)
        std::atomic_bool stop{false};  ///< a drain's cap passed: every worker returns at its next entry
        QSemaphore done;               ///< one release per finished worker
    };
    std::shared_ptr<Shared> mShared = std::make_shared<Shared>();
    int mRunning = 0;   ///< workers started and not yet drained (the owner's thread only)
};

#endif // FOLDERREMOVAL_H
