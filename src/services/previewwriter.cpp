/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/previewwriter.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QSqlDatabase>
#include <QUuid>

#include "data/database/database.h"
#include "irisgl/core/logger.h"

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
        Item item;
        item.guid = guid;
        item.image = image;   // implicitly shared; read-only on the worker
        item.key = key;
        item.done = std::move(done);
        mQueue.push_back(std::move(item));
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
            irisLog(QStringLiteral("previews: the writer could not open %1 (%2) — no preview is stored this session")
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
                }
                write = Database::writePreview(conn, item.guid, item.png, item.key);
            }

            lock.lock();
            if (write.busy && !stopping) {
                // DROPPED PAST THE BUDGET: said, and back at the HEAD so the order holds.
                ++item.drops;
                const QString why = write.reason;
                irisLog(QStringLiteral("previews: the write of %1 was dropped: %2 — it re-queues").arg(item.guid, why));
                if (mDropHook) {
                    DropHook hook = mDropHook;
                    const QString guid = item.guid;
                    post(mAlive, [hook, guid, why]() { hook(guid, why); });
                }
                mQueue.push_front(std::move(item));
                continue;
            }
            lock.unlock();

            landed.ok = write.ok;
            landed.reason = write.ok ? QString() : write.reason;
            landed.drops = item.drops;
            if (!write.ok)
                irisLog(QStringLiteral("previews: the preview of %1 was not stored: %2").arg(item.guid, write.reason));
            Done done = std::move(item.done);
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
