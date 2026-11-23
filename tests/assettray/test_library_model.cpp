/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// library.model — THE LIBRARY AS A MODEL OVER THE TILE CACHE (D11-LIBRARY-SCALE
// §3.1/§3.2/§3.4/§3.5), on the REAL Database over a throwaway SQLite file holding a
// 3,000-asset library with real PNG thumbnails:
//
//   * THE ITEM-MODEL CONTRACT: QAbstractItemModelTester (Fatal) watches the
//     LibraryModel and a LibraryFilterProxy over it through a reset, an append at
//     the front, an in-place update, removals, a drawer reassignment and every
//     proxy filter; the guid lookup is O(1) and stays right after removals.
//   * NO LISTING CARRIES A THUMBNAIL (the query log, Database::setQueryLog): every
//     listing a panel reads — the Library grid's, the tray's three, the favourites,
//     the Materials drawer's, the Desktop's projects — runs with the log on, and
//     every statement that selects a `thumbnail` column is keyed by guid.
//   * THE TILES COME OFF THE UI THREAD, BY GUID: a view's DecorationRole read over
//     60 rows answers the placeholder and queues them; the cache reads exactly
//     those 60 by guid (in batches of TileCache::kBatch) and decodes them on its
//     pool — nothing else of the library is read or decoded; a second read is a
//     hit; a thumbnail write drops the cached tile.
//   * THE QUEUE (PREVIEWS-1 stage 2): the newest batch is read first; a view STATES its visible
//     rows by its token (TileViewWatcher) and a row that left it is never decoded (counted); a row
//     painted once and still visible is decoded with no second paint; a tokenless ask is always
//     wanted and a destroyed view's asks are dropped; a HIDDEN view re-tells the render demand (0
//     further requests for its rows) and a view SHOWN AGAIN queues exactly one publish, touching no row.
//   * THE INDEXES the listings' predicates need exist, and SQLite's planner uses
//     them (EXPLAIN QUERY PLAN).
// Headless (offscreen); never touches the user's JahLibrary.db.

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QBuffer>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QListView>
#include <QAtomicInt>
#include <QMutex>
#include <QThread>
#include <QPixmap>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <algorithm>
#include <QUuid>
#include <cstdio>

#include "data/database/database.h"
#include "data/project.h"
#include "services/assetcas.h"
#include "services/assettray.h"
#include "services/previewdemand.h"
#include "ui/controls/librarymodel.h"
#include "ui/controls/tilecache.h"

static int failures = 0;
#define CHECK(cond, ...)                                                                   \
    do {                                                                                   \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); }  \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
        std::fflush(stdout);                                                               \
    } while (0)

static QString newGuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

static QByteArray png(int i)
{
    QImage img(64, 64, QImage::Format_RGB888);
    img.fill(QColor((i * 37) % 256, (i * 91) % 256, (i * 13) % 256));
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

static bool planUses(const QString &sql, const QString &index)
{
    QSqlQuery q;
    if (!q.exec(QStringLiteral("EXPLAIN QUERY PLAN ") + sql)) return false;
    QString plan;
    while (q.next()) plan += q.value(3).toString() + QLatin1Char(' ');
    std::printf("   plan [%s]: %s\n", qPrintable(sql.left(60)), qPrintable(plan));
    return plan.contains(index);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const QString dbPath = QStringLiteral("library_model_test.db");
    QFile::remove(dbPath);
    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");
    db.createAllTables();

    // ---- THE LIBRARY: 3,000 textures with thumbnails in four drawers, one
    // project with 20 rows of its own and 10 pins, 5 favourites. -------------
    const int kAssets = 3000;
    QStringList guids;
    {
        DbBatch batch(&db);
        const int drawer = db.createCollection(QStringLiteral("Drawer A"));
        for (int i = 0; i < kAssets; ++i) {
            const QString g = newGuid();
            db.createAssetEntry(g, QStringLiteral("asset_%1.png").arg(i, 4, 10, QLatin1Char('0')),
                                static_cast<int>(ModelTypes::Texture), QString(), assethome::assets(), assethome::Origin::Import, QString(),
                                QString(), png(i), QByteArray(), QByteArray(), QByteArray());
            if (i % 4 == 0) db.switchAssetCollection(drawer, g);
            guids << g;
        }
    }
    const QString projectGuid = newGuid();
    db.createProject(projectGuid, QStringLiteral("Library Model"));
    for (int i = 0; i < 20; ++i)
        db.createAssetEntry(newGuid(), QStringLiteral("own_%1.png").arg(i), static_cast<int>(ModelTypes::Texture),
                            QString(), assethome::project(projectGuid), assethome::Origin::Create, QString(), QString(), png(i), QByteArray(), QByteArray(),
                            QByteArray());
    for (int i = 0; i < 10; ++i) AssetCas::writePin(db.connection(), projectGuid, guids.at(i), QString());
    for (int i = 0; i < 5; ++i) db.addFavorite(guids.at(i));

    // ---- THE INDEXES (§3.5) -------------------------------------------------
    {
        QSqlQuery q;
        q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'index'"));
        QStringList names;
        while (q.next()) names << q.value(0).toString();
        for (const char *want : { "idx_assets_parent", "idx_assets_project_guid", "idx_assets_view_filter_listed",
                                  "idx_assets_collection_listed", "idx_projects_tiles" })
            CHECK(names.contains(QString::fromLatin1(want)), "the schema carries %s", want);
        CHECK(planUses(QStringLiteral("SELECT guid FROM assets WHERE project_guid = 'x'"), QStringLiteral("idx_assets_project_guid")),
              "a project_guid predicate is planned on its index");
        CHECK(planUses(QStringLiteral("SELECT guid FROM assets WHERE parent = 'x' AND project_guid = 'y'"),
                       QStringLiteral("idx_assets_")),
              "the tray's parent/project predicate is planned on an index");
        CHECK(planUses(QStringLiteral("SELECT guid FROM assets WHERE collection = 3"), QStringLiteral("idx_assets_collection_listed")),
              "a drawer predicate is planned on its index");
        CHECK(planUses(QStringLiteral("SELECT guid FROM projects WHERE desktop = 2"), QStringLiteral("idx_projects_tiles")),
              "a desktop predicate is planned on its index");
    }

    // ---- NO LISTING CARRIES A THUMBNAIL (§3.1) -------------------------------
    Database::setQueryLog(true);
    const QVector<AssetRecord> listing = assettray::libraryList(&db, false);
    CHECK(listing.size() == kAssets, "the Library listing holds the %d library rows (%d)", kAssets, int(listing.size()));
    bool anyBytes = false;
    for (const AssetRecord &r : listing) anyBytes |= !r.thumbnail.isEmpty();
    CHECK(!anyBytes, "no row of the Library listing carries thumbnail bytes");
    db.fetchChildAssets(QString(), projectGuid, -1);
    db.fetchChildAssetsIn({ QString() }, projectGuid, -1);
    db.fetchProjectPinnedAssets(projectGuid);
    db.fetchAssetsByType(static_cast<int>(ModelTypes::Texture), projectGuid);
    db.fetchFavorites();
    db.fetchAssetsInHome(assethome::Kind::MaterialsLibrary, static_cast<int>(ModelTypes::Material));
    db.fetchProjects(0);
    db.fetchProjects(1);
    assettray::list(&db, projectGuid, QString(), -1, false);
    ProjectTileData tile;
    CHECK(db.fetchProjectTile(projectGuid, &tile) && tile.thumbnail.isEmpty(),
          "one project resolves by its guid, with no thumbnail");
    CHECK(db.fetchProjectsNamed(QStringLiteral("Library Model")).size() == 1, "...and by its name");
    int listingStatements = 0;
    for (const Database::QueryLogEntry &e : Database::queryLogEntries()) {
        ++listingStatements;
        if (!e.selectsThumbnail) continue;
        CHECK(e.byGuid, "a thumbnail select is keyed by guid: %s — %s", qPrintable(e.name), qPrintable(e.sql.left(120)));
    }
    bool listedSelects = false;
    for (const Database::QueryLogEntry &e : Database::queryLogEntries())
        listedSelects |= e.selectsThumbnail;
    CHECK(!listedSelects, "NONE of the %d listing statements selects a thumbnail column", listingStatements);
    // The classifier itself, on the shapes it must tell apart.
    {
        bool thumb = false, keyed = false;
        Database::classifyQuery(QStringLiteral("SELECT name, thumbnail FROM assets WHERE view_filter = ?"), &thumb, &keyed);
        CHECK(thumb && !keyed, "the classifier flags a listing that selects the thumbnail");
        Database::classifyQuery(QStringLiteral("SELECT guid, thumbnail FROM assets WHERE guid IN (?,?)"), &thumb, &keyed);
        CHECK(thumb && keyed, "...and passes a by-guid batch read");
        Database::classifyQuery(QStringLiteral("SELECT thumbnail FROM assets WHERE project_guid = ?"), &thumb, &keyed);
        CHECK(thumb && !keyed, "...and a project_guid predicate is not a guid key");
    }

    // ---- THE MODEL CONTRACT (§3.2) -------------------------------------------
    LibraryModel model;
    auto *modelTester = new QAbstractItemModelTester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal, &model);
    LibraryFilterProxy proxy;
    proxy.setSourceModel(&model);
    auto *proxyTester = new QAbstractItemModelTester(&proxy, QAbstractItemModelTester::FailureReportingMode::Fatal, &proxy);
    QVector<LibraryRow> rows;
    for (const AssetRecord &r : listing) {
        LibraryRow row;
        row.guid = r.guid;
        row.name = r.name;
        row.type = r.type;
        row.collection = r.collection;
        rows.append(row);
    }
    model.setRows(rows);
    CHECK(model.rowCount() == kAssets && proxy.rowCount() == kAssets, "the model and its proxy list every row");
    CHECK(proxy.index(0, 0).data(LibraryModel::GuidRole).toString() == rows.first().guid,
          "the proxy keeps the listing's order");
    bool lookupRight = true;
    for (int i = 0; i < rows.size(); ++i) lookupRight &= model.rowOfGuid(rows.at(i).guid) == i;
    CHECK(lookupRight, "every guid resolves to its row in O(1)");

    LibraryRow added;
    added.guid = newGuid();
    added.name = QStringLiteral("zz_new_import.png");
    added.type = static_cast<int>(ModelTypes::Texture);
    model.upsert(added);
    CHECK(model.rowCount() == kAssets + 1, "an import appends one row");
    CHECK(proxy.index(0, 0).data(LibraryModel::GuidRole).toString() == added.guid,
          "...and the proxy shows it FIRST (a later add sorts to the front)");
    model.update(added.guid, [](LibraryRow &r) { r.name = QStringLiteral("renamed.png"); });
    CHECK(model.indexOfGuid(added.guid).data().toString() == QStringLiteral("renamed"),
          "an in-place update changes the row and its shown base name");
    for (int i = 0; i < 10; ++i) CHECK(model.remove(rows.at(i * 7).guid), "a row removes (%d)", i);
    bool afterRemoval = true;
    for (int r = 0; r < model.rowCount(); ++r)
        afterRemoval &= model.rowOfGuid(model.index(r, 0).data(LibraryModel::GuidRole).toString()) == r;
    CHECK(afterRemoval, "the guid index is exact after removals");
    CHECK(!model.contains(rows.at(0).guid) && model.rowFor(rows.at(0).guid) == nullptr,
          "a removed guid is gone from the index");

    // The drawer the fixture filed every fourth asset in (the listing is name
    // DESC, so read it off a row rather than an index).
    int drawer = 0;
    for (const LibraryRow &r : model.rows()) drawer = qMax(drawer, r.collection);
    const int drawerRows = int(std::count_if(model.rows().cbegin(), model.rows().cend(),
                                             [&](const LibraryRow &r) { return r.collection == drawer; }));
    proxy.setCollection(drawer);
    CHECK(proxy.rowCount() == drawerRows && drawerRows > 0, "the drawer filter shows the drawer's %d rows", drawerRows);
    proxy.setSearch(QStringLiteral("asset_000"));
    const int both = proxy.rowCount();
    CHECK(both > 0 && both < drawerRows, "the search AND the drawer filter apply together (%d)", both);
    proxy.setCollection(-1);
    proxy.setSearch(QString());
    proxy.setTypes({ static_cast<int>(ModelTypes::Material) });
    CHECK(proxy.rowCount() == 0, "a type filter with no such rows is empty");
    proxy.setTypes({});
    proxy.setCollection(drawer);
    model.reassignCollections({ drawer }, 0);
    CHECK(drawer > 0 && proxy.rowCount() == 0, "a drawer delete moves its rows out of the showing drawer, live");
    proxy.setCollection(-1);
    // The tile measurement below counts what a VIEW asks for: the testers read
    // every role of the indexes they check (DecorationRole included), so they
    // stand down first — the contract above is theirs, the counts are the view's.
    delete modelTester;
    delete proxyTester;

    // ---- THE TILES, BY GUID, OFF THE UI THREAD (§3.1) ------------------------
    // The Source runs on the cache's POOL (TC2-LIBRARY-CREATE-READ): it reads through its own
    // connection to the file, exactly as the shell installs it.
    const QString libraryFile = db.filePath();
    TileCache::instance().setSource(TileCache::Kind::Asset, [libraryFile](const QStringList &g) {
        return Database::readThumbnailBytes(libraryFile, Database::ThumbnailTable::Assets, g);
    });
    Database::setAssetThumbnailWritten([](const QString &guid) {
        TileCache::instance().invalidate(TileCache::Kind::Asset, guid);
    });
    model.remove(added.guid);        // the fixture's row with no catalog row (it would read as missing)
    TileCache::instance().clear();   // cold: what the testers' reads queued is dropped
    Database::setQueryLog(true);
    const int decodes0 = TileCache::instance().decodes();
    const int sourced0 = TileCache::instance().sourcedRows();
    int placeholders = 0;
    QElapsedTimer ui;
    ui.start();
    for (int r = 0; r < 60; ++r)
        placeholders += proxy.index(r, 0).data(Qt::DecorationRole).value<QPixmap>().isNull() ? 1 : 0;
    const qint64 paintMs = ui.elapsed();
    CHECK(placeholders == 60, "a cold view's 60 tiles answer the placeholder (no read, no decode inside the paint)");
    CHECK(TileCache::instance().decodes() == decodes0, "...and nothing was decoded on the UI thread (%lld ms)",
          static_cast<long long>(paintMs));
    int ready = 0;
    QObject::connect(&model, &QAbstractItemModel::dataChanged, &model,
                     [&ready](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
        if (roles.contains(Qt::DecorationRole)) ++ready;
    });
    CHECK(TileCache::instance().drain(20000), "the cache drains");
    CHECK(TileCache::instance().decodes() - decodes0 == 60, "exactly the 60 painted tiles were decoded (%d)",
          TileCache::instance().decodes() - decodes0);
    CHECK(TileCache::instance().sourcedRows() - sourced0 == 60, "...read by guid: 60 rows of 3,000 (%d)",
          TileCache::instance().sourcedRows() - sourced0);
    CHECK(ready == 60, "each landed tile repainted its row (dataChanged on DecorationRole: %d)", ready);
    int batchReads = 0;
    for (const Database::QueryLogEntry &e : Database::queryLogEntries())
        if (e.name == QStringLiteral("fetchAssetThumbnailBytes")) batchReads += e.count;
    CHECK(batchReads == (60 + TileCache::kBatch - 1) / TileCache::kBatch,
          "...in %d batched statements (kBatch %d)", batchReads, TileCache::kBatch);
    int hits = 0;
    for (int r = 0; r < 60; ++r)
        hits += proxy.index(r, 0).data(Qt::DecorationRole).value<QPixmap>().isNull() ? 0 : 1;
    CHECK(hits == 60 && TileCache::instance().decodes() - decodes0 == 60, "a second paint is 60 hits, no decode");

    // ---- THE PICTURE: the page's view over the model, painted offscreen ------
    // A QListView in icon mode with the tile delegate, as the Assets page builds
    // it: the first tile's picture centre is its asset's own colour (the fixture's
    // PNGs are flat colours), painted from the cache — pixel evidence the tiles are
    // the thumbnails, not placeholders. The grab is kept beside the suite.
    {
        QListView view;
        LibraryTileDelegate delegate;
        view.setModel(&proxy);
        view.setItemDelegate(&delegate);
        view.setViewMode(QListView::IconMode);
        view.setUniformItemSizes(true);
        view.setSpacing(5);
        view.resize(700, 480);
        view.show();
        QCoreApplication::processEvents();
        TileCache::instance().drain(20000);
        const QImage shot = view.viewport()->grab().toImage();
        shot.save(QStringLiteral("library_model_tiles.png"));
        const QModelIndex first = proxy.index(0, 0);
        const QRect r = view.visualRect(first);
        const QPoint centre(r.center().x(), r.top() + LibraryTileDelegate::kPictureHeight / 2);
        const QString name = first.data(LibraryModel::NameRole).toString();
        const int i = name.mid(6, 4).toInt();   // asset_NNNN.png
        const QColor want((i * 37) % 256, (i * 91) % 256, (i * 13) % 256);
        const QColor got = shot.pixelColor(centre * shot.devicePixelRatio());
        CHECK(qAbs(got.red() - want.red()) <= 3 && qAbs(got.green() - want.green()) <= 3
                  && qAbs(got.blue() - want.blue()) <= 3,
              "the first painted tile shows its own thumbnail (%s: got %d,%d,%d want %d,%d,%d)", qPrintable(name),
              got.red(), got.green(), got.blue(), want.red(), want.green(), want.blue());
    }

    // ---- THE QUEUE: WHAT A VIEW STILL SHOWS, NEWEST FIRST (PREVIEWS-1 stage 2) ----
    // The asks below are the test's own (a tile view's paints): the views over `queueModel` paint no
    // picture (setDecorated(false), the default delegate), so every ask is counted here, never inferred.
    // The posted calls are delivered ONE AT A TIME where the order is the law (sendPostedEvents to one
    // receiver delivers what was posted before the call, never what it posts itself).
    {
        TileCache &cache = TileCache::instance();
        const TileCache::Spec spec{ QSize(96, 96), 0 };
        const auto ask = [&](const QStringList &guids, int token) {
            for (const QString &g : guids) cache.tile(TileCache::Kind::Asset, g, spec, token);
        };
        const auto rowGuids = [&](int from, int count) {
            QStringList out;
            for (int r = from; r < from + count; ++r) out << model.rows().at(r).guid;
            return out;
        };

        // (1) A..Z: THE NEWEST BATCH DECODES FIRST. Two batches asked in one turn, A then B (tokenless):
        // the first flush takes B, the last asked - the source is handed B's guids before A's exist
        // in any batch.
        cache.clear();
        QMutex seenLock;
        QVector<QStringList> seen;
        QAtomicInt calls = 0;
        cache.setSource(TileCache::Kind::Asset, [libraryFile, &seenLock, &seen, &calls](const QStringList &g) {
            { QMutexLocker lock(&seenLock); seen.append(g); }
            calls.fetchAndAddOrdered(1);
            return Database::readThumbnailBytes(libraryFile, Database::ThumbnailTable::Assets, g);
        });
        const QStringList batchA = rowGuids(200, TileCache::kBatch), batchB = rowGuids(300, TileCache::kBatch);
        ask(batchA, 0);
        ask(batchB, 0);
        QCoreApplication::sendPostedEvents(&cache, QEvent::MetaCall);   // ONE flush
        for (int spins = 0; calls.loadAcquire() < 1 && spins < 20000; ++spins) QThread::msleep(1);
        {
            QMutexLocker lock(&seenLock);
            QStringList first = seen.value(0), wantB = batchB;
            std::sort(first.begin(), first.end());
            std::sort(wantB.begin(), wantB.end());
            CHECK(seen.size() == 1 && first == wantB,
                  "A..Z: the first batch read is the NEWEST asked (B: %lld guids, %s), never the FIFO's A",
                  static_cast<long long>(first.size()), first == wantB ? "B" : "not B");
        }
        CHECK(cache.drain(20000), "...and both batches land");
        {
            // (The FIFO this replaces read A first: this row's red on the base, kept with the stage's evidence.)
            QMutexLocker lock(&seenLock);
            QStringList second = seen.value(1), wantA = batchA;
            std::sort(second.begin(), second.end());
            std::sort(wantA.begin(), wantA.end());
            CHECK(seen.size() == 2 && second == wantA, "...the older batch A second");
        }
        // UNREAD IS NEVER MISSING (NOQT-DATABASE-1 stage 3, step 1): a batch whose read FAILED leaves its rows
        // neither tiled nor marked missing (a missing mark is final); A PAINT NEVER ASKS IT AGAIN (the reader's point
        // 6b: three more paints, still ONE Source call) - the next PUBLISH of a view showing them does, and the rows
        // read for real land as tiles.
        {
            const QStringList unreadRows = rowGuids(500, 3);
            cache.clear();
            QAtomicInt failedCalls = 0;
            cache.setSource(TileCache::Kind::Asset, [&failedCalls](const QStringList &) {
                failedCalls.fetchAndAddOrdered(1);
                return std::optional<QHash<QString, QByteArray>>();
            });
            ask(unreadRows, 0);
            CHECK(cache.drain(20000), "unread: the failed batch drains");
            for (int paint = 0; paint < 3; ++paint) {
                ask(unreadRows, 0);
                CHECK(cache.drain(20000), "unread: paint %d drains", paint + 1);
            }
            CHECK(failedCalls.loadAcquire() == 1, "unread: three more paints over unread rows read NOTHING again: %d Source "
                                                  "call(s) in all, want 1", failedCalls.loadAcquire());
            int missing = 0, held = 0;
            for (const QString &g : unreadRows) {
                missing += cache.isMissing(TileCache::Kind::Asset, g, spec) ? 1 : 0;
                held += cache.peek(TileCache::Kind::Asset, g, spec).isNull() ? 0 : 1;
            }
            CHECK(missing == 0 && held == 0, "unread: a FAILED read marks no row missing and holds no tile (missing %d, "
                                             "held %d of %lld)", missing, held, static_cast<long long>(unreadRows.size()));
            cache.setSource(TileCache::Kind::Asset, [libraryFile](const QStringList &g) {
                return Database::readThumbnailBytes(libraryFile, Database::ThumbnailTable::Assets, g);
            });
            const int view = cache.openView(TileCache::Kind::Asset);
            cache.publishVisible(view, unreadRows);
            CHECK(cache.drain(20000), "unread: the publish's asks drain");
            held = 0;
            for (const QString &g : unreadRows) held += cache.peek(TileCache::Kind::Asset, g, spec).isNull() ? 0 : 1;
            CHECK(held == int(unreadRows.size()), "unread: CONTROL: a publish asks them again, and the rows read for real "
                                                  "land as tiles (%d)", held);
            cache.closeView(view);
        }
        // THE LIBRARY READS AGAIN (the reader's point b): a shown view's unread rows are asked again when a LATER batch
        // reads for real - no publish from the test after the failure.
        {
            const QStringList shownRows = rowGuids(520, 3), goodRows = rowGuids(530, 2);
            cache.clear();
            const int view = cache.openView(TileCache::Kind::Asset);
            cache.publishVisible(view, shownRows);
            cache.setSource(TileCache::Kind::Asset, [](const QStringList &) {
                return std::optional<QHash<QString, QByteArray>>();
            });
            ask(shownRows, view);
            CHECK(cache.drain(20000), "unread again: the failed batch drains");
            cache.setSource(TileCache::Kind::Asset, [libraryFile](const QStringList &g) {
                return Database::readThumbnailBytes(libraryFile, Database::ThumbnailTable::Assets, g);
            });
            ask(goodRows, 0);
            CHECK(cache.drain(20000), "unread again: a batch that reads, and what it asks again, drain");
            int held = 0;
            for (const QString &g : shownRows) held += cache.peek(TileCache::Kind::Asset, g, spec).isNull() ? 0 : 1;
            CHECK(held == int(shownRows.size()), "unread again: a batch that read asks the shown view's unread rows again, "
                                                 "and they land as tiles with no publish (%d of %lld)", held,
                  static_cast<long long>(shownRows.size()));
            cache.closeView(view);
        }

        // A VIEW and its watcher: an icon grid over 2,000 rows, offscreen.
        LibraryModel queueModel;
        queueModel.setDecorated(false);
        queueModel.setRows(QVector<LibraryRow>(model.rows().cbegin(), model.rows().cbegin() + 2000));
        auto *view = new QListView;
        view->setModel(&queueModel);
        view->setViewMode(QListView::IconMode);
        view->setUniformItemSizes(true);
        view->setGridSize(QSize(100, 100));
        view->resize(520, 330);
        auto *watcher = new TileViewWatcher(view, TileCache::Kind::Asset, LibraryModel::GuidRole);
        int publishes = 0;
        QStringList lastPublished;
        QObject::connect(watcher, &TileViewWatcher::published, watcher, [&](const QStringList &g) {
            ++publishes;
            lastPublished = g;
        });
        view->show();
        for (int i = 0; i < 50 && publishes == 0; ++i) QCoreApplication::processEvents();
        const QStringList top = lastPublished;
        CHECK(publishes > 0 && top.size() > 0 && top.size() < 60 && top.first() == queueModel.rows().first().guid,
              "the watcher publishes the view's visible range only: %lld guids of 2,000, the first row first",
              static_cast<long long>(top.size()));

        // (2) A ROW THAT LEFT THE VIEW IS NEVER DECODED. The top screen is asked (painted) once; the view
        // scrolls to the bottom and publishes BEFORE the flush runs: every top request is dropped.
        cache.clear();
        int decodes = cache.decodes(), dropped = cache.dropped();
        ask(top, watcher->token());
        view->scrollToBottom();
        QCoreApplication::sendPostedEvents(watcher, QEvent::MetaCall);   // the scroll's publish first
        const QStringList bottom = lastPublished;
        CHECK(!bottom.isEmpty() && !bottom.contains(top.first()), "the scroll published the bottom rows (%lld)",
              static_cast<long long>(bottom.size()));
        ask(bottom, watcher->token());
        CHECK(cache.drain(20000), "the cache drains");
        int topDecoded = 0;
        for (const QString &g : top) topDecoded += cache.peek(TileCache::Kind::Asset, g, spec).isNull() ? 0 : 1;
        CHECK(topDecoded == 0 && cache.dropped() - dropped == top.size(),
              "a row that left the view is NEVER decoded: %d of the %lld top rows decoded, %d dropped (counted)",
              topDecoded, static_cast<long long>(top.size()), cache.dropped() - dropped);
        CHECK(cache.decodes() - decodes == bottom.size(), "...only the %lld visible rows were (%d decodes)",
              static_cast<long long>(bottom.size()), cache.decodes() - decodes);

        // (3) THE NEGATIVE CONTROL OF THE REFUSED DESIGN ("re-asked each paint"): a row painted ONCE and
        // still visible is decoded with NO second paint - a publish (a resize) lands before the flush and
        // the view's stated range still holds it.
        cache.clear();
        decodes = cache.decodes();
        dropped = cache.dropped();
        ask(bottom, watcher->token());
        view->resize(521, 330);
        QCoreApplication::sendPostedEvents(watcher, QEvent::MetaCall);
        CHECK(publishes > 0 && lastPublished.contains(bottom.first()), "the resize published, the rows still shown");
        CHECK(cache.drain(20000), "the cache drains");
        int stillDecoded = 0;
        for (const QString &g : bottom) stillDecoded += cache.peek(TileCache::Kind::Asset, g, spec).isNull() ? 0 : 1;
        CHECK(stillDecoded == bottom.size() && cache.dropped() == dropped,
              "a row painted ONCE and still visible IS decoded with no second paint (%d of %lld; %d dropped)",
              stillDecoded, static_cast<long long>(bottom.size()), cache.dropped() - dropped);

        // A NEW PICTURE STORED (the shell's thumbnail-written hook calls renew): a shown row is decoded
        // again at the view's size with no ask; a row no view shows is only forgotten.
        decodes = cache.decodes();
        cache.renew(TileCache::Kind::Asset, bottom.first());
        cache.renew(TileCache::Kind::Asset, top.first());
        CHECK(cache.pending() == 1, "renew queues the shown row only (%d pending)", cache.pending());
        CHECK(cache.drain(20000) && cache.decodes() - decodes == 1
                  && !cache.peek(TileCache::Kind::Asset, bottom.first(), spec).isNull(),
              "...and it is decoded again (%d decode)", cache.decodes() - decodes);

        // A SCRIPT RUN's END: every live view publishes ONCE (TileCache::republishAll); a hidden one, none.
        {
            auto *hidden = new QListView;
            hidden->setModel(&queueModel);
            auto *hiddenWatcher = new TileViewWatcher(hidden, TileCache::Kind::Asset, LibraryModel::GuidRole);
            int hiddenPublishes = 0;
            QObject::connect(hiddenWatcher, &TileViewWatcher::published, hiddenWatcher, [&] { ++hiddenPublishes; });
            for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
            const int shownBefore = publishes;
            cache.republishAll();
            for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
            CHECK(publishes - shownBefore == 1 && hiddenPublishes == 0,
                  "republishAll: the shown view published once (%d), the hidden one none (%d)",
                  publishes - shownBefore, hiddenPublishes);
            delete hidden;
        }

        // A HIDDEN VIEW TELLS THE DEMAND (the final read's C1): Hide re-tells the kind's demand what the shown
        // views still want (here none: an empty list), so a page switched away from stops rendering its rows.
        // A real PreviewDemand over fakes (every row missing, every request completing at once); its settle
        // is driven by runNow, never by its clock.
        {
            int requests = 0;
            PreviewDemand demand(
                [](const QStringList &guids) {
                    QHash<QString, QString> need;
                    for (const QString &g : guids) need.insert(g, QStringLiteral("missing|k1"));
                    return need;
                },
                [&](const QString &, std::function<void(bool)> done) { ++requests; done(false); }, 8);
            QVector<QStringList> told;
            cache.setDemand(TileCache::Kind::Asset, [&](const QStringList &wanted) {
                told.append(wanted);
                demand.want(wanted);
            });
            const auto turnsOf = [&] {
                for (int i = 0; i < 200; ++i) {
                    const int before = requests;
                    QCoreApplication::processEvents();
                    if (requests == before && i > 2) break;
                }
            };
            const int publishedBefore = publishes;
            cache.republishAll();
            turnsOf();
            CHECK(publishes - publishedBefore == 1 && !told.isEmpty() && told.last() == lastPublished
                      && lastPublished.size() > 1,
                  "the shown view's publish tells the demand its %lld rows", static_cast<long long>(lastPublished.size()));
            demand.runNow();   // its settle: the first row is requested, the rest wait a turn each
            const int toldBefore = int(told.size());
            view->hide();
            CHECK(told.size() == toldBefore + 1 && told.last().isEmpty(),
                  "HIDE re-tells the demand what the shown views still want: %lld call(s), %lld guids",
                  static_cast<long long>(told.size() - toldBefore),
                  static_cast<long long>(told.isEmpty() ? -1 : told.last().size()));
            turnsOf();
            demand.runNow();   // the hidden page's settle
            turnsOf();
            CHECK(requests == 1, "A VIEW HIDDEN AFTER ITS PUBLISH: 0 further requests for its rows (%d of %lld requested)",
                  requests, static_cast<long long>(lastPublished.size()));

            // A SHOWN-AGAIN PAGE (the lead's ruling): Show QUEUES exactly one publish - one turn later, never
            // inside the show handler - which tells the demand what is visible, so a page shown again fills
            // its missing tiles with no scroll; the watcher mounts, rebuilds and deletes NO row (the model
            // signals nothing).
            int rowSignals = 0;
            const auto touched = [&] { ++rowSignals; };
            QObject probe;
            QObject::connect(&queueModel, &QAbstractItemModel::rowsAboutToBeInserted, &probe, touched);
            QObject::connect(&queueModel, &QAbstractItemModel::rowsAboutToBeRemoved, &probe, touched);
            QObject::connect(&queueModel, &QAbstractItemModel::modelAboutToBeReset, &probe, touched);
            QObject::connect(&queueModel, &QAbstractItemModel::layoutAboutToBeChanged, &probe, touched);
            QObject::connect(&queueModel, &QAbstractItemModel::dataChanged, &probe, touched);
            const int beforeShow = publishes;
            const int toldBeforeShow = int(told.size());
            view->show();
            const int inShow = publishes - beforeShow;
            for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
            CHECK(inShow == 0 && publishes - beforeShow == 1,
                  "SHOWN AGAIN: exactly one publish (%d), queued (%d inside the show)", publishes - beforeShow, inShow);
            CHECK(told.size() == toldBeforeShow + 1 && told.last() == lastPublished && !lastPublished.isEmpty(),
                  "...it tells the demand the %lld visible rows", static_cast<long long>(lastPublished.size()));
            CHECK(rowSignals == 0, "...and no row is mounted, rebuilt or deleted (%d model signals)", rowSignals);
            cache.setDemand(TileCache::Kind::Asset, {});
        }

        // A tokenless ask is always wanted; a DESTROYED view's token is gone, its queued asks dropped.
        cache.clear();
        dropped = cache.dropped();
        const QStringList loose = rowGuids(1000, 3);
        ask(top, watcher->token());
        ask(loose, 0);
        delete view;   // the watcher with it
        CHECK(cache.drain(20000), "the cache drains after the view is gone");
        int looseDecoded = 0, goneDecoded = 0;
        for (const QString &g : loose) looseDecoded += cache.peek(TileCache::Kind::Asset, g, spec).isNull() ? 0 : 1;
        for (const QString &g : top) goneDecoded += cache.peek(TileCache::Kind::Asset, g, spec).isNull() ? 0 : 1;
        CHECK(looseDecoded == 3 && goneDecoded == 0 && cache.dropped() - dropped == top.size(),
              "a tokenless ask is decoded (%d of 3); a destroyed view's are dropped (%d decoded, %d dropped)",
              looseDecoded, goneDecoded, cache.dropped() - dropped);
    }

    const QString rewritten = proxy.index(0, 0).data(LibraryModel::GuidRole).toString();
    db.updateAssetThumbnail(rewritten, png(4242));
    CHECK(TileCache::instance().peek(TileCache::Kind::Asset, rewritten, { model.tileSize(), 0 }).isNull(),
          "a thumbnail write drops the cached tile");
    Database::setQueryLog(false);

    TileCache::instance().setSource(TileCache::Kind::Asset, nullptr);
    Database::setAssetThumbnailWritten(nullptr);
    db.closeDatabase();
    QFile::remove(dbPath);
    std::printf(failures ? "library.model: %d FAILURES\n" : "library.model: all checks passed\n", failures);
    return failures ? 1 : 0;
}
