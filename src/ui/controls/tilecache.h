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
/// turn: the BYTES are read on the UI thread (the Database's thread) by ONE
/// `WHERE guid IN (...)` per batch through the kind's Source, the DECODE and the
/// scale to the tile run on the cache's own thread pool, and `tileReady` names
/// every guid whose picture landed. So a view over 10,000 assets reads and
/// decodes the ~50 tiles it shows, never the library.
///
/// ENTRIES ARE TILE-SIZED (the bound charges what is held: 128 MB is ~940 Desktop
/// tiles at Normal and ~7,000 Library tiles). A guid with no thumbnail, or bytes
/// that do not decode, is remembered as MISSING so a repaint does not re-queue it;
/// `invalidate` forgets both (a thumbnail was rebuilt, a project re-saved).
/// UI thread only (QPixmap, and the Sources read the UI thread's connection).
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

    /// The bytes of `guids` (absent = no thumbnail). Called on the UI thread with
    /// at most kBatch guids.
    using Source = std::function<QHash<QString, QByteArray>(const QStringList &guids)>;

    static constexpr int kBatch = 48;
    static constexpr int kBudgetKB = 128 * 1024;

    static TileCache &instance();

    void setSource(Kind kind, Source source);

    /// The tile, or a null pixmap while it is on its way (the request is queued).
    QPixmap tile(Kind kind, const QString &guid, const Spec &spec);
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
    /// Forget EVERYTHING and drop the queue (a library reset; a suite that
    /// measures a cold cache). Batches already decoding land into the empty cache.
    void clear();

    /// PNG decodes performed this session (suites prove a rebuild decodes nothing seen).
    int decodes() const { return mDecodes; }
    /// Rows read through the Sources this session (the no-list-BLOB proof's other half).
    int sourcedRows() const { return mSourced; }
    /// Requests queued or decoding.
    int pending() const { return int(mQueue.size()) + mInFlight; }
    /// Pumps the event loop until nothing is pending (tests, and a caller that
    /// must hand a finished picture to a script). False on timeout.
    bool drain(int timeoutMs = 10000);

signals:
    void tileReady(TileCache::Kind kind, const QString &guid);

private:
    TileCache();
    ~TileCache() override;

    struct Request
    {
        Kind kind = Kind::Asset;
        QString guid;
        Spec spec;
        QString key;
    };
    struct Decoded
    {
        Request request;
        QByteArray png;
        QImage tile;
    };

    static QString keyOf(Kind kind, const QString &guid, const Spec &spec);
    void scheduleFlush();
    void flush();
    void land(const QVector<Decoded> &batch);

    QCache<QString, QPixmap> mEntries{ kBudgetKB };
    QSet<QString> mMissing;
    QHash<QString, size_t> mHashes;  // key -> the supplied bytes' hash
    QSet<QString> mQueued;          // keys queued or decoding
    QVector<Request> mQueue;
    QHash<int, Source> mSources;
    QThreadPool *mPool = nullptr;
    int mInFlight = 0;
    int mDecodes = 0;
    int mSourced = 0;
    bool mFlushScheduled = false;
};

class QListWidget;
class QListWidgetItem;

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
    ListTileBinder(QListWidget *list, const TileCache::Spec &spec, QObject *parent = nullptr);

    void assign(QListWidgetItem *item, const QString &guid, const QIcon &placeholder);
    /// The asset's thumbnail changed (a render landed): forget and re-request.
    void refresh(QListWidgetItem *item, const QString &guid, const QIcon &placeholder);

private:
    void onReady(TileCache::Kind kind, const QString &guid);

    QListWidget *mList = nullptr;
    TileCache::Spec mSpec;
    QHash<QString, QList<QPersistentModelIndex>> mWaiting;
};

/// The one shaping rule, public for the Desktop's parallel prefetch and the tests:
/// `full` scaled to fit `spec.size`, top corners rounded when asked.
QImage tileImageFor(const QImage &full, const TileCache::Spec &spec);

#endif // TILECACHE_H
