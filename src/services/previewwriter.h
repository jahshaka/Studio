/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SERVICES_PREVIEWWRITER_H
#define SERVICES_PREVIEWWRITER_H

// THE ONE PREVIEW WRITER (PREVIEWS-1, the lead's ruling 3; spec call 6).
//
// A preview's bytes and its key (services/previewkey.h) are DURABLE writes, so they never run on
// the UI thread: this worker owns a connection of its own to the library file (the precedents:
// Database::readThumbnailBytes and the WAL checkpointer), set to the library's durability pragmas,
// and writes `assets.thumbnail` and the `thumbnails` row {guid, hash = key, last_written} in ONE
// transaction — a tile is never stored without the key it was drawn from, nor a key without its
// tile. It also encodes the PNG, so the UI thread hands over a QImage and nothing else.
//
//   - SUBMIT RETURNS AT ONCE: a null image is refused there (nothing queued, nothing changes); every
//     other write is queued and the caller goes on, even while the UI connection holds the lock.
//   - SUBMIT ORDER: writes land one at a time in the order submitted, ONE ROW A TRANSACTION, so a UI
//     write that meets the writer waits for one row at most (the UI connection's busy timeout).
//   - A NAMED BUDGET: the writer waits for the library's lock at most kBudgetMs (SQLite's busy
//     timeout on its connection). Past it the write is DROPPED with its reason — told to the drop
//     hook — and RE-QUEUED AT THE HEAD, so the order holds; it is tried again at once. A UI batch can
//     hold the lock for a whole script run (Database::beginBatch), so the writer is QUIET while it
//     waits: one log line when a write first waits, one when it lands (its tries, the ms waited).
//   - BOUNDED PER GUID: at most ONE write of a guid waits (beside the one in flight). A newer submit
//     of the same guid REPLACES the waiting one in its place in the queue; the replaced write's
//     completion says so (not ok, "superseded"), in submit order. A preview older than one already
//     asked for is never worth storing.
//   - THE LOCK IS HELD FOR THE ROW ONLY: the PNG is encoded, and every byte allocated, BEFORE the
//     transaction begins (Database::writePreview takes bytes, never an image), so a UI BEGIN behind
//     the writer waits for two statements, never an encode.
//   - A FAILED WRITE CHANGES NOTHING: an empty PNG, a row that is gone, any statement's failure
//     roll the transaction back.
//   - THE COMPLETION and the library's thumbnail hook (the tile cache drops the guid's tile) are
//     POSTED to the application's thread; one that arrives after the writer is gone is dropped.
//   - THE END: the destructor lets the queue drain (each write under its budget) and joins; a write
//     still locked out then is lost — derived data, it reads stale at the next session.

#include <QElapsedTimer>
#include <QImage>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace previews {

class Writer
{
public:
    /// The writer's budget: how long one write waits for the library's lock before it is dropped.
    static constexpr int kBudgetMs = 2000;
    /// THE QUEUE'S CAP ACROSS GUIDS: at most this many writes pending (queued or in flight), each holding
    /// its picture (1 MiB a 512-px tile) until it lands. A sweep at the cap waits for a landing
    /// (PreviewService::SweepOptions::waitForLanding); a single request is never refused by it.
    static constexpr int kQueueCap = 8;

    struct Landed
    {
        QString guid;
        QString key;
        bool ok = false;
        QString reason;   ///< empty when ok
        int drops = 0;    ///< how often this write was dropped past its budget before it landed
        bool superseded = false;   ///< replaced by a newer write of its guid: not stored, not a failure
        qint64 waitedMs = 0;   ///< from its first drop to its landing; 0 when never dropped
        /// THE ORDER OF ITS EVENTS, on the writer's one serial (each event takes the next number): its
        /// PNG's encode ending, and its (last) transaction beginning. encodedAt < begunAt is the law that
        /// the lock is held for the row only.
        quint64 encodedAt = 0;
        quint64 begunAt = 0;
    };
    using Done = std::function<void(const Landed &)>;
    using DropHook = std::function<void(const QString &guid, const QString &reason)>;

    /// `libraryFile` is the open library's file (Database::filePath). `budgetMs` is kBudgetMs in
    /// the product; a suite hands a short one to reach the drop without a long wait.
    explicit Writer(const QString &libraryFile, int budgetMs = kBudgetMs);
    ~Writer();
    Writer(const Writer &) = delete;
    Writer &operator=(const Writer &) = delete;

    /// Queues one preview. False with `reasonOut` for a null image (nothing is queued and `done` is
    /// not called); true otherwise, and `done` is called on the application's thread once it landed
    /// or failed.
    bool submit(const QString &guid, const QImage &image, const QString &key, Done done,
                QString *reasonOut = nullptr);

    /// Told of every drop past the budget, on the application's thread.
    void setDropHook(DropHook hook);

    /// Writes submitted and not yet completed (queued or in flight). Any thread.
    int pending() const { return mPending.load(); }

private:
    struct Item
    {
        QString guid;
        QImage image;
        QByteArray png;   ///< encoded once, kept across a drop
        QString key;
        Done done;
        int drops = 0;
        qint64 firstDropMs = 0;   ///< the writer's clock at its first drop
        quint64 encodedAt = 0;    ///< the serial when its encode ended (once: kept across a drop)
    };
    void run();

    QElapsedTimer mClock;   ///< for the waited ms of a log line, never a decision
    const QString mPath;
    const int mBudgetMs;
    std::shared_ptr<bool> mAlive;   ///< read and cleared on the application's thread only
    std::atomic<int> mPending{ 0 };
    quint64 mSerial = 0;   ///< the order of the worker's events; the worker's only
    DropHook mDropHook;
    std::mutex mMutex;
    std::condition_variable mWake;
    std::deque<Item> mQueue;
    bool mStop = false;
    std::thread mThread;
};

}   // namespace previews

#endif   // SERVICES_PREVIEWWRITER_H
