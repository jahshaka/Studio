/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/previewwriter.h"

#include <algorithm>

#include <QBuffer>
#include <QCoreApplication>
#include <QSqlDatabase>
#include <QUuid>

#include "data/database/database.h"
#include "services/jahlog.h"

namespace previews {
namespace {

QByteArray encodePng(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) return QByteArray();
    return bytes;
}

/// Runs `fn` on the application's thread, unless the writer that posted it is gone by then.
void post(const std::shared_ptr<bool> &alive, std::function<void()> fn)
{
    QCoreApplication *app = QCoreApplication::instance();
    if (!app) return;
    QMetaObject::invokeMethod(app, [alive, fn = std::move(fn)]() {
        if (*alive) fn();
    }, Qt::QueuedConnection);
}

}   // namespace

Writer::Writer(const QString &libraryFile, int budgetMs)
    : mPath(libraryFile), mBudgetMs(budgetMs), mAlive(std::make_shared<bool>(true))
{
    mClock.start();
    mThread = std::thread([this]() { run(); });
}

Writer::~Writer()
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStop = true;
    }
    mWake.notify_all();
    if (mThread.joinable()) mThread.join();
    *mAlive = false;
}

void Writer::setDropHook(DropHook hook)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mDropHook = std::move(hook);
}

bool Writer::submit(const QString &guid, const QImage &image, const QString &key, Done done,
                    QString *reasonOut)
{
    if (image.isNull()) {
        if (reasonOut) *reasonOut = QStringLiteral("the render produced no image (nothing is stored)");
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mMutex);
        for (Item &waiting : mQueue) {
            if (waiting.guid != guid) continue;
            // ONE WAITING WRITE A GUID: the newer replaces it, in its place.
            Landed superseded;
            superseded.guid = guid;
            superseded.key = waiting.key;
            superseded.reason = QStringLiteral("superseded by a newer preview of the same row");
            superseded.superseded = true;
            superseded.drops = waiting.drops;
            if (waiting.done) {
                Done old = std::move(waiting.done);
                post(mAlive, [old, superseded]() { old(superseded); });
            }
            waiting.image = image;
            waiting.png.clear();
            waiting.key = key;
            waiting.done = std::move(done);
            return true;
        }
        Item item;
        item.guid = guid;
        item.image = image;   // implicitly shared; read-only on the worker
        item.key = key;
        item.done = std::move(done);
        mQueue.push_back(std::move(item));
        ++mPending;
    }
    mWake.notify_one();
    return true;
}

void Writer::run()
{
    const QString name = QStringLiteral("PreviewWriter-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QString openError;
    {
        QSqlDatabase conn;
        const bool open = Database::openPreviewConnection(&conn, mPath, name, mBudgetMs, &openError);
        if (!open)
            JahLog::write(JahLog::legacy, JahLog::Level::Log, QStringLiteral("previews: the writer could not open %1 (%2) — no preview is stored this session")
                                                                  .arg(mPath, openError));

        std::unique_lock<std::mutex> lock(mMutex);
        for (;;) {
            mWake.wait(lock, [this] { return mStop || !mQueue.empty(); });
            if (mQueue.empty()) break;   // stopped, and drained
            Item item = std::move(mQueue.front());
            mQueue.pop_front();
            const bool stopping = mStop;
            lock.unlock();

            Landed landed;
            landed.guid = item.guid;
            landed.key = item.key;
            Database::PreviewWrite write;
            if (!open) {
                write.reason = QStringLiteral("the preview writer has no connection to the library: %1").arg(openError);
            } else {
                if (item.png.isEmpty() && !item.image.isNull()) {
                    item.png = encodePng(item.image);
                    item.image = QImage();
                    item.encodedAt = ++mSerial;
                }
                landed.begunAt = ++mSerial;   // writePreview's first statement is its BEGIN IMMEDIATE
                write = Database::writePreview(conn, item.guid, item.png, item.key);
            }

            lock.lock();
            if (write.busy && !stopping) {
                // DROPPED PAST THE BUDGET: said, and back at the HEAD so the order holds.
                const QString why = write.reason;
                if (item.drops == 0) {
                    item.firstDropMs = mClock.elapsed();
                    JahLog::write(JahLog::legacy, JahLog::Level::Log, QStringLiteral("previews: the write of %1 waits behind the library's lock (%2); "
                                                                                     "it re-queues at the head until it lands").arg(item.guid, why));
                }
                ++item.drops;
                if (mDropHook) {
                    DropHook hook = mDropHook;
                    const QString guid = item.guid;
                    post(mAlive, [hook, guid, why]() { hook(guid, why); });
                }
                // A NEWER write of the same guid already waits: this one is superseded, never a
                // second waiting write of the guid.
                const bool newer = std::any_of(mQueue.begin(), mQueue.end(),
                                               [&item](const Item &q) { return q.guid == item.guid; });
                if (newer) {
                    Landed superseded;
                    superseded.guid = item.guid;
                    superseded.key = item.key;
                    superseded.reason = QStringLiteral("superseded by a newer preview of the same row");
                    superseded.superseded = true;
                    superseded.drops = item.drops;
                    if (item.done) {
                        Done old = std::move(item.done);
                        post(mAlive, [old, superseded]() { old(superseded); });
                    }
                    --mPending;
                    continue;
                }
                mQueue.push_front(std::move(item));
                continue;
            }
            lock.unlock();

            landed.ok = write.ok;
            landed.reason = write.ok ? QString() : write.reason;
            landed.drops = item.drops;
            landed.encodedAt = item.encodedAt;
            landed.waitedMs = item.drops > 0 ? mClock.elapsed() - item.firstDropMs : 0;
            if (item.drops > 0)
                JahLog::write(JahLog::legacy, JahLog::Level::Log, QStringLiteral("previews: the write of %1 %2 after %3 tries, %4 ms behind the lock")
                                                                      .arg(item.guid, write.ok ? QStringLiteral("landed") : QStringLiteral("failed"))
                                                                      .arg(item.drops + 1).arg(landed.waitedMs));
            if (!write.ok)
                JahLog::write(JahLog::legacy, JahLog::Level::Log, QStringLiteral("previews: the preview of %1 was not stored: %2").arg(item.guid, write.reason));
            Done done = std::move(item.done);
            --mPending;
            post(mAlive, [landed, done]() {
                if (landed.ok) Database::announceAssetThumbnailWritten(landed.guid);
                if (done) done(landed);
            });
            lock.lock();
        }
        lock.unlock();
        if (conn.isOpen()) conn.close();
    }
    QSqlDatabase::removeDatabase(name);
}

}   // namespace previews
