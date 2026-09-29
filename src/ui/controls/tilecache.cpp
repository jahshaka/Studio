/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/tilecache.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QImage>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

namespace {

int costKB(const QPixmap &pm)
{
    return qMax(1, int(qint64(pm.width()) * pm.height() * 4 / 1024));
}

QPainterPath topCornersClip(int w, int h, int radius)
{
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, w, h), radius, radius);
    path.addRect(QRectF(0, h - radius, w, radius));
    return path.simplified();
}

}   // namespace

// The card's two top corners, clipped on a QImage — callable on a worker
// (QPainter on a QImage is; on a QPixmap it is not).
QImage tileImageFor(const QImage &full, const TileCache::Spec &spec)
{
    if (full.isNull() || !spec.size.isValid()) return QImage();
    const QImage scaled = full.scaled(spec.size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (spec.cornerRadius <= 0) return scaled;
    QImage out(scaled.size(), QImage::Format_ARGB32_Premultiplied);
    out.setDevicePixelRatio(scaled.devicePixelRatio());
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipPath(topCornersClip(scaled.width(), scaled.height(), spec.cornerRadius));
    p.drawImage(0, 0, scaled);
    return out;
}

TileCache &TileCache::instance()
{
    // Emptied while the application still exists: a QPixmap outliving its
    // QGuiApplication is undefined behaviour on some platforms. Leaked on
    // purpose after that (a static QObject destroyed after the app is the
    // same hazard in the other direction).
    static TileCache *cache = [] {
        auto *c = new TileCache;
        qAddPostRoutine([] {
            // THE POOL FIRST (the review, D11): a quit with a batch decoding
            // would leave a worker inside QImage during static destruction —
            // queued jobs are dropped, a running one finishes (bounded: one
            // batch of tile decodes), then the pictures go.
            TileCache &cache = instance();
            cache.mPool->clear();
            cache.mPool->waitForDone(5000);
            cache.mQueue.clear();
            cache.mEntries.clear();
        });
        return c;
    }();
    return *cache;
}

TileCache::TileCache()
{
    // ITS OWN POOL, two threads: the decode must never queue behind (or starve)
    // the global pool's long jobs — the open's parse, the import runner — and two
    // decoders keep a screenful arriving within a few turns without taking the
    // cores the engine's scene workers draw with.
    mPool = new QThreadPool(this);
    mPool->setMaxThreadCount(2);
    mPool->setObjectName(QStringLiteral("TileCacheDecode"));
}

TileCache::~TileCache() = default;

void TileCache::setSource(Kind kind, Source source)
{
    mSources.insert(int(kind), std::move(source));
}

QString TileCache::keyOf(Kind kind, const QString &guid, const Spec &spec)
{
    return QStringLiteral("%1|%2|%3x%4|%5")
        .arg(int(kind))
        .arg(guid)
        .arg(spec.size.width())
        .arg(spec.size.height())
        .arg(spec.cornerRadius);
}

QPixmap TileCache::peek(Kind kind, const QString &guid, const Spec &spec) const
{
    if (guid.isEmpty()) return QPixmap();
    if (const QPixmap *hit = mEntries.object(keyOf(kind, guid, spec))) return *hit;
    return QPixmap();
}

bool TileCache::isMissing(Kind kind, const QString &guid, const Spec &spec) const
{
    return mMissing.contains(keyOf(kind, guid, spec));
}

QPixmap TileCache::tile(Kind kind, const QString &guid, const Spec &spec)
{
    if (guid.isEmpty() || !spec.size.isValid()) return QPixmap();
    const QString key = keyOf(kind, guid, spec);
    if (const QPixmap *hit = mEntries.object(key)) return *hit;
    if (mMissing.contains(key) || mQueued.contains(key)) return QPixmap();
    mQueued.insert(key);
    mQueue.append({ kind, guid, spec, key });
    scheduleFlush();
    return QPixmap();
}

QPixmap TileCache::supply(Kind kind, const QString &guid, const QByteArray &png, const Spec &spec)
{
    const QString key = keyOf(kind, guid, spec);
    // THE SAME BYTES ARE NOT DECODED TWICE (CREATE-GAP-1's rule, kept): a tile
    // rebuilt with the picture the cache already holds for it is a hit.
    const size_t hash = qHash(png) ^ size_t(png.size());
    if (const QPixmap *hit = mEntries.object(key))
        if (mHashes.value(key) == hash) return *hit;
    invalidate(kind, guid);
    mHashes.insert(key, hash);
    QImage full;
    if (png.isEmpty() || !full.loadFromData(png, "PNG")) {
        mMissing.insert(key);
        return QPixmap();
    }
    ++mDecodes;
    const QPixmap pm = QPixmap::fromImage(tileImageFor(full, spec));
    mEntries.insert(key, new QPixmap(pm), costKB(pm));
    return pm;
}

void TileCache::invalidate(Kind kind, const QString &guid)
{
    const QString prefix = QStringLiteral("%1|%2|").arg(int(kind)).arg(guid);
    for (const QString &key : mEntries.keys())
        if (key.startsWith(prefix)) mEntries.remove(key);
    for (auto it = mMissing.begin(); it != mMissing.end();)
        it = it->startsWith(prefix) ? mMissing.erase(it) : std::next(it);
    for (auto it = mHashes.begin(); it != mHashes.end();)
        it = it.key().startsWith(prefix) ? mHashes.erase(it) : std::next(it);
    // A request already queued for the old bytes is left alone: it reads the
    // row when its batch runs, which is after this change.
}

void TileCache::clear()
{
    mEntries.clear();
    mMissing.clear();
    mHashes.clear();
    mQueue.clear();
    mQueued.clear();
}

void TileCache::scheduleFlush()
{
    if (mFlushScheduled) return;
    mFlushScheduled = true;
    // A QUEUED call, never a synchronous read: a paint (the usual caller) must
    // not run a query; the batch runs on the next turn of the loop.
    QMetaObject::invokeMethod(this, [this]() { flush(); }, Qt::QueuedConnection);
}

void TileCache::flush()
{
    mFlushScheduled = false;
    if (mQueue.isEmpty()) return;

    // ONE KIND per batch, in request order (what the view asked for first is
    // what it is showing).
    const Kind kind = mQueue.first().kind;
    QVector<Request> batch;
    QStringList guids;
    for (int i = 0; i < mQueue.size() && batch.size() < kBatch;) {
        if (mQueue.at(i).kind != kind) { ++i; continue; }
        const Request r = mQueue.takeAt(i);
        batch.append(r);
        if (!guids.contains(r.guid)) guids.append(r.guid);
    }

    QHash<QString, QByteArray> bytes;
    const auto source = mSources.constFind(int(kind));
    if (source != mSources.constEnd() && *source) bytes = (*source)(guids);
    mSourced += bytes.size();

    QVector<Decoded> jobs;
    jobs.reserve(batch.size());
    for (const Request &r : batch) {
        const QByteArray png = bytes.value(r.guid);
        if (png.isEmpty()) {
            mQueued.remove(r.key);
            mMissing.insert(r.key);
            continue;
        }
        jobs.append({ r, png, QImage() });
    }
    // The next batch reads on the next turn, while this one decodes.
    if (!mQueue.isEmpty()) scheduleFlush();
    if (jobs.isEmpty()) return;

    ++mInFlight;
    auto *watcher = new QFutureWatcher<QVector<Decoded>>(this);
    connect(watcher, &QFutureWatcher<QVector<Decoded>>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        --mInFlight;
        land(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run(mPool, [jobs]() mutable {
        for (Decoded &job : jobs) {
            QImage full;
            if (full.loadFromData(job.png, "PNG")) job.tile = tileImageFor(full, job.request.spec);
            job.png.clear();
        }
        return jobs;
    }));
}

void TileCache::land(const QVector<Decoded> &batch)
{
    QVector<QPair<Kind, QString>> ready;
    for (const Decoded &job : batch) {
        mQueued.remove(job.request.key);
        if (job.tile.isNull()) {
            mMissing.insert(job.request.key);
        } else {
            ++mDecodes;
            const QPixmap pm = QPixmap::fromImage(job.tile);
            mEntries.insert(job.request.key, new QPixmap(pm), costKB(pm));
        }
        // A missing picture is news too: the view stops waiting and paints its
        // placeholder for good.
        ready.append({ job.request.kind, job.request.guid });
    }
    for (const auto &r : ready) emit tileReady(r.first, r.second);
}

bool TileCache::drain(int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (pending() > 0) {
        if (t.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (mInFlight > 0 && mQueue.isEmpty()) QThread::msleep(1);
    }
    return true;
}

// ---- ListTileBinder ----------------------------------------------------------

ListTileBinder::ListTileBinder(QListWidget *list, const TileCache::Spec &spec, QObject *parent)
    : QObject(parent ? parent : list), mList(list), mSpec(spec)
{
    connect(&TileCache::instance(), &TileCache::tileReady, this, &ListTileBinder::onReady);
}

void ListTileBinder::assign(QListWidgetItem *item, const QString &guid, const QIcon &placeholder)
{
    if (!item || !mList) return;
    const QPixmap tile = TileCache::instance().tile(TileCache::Kind::Asset, guid, mSpec);
    if (!tile.isNull()) {
        item->setIcon(QIcon(tile));
        return;
    }
    item->setIcon(placeholder);
    if (TileCache::instance().isMissing(TileCache::Kind::Asset, guid, mSpec)) return;
    const QModelIndex index = mList->indexFromItem(item);
    // An item not yet in the list has no index: the caller adds it first.
    if (index.isValid()) mWaiting[guid].append(QPersistentModelIndex(index));
}

void ListTileBinder::refresh(QListWidgetItem *item, const QString &guid, const QIcon &placeholder)
{
    TileCache::instance().invalidate(TileCache::Kind::Asset, guid);
    assign(item, guid, placeholder);
}

void ListTileBinder::onReady(TileCache::Kind kind, const QString &guid)
{
    if (kind != TileCache::Kind::Asset || !mList) return;
    const auto waiting = mWaiting.take(guid);
    const QPixmap tile = TileCache::instance().peek(kind, guid, mSpec);
    if (tile.isNull()) return;   // no picture: the placeholder stays
    for (const QPersistentModelIndex &index : waiting)
        if (index.isValid()) mList->model()->setData(index, QIcon(tile), Qt::DecorationRole);
}
