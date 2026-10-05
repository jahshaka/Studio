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
    for (int i = 0; i < 10; ++i) AssetCas::writePin(QSqlDatabase::database(), projectGuid, guids.at(i), QString());
    for (int i = 0; i < 5; ++i) db.addFavorite(guids.at(i));

    // ---- THE INDEXES (§3.5) -------------------------------------------------
    {
        QSqlQuery q;
        q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'index'"));
        QStringList names;
        while (q.next()) names << q.value(0).toString();
        for (const char *want : { "idx_assets_parent", "idx_assets_project_guid", "idx_assets_view_filter_listed",
                                  "idx_assets_collection", "idx_projects_desktop" })
            CHECK(names.contains(QString::fromLatin1(want)), "the schema carries %s", want);
        CHECK(planUses(QStringLiteral("SELECT guid FROM assets WHERE project_guid = 'x'"), QStringLiteral("idx_assets_project_guid")),
              "a project_guid predicate is planned on its index");
        CHECK(planUses(QStringLiteral("SELECT guid FROM assets WHERE parent = 'x' AND project_guid = 'y'"),
                       QStringLiteral("idx_assets_")),
              "the tray's parent/project predicate is planned on an index");
        CHECK(planUses(QStringLiteral("SELECT guid FROM assets WHERE collection = 3"), QStringLiteral("idx_assets_collection")),
              "a drawer predicate is planned on its index");
        CHECK(planUses(QStringLiteral("SELECT guid FROM projects WHERE desktop = 2"), QStringLiteral("idx_projects_desktop")),
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
    TileCache::instance().setSource(TileCache::Kind::Asset,
                                    [&db](const QStringList &g) { return db.fetchAssetThumbnailBytes(g); });
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
