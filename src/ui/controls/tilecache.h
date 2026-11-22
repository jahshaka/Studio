/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef TILECACHE_H
#define TILECACHE_H

#include <QByteArray>
#include <QCache>
#include <QHash>
#include <QObject>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QPointer>
#include <QIcon>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QThreadPool;

/// THE SESSION'S TILE CACHE (D11-LIBRARY-SCALE; CREATE-GAP-1's Desktop cache made
/// the one cache every thumbnail listing paints from).
///
/// A LISTING NEVER CARRIES A THUMBNAIL. A panel lists guids (a query with no BLOB
/// column) and asks this cache for each tile it PAINTS: a hit is the tile-sized
/// picture; a miss answers a null pixmap (the panel paints its placeholder) and
/// queues the guid. The queue is drained in small batches, one per event-loop
/// turn: the BYTES are read by ONE `WHERE guid IN (...)` per batch through the
/// kind's Source and DECODED and scaled to the tile, both on the cache's own
/// thread pool (the read moved off the UI thread: TC2-LIBRARY-CREATE-READ), and `tileReady` names
/// every guid whose picture landed. So a view over 10,000 assets reads and
/// decodes the ~50 tiles it shows, never the library.
///
/// ENTRIES ARE TILE-SIZED (the bound charges what is held: 128 MB is ~940 Desktop
/// tiles at Normal and ~7,000 Library tiles). A guid with no thumbnail, or bytes
/// that do not decode, is remembered as MISSING so a repaint does not re-queue it;
/// `invalidate` forgets both (a thumbnail was rebuilt, a project re-saved).
/// WHAT IS DECODED IS WHAT A VIEW STILL SHOWS (PREVIEWS-1 stage 2, LARGE-LIBRARY-1 call 4): a view
/// that shows tiles holds a TOKEN (openView; TileViewWatcher below does it for an item view) and
/// PUBLISHES its visible guids; `flush` decodes the NEWEST request first and drops a queued request
/// of a token in NO live view's published set (it leaves the queue, so a later ask queues it again).
/// The view STATES what it wants: nothing is inferred from paints (a visible row painted once and
/// waiting is never painted again). An ask with a token between two publishes joins that token's
/// set until the next publish replaces it (a paint can land before its scroll's publish). A token
/// that has not published, or whose view is hidden, is UNKNOWN: its own asks are kept, never
/// dropped. An ask with no token (a dialog's single tile, a verb, the Desktop grid) is always wanted.
/// Each publish hands the wanted guids (the publisher's first) to the kind's DEMAND (setDemand: the
/// shell's render demand) — a hidden view publishes nothing, so a page not shown renders nothing.
/// UI thread only (QPixmap) — except the Sources, which the pool calls.
class TileCache : public QObject
{
    Q_OBJECT

public:
    /// What a guid names: the two tables whose rows carry a thumbnail.
    enum class Kind { Asset, Project };
    Q_ENUM(Kind)

    /// How a decoded picture becomes a tile: scaled to fit `size` (aspect kept),
    /// the top corners rounded by `cornerRadius` (the Desktop's shape) when > 0.
    struct Spec
    {
        QSize size;
        int cornerRadius = 0;
    };

    /// The bytes of `guids` (absent = no thumbnail). Called ON THE CACHE'S POOL, with
    /// at most kBatch guids, possibly two batches at once: it opens its own read
    /// connection (Database::readThumbnailBytes) and touches nothing of the UI thread's.
    using Source = std::function<QHash<QString, QByteArray>(const QStringList &guids)>;

    static constexpr int kBatch = 48;
    static constexpr int kBudgetKB = 128 * 1024;

    static TileCache &instance();

    void setSource(Kind kind, Source source);

    /// THE RENDER DEMAND of a kind: told, at each publish, every guid a shown view wants (the
    /// publishing view's visible guids first, in its order, then the other shown views').
    using Demand = std::function<void(const QStringList &wanted)>;
    void setDemand(Kind kind, Demand demand);

    /// A view that shows tiles of `kind`: its token (never 0) until closeView.
    int openView(Kind kind);
    /// The view is gone: its set with it; its queued requests are dropped at the next flush unless
    /// another shown view wants their guid.
    void closeView(int token);
    /// The view's VISIBLE guids now (top-left first), replacing its set; then the kind's demand.
    void publishVisible(int token, const QStringList &guids);
    /// The view cannot say (it is hidden): UNKNOWN until its next publish — its asks are kept.
    void withdrawVisible(int token);
    /// Every live view publishes once more (its own queued publish): a script run ended, and the
    /// render demand it suspended hears what is on screen now.
    void republishAll() { emit republishRequested(); }

    /// The tile, or a null pixmap while it is on its way (the request is queued). `token` 0: wanted
    /// whatever any view shows; else the asking view's token.
    QPixmap tile(Kind kind, const QString &guid, const Spec &spec, int token = 0);
    /// The tile if held; never queues (a caller that only wants to know).
    QPixmap peek(Kind kind, const QString &guid, const Spec &spec) const;
    /// True when `guid` has no decodable thumbnail at `spec` (its placeholder
    /// is final until `invalidate`).
    bool isMissing(Kind kind, const QString &guid, const Spec &spec) const;

    /// Bytes the caller already holds (a save's new thumbnail): decoded
    /// SYNCHRONOUSLY into the cache for `spec` — one picture, the one on screen.
    QPixmap supply(Kind kind, const QString &guid, const QByteArray &png, const Spec &spec);
    /// Forget every entry of `guid` (all specs), missing marks included.
    void invalidate(Kind kind, const QString &guid);
    /// A NEW PICTURE WAS STORED for `guid`: invalidate, and queue it again at each shown view's
    /// size when a shown view wants it (the view repaints when it lands: tileReady).
    void renew(Kind kind, const QString &guid);
    /// Forget EVERYTHING and drop the queue (a library reset; a suite that
    /// measures a cold cache). Batches already decoding land into the empty cache.
    void clear();

    /// PNG decodes performed this session (suites prove a rebuild decodes nothing seen).
    int decodes() const { return mDecodes; }
    /// Rows read through the Sources this session (the no-list-BLOB proof's other half).
    int sourcedRows() const { return mSourced; }
    /// Queued requests dropped because no shown view wanted them any more (this session).
    int dropped() const { return mDropped; }
    /// Requests queued or decoding.
    int pending() const { return int(mQueue.size()) + mInFlight; }
    /// Pumps the event loop until nothing is pending (tests, and a caller that
    /// must hand a finished picture to a script). False on timeout.
    bool drain(int timeoutMs = 10000);

signals:
    void tileReady(TileCache::Kind kind, const QString &guid);
    /// republishAll: every TileViewWatcher schedules its publish.
    void republishRequested();

private:
    TileCache();
    ~TileCache() override;

    struct Request
    {
        Kind kind = Kind::Asset;
        QString guid;
        Spec spec;
        QString key;
        int token = 0;   ///< the asking view's; 0 = always wanted
    };
    /// A view's published state (openView .. closeView).
    struct ViewSet
    {
        Kind kind = Kind::Asset;
        bool known = false;        ///< published and shown
        QStringList order;         ///< the published guids, top-left first
        QSet<QString> guids;       ///< `order`, plus the asks since
        Spec spec;                 ///< the size the view last asked at (renew queues at it)
    };
    struct Decoded
    {
        Request request;
        QByteArray png;
        QImage tile;
        bool sourced = false;   ///< the Source answered bytes for it
    };

    static QString keyOf(Kind kind, const QString &guid, const Spec &spec);
    void scheduleFlush();
    bool wanted(const Request &request) const;
    void flush();
    void land(const QVector<Decoded> &batch);

    QCache<QString, QPixmap> mEntries{ kBudgetKB };
    QSet<QString> mMissing;
    QHash<QString, size_t> mHashes;  // key -> the supplied bytes' hash
    QSet<QString> mQueued;          // keys queued or decoding
    QVector<Request> mQueue;
    QHash<int, Source> mSources;
    QHash<int, Demand> mDemands;
    QHash<int, ViewSet> mViews;   // token -> its view's state
    QThreadPool *mPool = nullptr;
    int mNextToken = 1;
    int mInFlight = 0;
    int mDecodes = 0;
    int mSourced = 0;
    int mDropped = 0;
    bool mFlushScheduled = false;
};

class QAbstractItemView;
class QListWidget;
class QListWidgetItem;

/// AN ITEM VIEW's TOKEN AND ITS VISIBLE RANGE (PREVIEWS-1 stage 2): a child of `view` (gone with it,
/// its token closed), it publishes the guids (`guidRole` of the model's column-0 rows under the root)
/// the view shows — on a scroll, a viewport resize, a model reset, a layout change, rows in or out —
/// ONE queued publish an event-loop turn, never from a paint. The range is a binary search on
/// `visualRect` over the root's rows, then the walk across the viewport: O(log n + visible), never a
/// walk of the model (the rows' rectangles advance with their row along the flow — a list, an icon
/// grid, a flat tree; a hidden row is not supported). A hidden view publishes NOTHING (it withdraws
/// its set): a page not shown raises no render demand, and its showing raises none either — the
/// first publish after it is its next scroll, resize or reset.
class TileViewWatcher : public QObject
{
    Q_OBJECT

public:
    TileViewWatcher(QAbstractItemView *view, TileCache::Kind kind, int guidRole);
    ~TileViewWatcher() override;

    int token() const { return mToken; }
    /// The guids the view shows now (what the next publish says).
    QStringList visibleGuids() const;

signals:
    /// After each publish, with what it said.
    void published(const QStringList &guids);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void schedule();
    void publish();

    QAbstractItemView *mView = nullptr;   // the parent: outlives this
    int mGuidRole = 0;
    int mToken = 0;
    bool mScheduled = false;
};

/// A QListWidget PANEL over the tile cache (the tray, the preset drawers, the
/// Materials module's drawers — panels of a project's or a drawer's rows, not of
/// the library): `assign` gives an item its cached tile, or `placeholder` while
/// the cache fetches and decodes it off the UI thread, and the icon follows when
/// the tile lands. Items are tracked by persistent index, so a cleared or
/// repopulated list simply stops receiving.
class ListTileBinder : public QObject
{
    Q_OBJECT

public:
    /// The role an assigned item carries its guid in (what the list's watcher publishes).
    static constexpr int kGuidRole = Qt::UserRole + 0x7417;

    ListTileBinder(QListWidget *list, const TileCache::Spec &spec, QObject *parent = nullptr);

    void assign(QListWidgetItem *item, const QString &guid, const QIcon &placeholder);
    /// The asset's thumbnail changed (a render landed): forget and re-request.
    void refresh(QListWidgetItem *item, const QString &guid, const QIcon &placeholder);

private:
    void onReady(TileCache::Kind kind, const QString &guid);
    /// A publish: the visible items still waiting ask again (a request dropped while they were
    /// out of view queues again now).
    void onPublished(const QStringList &guids);
    int token() const { return mWatcher ? mWatcher->token() : 0; }

    QListWidget *mList = nullptr;
    TileCache::Spec mSpec;
    QPointer<TileViewWatcher> mWatcher;   // the list's child
    QHash<QString, QList<QPersistentModelIndex>> mAssigned;   // every item given a tile, by guid
    QSet<QString> mWaiting;                                   // guids whose tile has not landed
};

/// The one shaping rule, public for the Desktop's parallel prefetch and the tests:
/// `full` scaled to fit `spec.size`, top corners rounded when asked.
QImage tileImageFor(const QImage &full, const TileCache::Spec &spec);

#endif // TILECACHE_H
