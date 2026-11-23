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
#include <QAbstractItemView>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QThread>
#include <QThreadPool>

#include "irisgl/core/threadbudget.h"
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
    // ITS OWN POOL, two threads of the background tier (irisgl/core/threadbudget.h,
    // THREADING-1): the decode must never queue behind (or starve) the global pool's
    // long jobs — the open's parse, the import runner — and two decoders keep a
    // screenful arriving within a few turns without taking the cores the engine's
    // scene workers draw with.
    mPool = new QThreadPool(this);
    mPool->setMaxThreadCount(std::min(2, iris::threadBudget().background));
    mPool->setObjectName(QStringLiteral("TileCacheDecode"));
}

TileCache::~TileCache() = default;

void TileCache::setSource(Kind kind, Source source)
{
    mSources.insert(int(kind), std::move(source));
}

void TileCache::setDemand(Kind kind, Demand demand)
{
    if (demand) mDemands.insert(int(kind), std::move(demand));
    else mDemands.remove(int(kind));
}

int TileCache::openView(Kind kind)
{
    const int token = mNextToken++;
    ViewSet view;
    view.kind = kind;
    mViews.insert(token, view);
    return token;
}

void TileCache::closeView(int token)
{
    const auto it = mViews.find(token);
    if (it == mViews.end()) return;
    const Kind kind = it->kind;
    const bool known = it->known;
    mViews.erase(it);
    if (known) tellDemand(kind, 0);
}

void TileCache::publishVisible(int token, const QStringList &guids)
{
    const auto it = mViews.find(token);
    if (it == mViews.end()) return;
    it->known = true;
    it->order = guids;
    it->guids = QSet<QString>(guids.cbegin(), guids.cend());
    // AN UNREAD KEY IS ASKED AGAIN HERE, at a publish (a scroll, a resize, a show), never at a paint.
    for (auto u = mUnread.begin(); u != mUnread.end();) {
        if (u->kind != it->kind || !it->guids.contains(u->guid) || mQueued.contains(u.key())) { ++u; continue; }
        Request again = *u;
        again.token = token;
        mQueued.insert(again.key);
        mQueue.append(again);
        u = mUnread.erase(u);
        scheduleFlush();
    }
    tellDemand(it->kind, token);
}

void TileCache::withdrawVisible(int token)
{
    const auto it = mViews.find(token);
    if (it == mViews.end() || !it->known) return;
    it->known = false;
    it->order.clear();
    it->guids.clear();
    tellDemand(it->kind, 0);
}

void TileCache::tellDemand(Kind kind, int first)
{
    const auto demand = mDemands.constFind(int(kind));
    if (demand == mDemands.constEnd()) return;
    // `first`'s rows first (the publisher: what the user is looking at now), then every other shown view's.
    QStringList wanted;
    QSet<QString> listed;
    const auto add = [&](const ViewSet &v) {
        for (const QString &guid : v.order)
            if (!listed.contains(guid)) { listed.insert(guid); wanted.append(guid); }
    };
    if (first) add(mViews.value(first));
    for (auto v = mViews.cbegin(); v != mViews.cend(); ++v)
        if (v.key() != first && v->known && v->kind == kind) add(*v);
    // A copy: the demand may reach back into the cache.
    const Demand call = *demand;
    call(wanted);
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

QPixmap TileCache::tile(Kind kind, const QString &guid, const Spec &spec, int token)
{
    if (guid.isEmpty() || !spec.size.isValid()) return QPixmap();
    if (token) {
        const auto view = mViews.find(token);
        if (view != mViews.end()) {
            view->spec = spec;
            // The view asks for it now: it is on screen now (until its next publish says otherwise).
            if (view->known) view->guids.insert(guid);
        }
    }
    const QString key = keyOf(kind, guid, spec);
    if (const QPixmap *hit = mEntries.object(key)) return *hit;
    if (mMissing.contains(key) || mUnread.contains(key)) return QPixmap();
    if (mQueued.contains(key)) {
        // A tokenless ask makes a queued request always wanted.
        if (!token)
            for (Request &r : mQueue)
                if (r.key == key) r.token = 0;
        return QPixmap();
    }
    mQueued.insert(key);
    mQueue.append({ kind, guid, spec, key, token });
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
    for (auto it = mUnread.begin(); it != mUnread.end();)
        it = it.key().startsWith(prefix) ? mUnread.erase(it) : std::next(it);
    for (auto it = mHashes.begin(); it != mHashes.end();)
        it = it.key().startsWith(prefix) ? mHashes.erase(it) : std::next(it);
    // A request already queued for the old bytes is left alone: it reads the
    // row when its batch runs, which is after this change.
}

void TileCache::renew(Kind kind, const QString &guid)
{
    invalidate(kind, guid);
    for (auto v = mViews.cbegin(); v != mViews.cend(); ++v) {
        if (!v->known || v->kind != kind || !v->spec.size.isValid() || !v->guids.contains(guid)) continue;
        const QString key = keyOf(kind, guid, v->spec);
        if (mQueued.contains(key)) continue;
        mQueued.insert(key);
        mQueue.append({ kind, guid, v->spec, key, v.key() });
        scheduleFlush();
    }
}

void TileCache::clear()
{
    // (The views' tokens and sets stay: they are the views', not the pictures'.)
    mEntries.clear();
    mMissing.clear();
    mUnread.clear();
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

bool TileCache::wanted(const Request &request) const
{
    if (!request.token) return true;
    const auto own = mViews.constFind(request.token);
    if (own != mViews.constEnd() && !own->known) return true;   // its view cannot say: kept
    for (const ViewSet &v : mViews)
        if (v.known && v.kind == request.kind && v.guids.contains(request.guid)) return true;
    return false;
}

void TileCache::flush()
{
    mFlushScheduled = false;
    // WHAT NO SHOWN VIEW WANTS ANY MORE IS DROPPED (a row scrolled past before its turn); it leaves
    // mQueued, so the view's next ask queues it again.
    for (int i = int(mQueue.size()) - 1; i >= 0; --i) {
        if (wanted(mQueue.at(i))) continue;
        mQueued.remove(mQueue.at(i).key);
        mQueue.removeAt(i);
        ++mDropped;
    }
    if (mQueue.isEmpty()) return;

    // ONE KIND per batch, NEWEST FIRST: what the view asked for last is what it
    // is showing now (a scroll's older asks are behind it, or dropped above).
    const Kind kind = mQueue.last().kind;
    QVector<Request> batch;
    QStringList guids;
    for (int i = int(mQueue.size()) - 1; i >= 0 && batch.size() < kBatch; --i) {
        if (mQueue.at(i).kind != kind) continue;
        const Request r = mQueue.takeAt(i);
        batch.append(r);
        if (!guids.contains(r.guid)) guids.append(r.guid);
    }

    // THE READ IS THE POOL'S TOO (TC2-LIBRARY-CREATE-READ, 2026-10-10): the batch's bytes are read
    // by the kind's Source ON THE WORKER, beside the decode — a Desktop of 500 projects read ~95 MB of
    // full-size PNGs on the UI thread, a slice per turn, inside every create that entered it. A kind
    // with no Source answers every guid missing.
    const auto found = mSources.constFind(int(kind));
    const Source source = found != mSources.constEnd() ? *found : Source();
    QVector<Decoded> jobs;
    jobs.reserve(batch.size());
    for (const Request &r : batch) jobs.append({ r, QByteArray(), QImage(), false, false });
    // The next batch reads on the next turn, while this one reads and decodes.
    if (!mQueue.isEmpty()) scheduleFlush();

    ++mInFlight;
    auto *watcher = new QFutureWatcher<QVector<Decoded>>(this);
    connect(watcher, &QFutureWatcher<QVector<Decoded>>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        --mInFlight;
        land(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run(mPool, [jobs, guids, source]() mutable {
        const std::optional<QHash<QString, QByteArray>> bytes =
            source ? source(guids) : std::optional<QHash<QString, QByteArray>>(QHash<QString, QByteArray>());
        for (Decoded &job : jobs) {
            if (!bytes) {
                job.unread = true;
                continue;
            }
            job.png = bytes->value(job.request.guid);
            job.sourced = !job.png.isEmpty();
            QImage full;
            if (job.sourced && full.loadFromData(job.png, "PNG"))
                job.tile = tileImageFor(full, job.request.spec);
            job.png.clear();
        }
        return jobs;
    }));
}

void TileCache::land(const QVector<Decoded> &batch)
{
    QVector<QPair<Kind, QString>> ready;
    QSet<QString> sourced;   // rows read, by guid (a guid asked at two specs is one row)
    for (const Decoded &job : batch)
        if (job.sourced) sourced.insert(job.request.guid);
    mSourced += int(sourced.size());
    for (const Decoded &job : batch) {
        mQueued.remove(job.request.key);
        // UNREAD IS NOT MISSING: no mark, no tile, no repaint - the next publish of a view showing it asks again.
        if (job.unread) {
            mUnread.insert(job.request.key, job.request);
            continue;
        }
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

// ---- TileViewWatcher ---------------------------------------------------------

TileViewWatcher::TileViewWatcher(QAbstractItemView *view, TileCache::Kind kind, int guidRole)
    : QObject(view), mView(view), mGuidRole(guidRole), mToken(TileCache::instance().openView(kind))
{
    view->installEventFilter(this);
    view->viewport()->installEventFilter(this);
    connect(view->verticalScrollBar(), &QScrollBar::valueChanged, this, &TileViewWatcher::schedule);
    connect(view->horizontalScrollBar(), &QScrollBar::valueChanged, this, &TileViewWatcher::schedule);
    connect(&TileCache::instance(), &TileCache::republishRequested, this, &TileViewWatcher::schedule);
    // The model the view shows now (a view whose model is replaced later is not followed: none is).
    if (QAbstractItemModel *model = view->model()) {
        connect(model, &QAbstractItemModel::modelReset, this, &TileViewWatcher::schedule);
        connect(model, &QAbstractItemModel::layoutChanged, this, &TileViewWatcher::schedule);
        connect(model, &QAbstractItemModel::rowsInserted, this, &TileViewWatcher::schedule);
        connect(model, &QAbstractItemModel::rowsRemoved, this, &TileViewWatcher::schedule);
        connect(model, &QAbstractItemModel::rowsMoved, this, &TileViewWatcher::schedule);
    }
    schedule();
}

TileViewWatcher::~TileViewWatcher()
{
    TileCache::instance().closeView(mToken);
}

bool TileViewWatcher::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == mView->viewport() && event->type() == QEvent::Resize) schedule();
    // Hidden (the view, or a page above it): it says nothing until its next publish. Shown (again):
    // ONE publish QUEUED to the next turn, never run inside the show (it reads the visible range and
    // touches no row), so a page shown again fills its missing tiles with no scroll.
    if (watched == mView && event->type() == QEvent::Hide) TileCache::instance().withdrawVisible(mToken);
    if (watched == mView && event->type() == QEvent::Show) schedule();
    return QObject::eventFilter(watched, event);
}

void TileViewWatcher::schedule()
{
    if (mScheduled) return;
    mScheduled = true;
    QMetaObject::invokeMethod(this, [this]() { publish(); }, Qt::QueuedConnection);
}

void TileViewWatcher::publish()
{
    mScheduled = false;
    if (!mView->isVisible()) {
        TileCache::instance().withdrawVisible(mToken);
        return;
    }
    const QStringList guids = visibleGuids();
    TileCache::instance().publishVisible(mToken, guids);
    emit published(guids);
}

QStringList TileViewWatcher::visibleGuids() const
{
    QAbstractItemModel *model = mView->model();
    if (!model) return {};
    const QModelIndex root = mView->rootIndex();
    const int rows = model->rowCount(root);
    if (rows == 0) return {};
    const QListView *list = qobject_cast<const QListView *>(mView);
    const int column = list ? list->modelColumn() : 0;
    // The axis the rows advance along: a list or a wrapping icon grid scrolls down; a strip across.
    const bool across = list && ((list->flow() == QListView::LeftToRight) != list->isWrapping());
    const QRect port = mView->viewport()->rect();
    const auto rectOf = [&](int row) { return mView->visualRect(model->index(row, column, root)); };
    const auto before = [&](const QRect &r) { return across ? r.right() < port.left() : r.bottom() < port.top(); };
    const auto after = [&](const QRect &r) { return across ? r.left() > port.right() : r.top() > port.bottom(); };
    int lo = 0, hi = rows;   // the first row not wholly before the viewport
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        if (before(rectOf(mid))) lo = mid + 1;
        else hi = mid;
    }
    QStringList out;
    for (int row = lo; row < rows; ++row) {
        const QRect r = rectOf(row);
        if (after(r)) break;
        if (!r.intersects(port)) continue;
        const QString guid = model->index(row, 0, root).data(mGuidRole).toString();
        if (!guid.isEmpty()) out.append(guid);
    }
    return out;
}

// ---- ListTileBinder ----------------------------------------------------------

ListTileBinder::ListTileBinder(QListWidget *list, const TileCache::Spec &spec, QObject *parent)
    : QObject(parent ? parent : list), mList(list), mSpec(spec),
      mWatcher(new TileViewWatcher(list, TileCache::Kind::Asset, kGuidRole))
{
    connect(&TileCache::instance(), &TileCache::tileReady, this, &ListTileBinder::onReady);
    connect(mWatcher, &TileViewWatcher::published, this, &ListTileBinder::onPublished);
    // A cleared list's items are gone: so are their indexes.
    connect(list->model(), &QAbstractItemModel::modelReset, this, [this]() {
        mAssigned.clear();
        mWaiting.clear();
    });
}

void ListTileBinder::assign(QListWidgetItem *item, const QString &guid, const QIcon &placeholder)
{
    if (!item || !mList) return;
    item->setData(kGuidRole, guid);
    const QModelIndex index = mList->indexFromItem(item);
    // An item not yet in the list has no index: the caller adds it first.
    if (index.isValid()) {
        QList<QPersistentModelIndex> &shown = mAssigned[guid];
        shown.removeIf([](const QPersistentModelIndex &i) { return !i.isValid(); });
        if (!shown.contains(index)) shown.append(QPersistentModelIndex(index));
    }
    const QPixmap tile = TileCache::instance().tile(TileCache::Kind::Asset, guid, mSpec, token());
    if (!tile.isNull()) {
        item->setIcon(QIcon(tile));
        return;
    }
    item->setIcon(placeholder);
    if (TileCache::instance().isMissing(TileCache::Kind::Asset, guid, mSpec)) return;
    if (index.isValid()) mWaiting.insert(guid);
}

void ListTileBinder::refresh(QListWidgetItem *item, const QString &guid, const QIcon &placeholder)
{
    TileCache::instance().invalidate(TileCache::Kind::Asset, guid);
    assign(item, guid, placeholder);
}

void ListTileBinder::onReady(TileCache::Kind kind, const QString &guid)
{
    if (kind != TileCache::Kind::Asset || !mList) return;
    const QPixmap tile = TileCache::instance().peek(kind, guid, mSpec);
    if (tile.isNull()) {
        // No picture: the placeholder stays for good (a dropped request never lands here).
        if (TileCache::instance().isMissing(kind, guid, mSpec)) mWaiting.remove(guid);
        return;
    }
    mWaiting.remove(guid);
    // Every item of the guid takes it: a waiting placeholder, or a renewed (re-rendered) picture.
    for (const QPersistentModelIndex &index : mAssigned.value(guid))
        if (index.isValid()) mList->model()->setData(index, QIcon(tile), Qt::DecorationRole);
}

void ListTileBinder::onPublished(const QStringList &guids)
{
    for (const QString &guid : guids) {
        if (!mWaiting.contains(guid)) continue;
        // Queued again if a flush dropped it while it was out of view; a hit lands at once.
        if (!TileCache::instance().tile(TileCache::Kind::Asset, guid, mSpec, token()).isNull())
            onReady(TileCache::Kind::Asset, guid);
    }
}
