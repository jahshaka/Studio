/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// library.statements — THE LIBRARY'S STATEMENT COUNT DOES NOT GROW WITH THE LIBRARY (LARGE-LIBRARY-1
// stage 1), on the REAL Database over a throwaway SQLite file, counted by SQLite's own statement trace
// (data/database/databasereads.h: every statement the default connection runs, raw SQL included).
//
//   * THE TRACE ITSELF, and its negative controls: a `fetchAsset` loop of N grows the traced count and
//     the query log's by N; a raw QSqlQuery loop of N grows only the traced count; a trigger's
//     sub-statement and the read's own PRAGMA are not counted; with JAH_DB_STATEMENT_TRACE unset (a
//     child run of this binary) `traced` is false and `statements` 0 — nothing is registered.
//   * M1, THE FIRST MEASUREMENT (printed, never barred): the pages the Library listing reads on a cold
//     connection at 1,000 assets, with 256-px thumbnails and with none — members off (the page's default,
//     the fold included) and members on (the listing's scan alone). Does a listing read each row's thumbnail
//     pages? (The assets table stores `thumbnail` before properties, view_filter and listed.)
//   * THE LAW (acceptance 1-3): each LISTING — libraryList members off and on, the Materials custom
//     drawer with a project open, the open project's tray (assettray::list), fetchProjects — runs the SAME
//     number of TRACED statements at 1,000 assets / 50 projects as at 10,000 / 500 (the query log's count
//     printed beside it), and none of its statements selects a thumbnail, by guid or not (the tile cache's
//     reads, fetchAssetThumbnailBytes / fetchProjectThumbnailBytes, are counted on their own).
//     RED AT THE BASE (066417db2, kept in ~/Developer/spikes/large-library-1/stage1/red-first.txt):
//       the fold +1,890 traced (stamped textures 70 -> 700, 3 statements each: foldedIntoBundle
//       assettray.cpp:149-157 -> hiddenAsMember materialmembers.cpp:60-80: fetchAsset, hasMultipleDependers,
//       fetchAsset of the one depender), and those fetchAssets select the thumbnail;
//       the drawer +1,800 traced / +900 logged (900 more Materials-home materials, 2 each: isAssetPinnedBy
//       effectspage.cpp:2025-2026 -> database.cpp:1822-1830, whose checkIfTableExists (database.cpp:743-757)
//       is raw SQL the log never sees).
//   * THE V-2 RULE, batched (materialmembers::hiddenAsMembers): a stamped picture folds only while it is
//     used and every user is a material; one a scene node uses (a depender the catalog does not know), one
//     a model uses, and one nothing uses stay tiles, and an unstamped one is never folded.
//   * STAGE 1b, THE COVERING INDEXES (Database::createIndexes):
//     (1) THEY RESET NO LIBRARY: a library written with the previous index set, opened by this build, keeps
//         its generation (PRAGMA user_version) and passes the schema check (schemaMatchesFresh: tables'
//         columns only), keeps its rows, and gains the new indexes at the boot's gate; the retired one goes.
//     (2) THE PLANNER USES THEM: EXPLAIN QUERY PLAN of the Assets page's listing, the drawers' counts and
//         a preset's copies names each covering index.
//     (3) THEIR COST, printed: each index's size at 1,000 and 10,000; the pages written and statements run
//         by a properties update, a rename and 100 imported assets, with the previous set and with this one;
//         the boot's one-time build over an existing 10,000-asset library (ms, warm), and the open after it (0
//         index statements).
//   * THE BOOT'S INDEX GATE (Database::prepareIndexes, main.cpp: before the shell opens the library, on a
//     worker): what it builds, that a prepared library's open runs no index statement, its negative control (an
//     unprepared library's open says DEFECT) and a failed build named.
//     (4) THE BYTES LAW: each of those three statements reads, on a cold connection at 1,000 assets, EXACTLY
//         the pages it reads in the same library with no thumbnail. Derivation: a covering index answers
//         the statement from its own pages, which hold no thumbnail byte; the two libraries are grown by
//         the same calls and differ ONLY in their thumbnails (the generator's rows are deterministic), so
//         their indexes are built from the same entries in the same order and are page for page the same
//         size. One table page read — a thumbnail chain walked — breaks the equality. RED before 1b: the
//         listing 6,562 pages against 222 (29.6x; spikes/large-library-1/stage1/red-first-1b.txt).
//     THE RESIDUE (B), printed, NEVER asserted: the listings that select a column an index cannot
//     reasonably carry, or that are project-scaled, read the thumbnail chains of their rows — they need the
//     thumbnail bytes out of the assets row, which is a migration: the owner's question.
//   * THE GENERATOR (tests/support/libraryfixture.h) writes the library the laws run on; its rows are
//     checked here by what the listings see of them. `--generate <db> <store> <assets> <projects>` writes
//     one library and exits (the column comparison's input; stage 2's roots).
//
// THE STATED LIMIT (ruled call 3): only the default connection is traced; a per-asset query moved onto
// a worker connection would not be seen here.
// Headless (offscreen); never touches the user's JahLibrary.db. The row's ENVIRONMENT sets
// JAH_DB_STATEMENT_TRACE=1 (tests/assettray/CMakeLists.txt).

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <cstdio>
#include <cstring>
#include <functional>

#include "data/database/casschema.h"
#if defined(JAH_HAVE_SQLITE3)
#include <QSqlDriver>
#include <sqlite3.h>
#endif

#include "data/database/database.h"
#include "data/database/databasereads.h"
#include "irisgl/core/logger.h"
#include "services/assettray.h"
#include "services/assetcas.h"
#include "services/memberstamp.h"
#include "services/meshbakestore.h"
#include "services/projectassets.h"
#include "../support/libraryfixture.h"

static int failures = 0;
#define CHECK(cond, ...)                                                                   \
    do {                                                                                   \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); }  \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
        std::fflush(stdout);                                                               \
    } while (0)

static QString newGuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

static qint64 traced() { return databasereads::read().statements; }

/// THE CHILD: the same Database opened with the env unset. Prints one line the parent parses.
static int envUnsetControl(const QString &dbPath)
{
    QFile::remove(dbPath);
    Database db;
    if (!db.initializeDatabase(dbPath)) { std::printf("control: open failed\n"); return 2; }
    db.createAllTables();
    for (int i = 0; i < 10; ++i) db.fetchAsset(newGuid());
    const databasereads::Stats r = databasereads::read();
    std::printf("control available=%d traced=%d statements=%lld\n", r.available ? 1 : 0, r.traced ? 1 : 0,
                static_cast<long long>(r.statements));
    db.closeDatabase();
    QFile::remove(dbPath);
    return 0;
}

/// What the default connection did around one step: the pages its pager read from the file and the
/// statements it ran.
struct Reading {
    qint64 pages = 0;
    qint64 bytes = 0;
    qint64 statements = 0;
};

static Reading around(const std::function<void()> &step)
{
    const databasereads::Stats a = databasereads::read();
    step();
    const databasereads::Stats b = databasereads::read();
    return { b.pages - a.pages, (b.pages - a.pages) * b.pageSize, b.statements - a.statements };
}

/// The Library listing on a COLD connection (closed and reopened: the pager's cache empty), members off and on.
struct ColdListing {
    Reading membersOff;
    Reading membersOn;
};

static ColdListing coldListing(Database &db, const QString &dbPath)
{
    db.closeDatabase();
    db.initializeDatabase(dbPath);
    ColdListing out;
    out.membersOff = around([&] { assettray::libraryList(&db, false); });
    db.closeDatabase();
    db.initializeDatabase(dbPath);
    out.membersOn = around([&] { assettray::libraryList(&db, true); });
    return out;
}

static void printM1(const char *label, const ColdListing &l, qint64 thumbnailBytes)
{
    std::printf("   M1 %s (%lld thumbnail bytes in the library): libraryList members off %lld pages = %lld B, %lld statements;"
                " members on %lld pages = %lld B, %lld statements\n",
                label, static_cast<long long>(thumbnailBytes), static_cast<long long>(l.membersOff.pages),
                static_cast<long long>(l.membersOff.bytes), static_cast<long long>(l.membersOff.statements),
                static_cast<long long>(l.membersOn.pages), static_cast<long long>(l.membersOn.bytes),
                static_cast<long long>(l.membersOn.statements));
}

/// One listing's statements, counted both ways.
struct Count {
    qint64 traced = 0;          ///< every statement the default connection ran (SQLite's trace)
    int logged = 0;             ///< the query log's count (executeAndCheckQuery), the tile reads excluded
    int thumbnailSelects = 0;   ///< logged statements that select a thumbnail column (the tile reads excluded)
    int tileReads = 0;          ///< fetchAssetThumbnailBytes / fetchProjectThumbnailBytes
};

static Count countOf(const std::function<void()> &listing)
{
    Database::setQueryLog(true);
    const qint64 t0 = traced();
    listing();
    Count out;
    out.traced = traced() - t0;
    for (const Database::QueryLogEntry &e : Database::queryLogEntries()) {
        if (e.name == QLatin1String("fetchAssetThumbnailBytes") || e.name == QLatin1String("fetchProjectThumbnailBytes")) {
            out.tileReads += e.count;
            continue;
        }
        out.logged += e.count;
        if (e.selectsThumbnail) out.thumbnailSelects += e.count;
    }
    Database::setQueryLog(false);
    return out;
}

struct Listing {
    const char *name;
    std::function<void()> run;
};

static QVector<Listing> listings(Database &db, const libraryfixture::Library &lib)
{
    Database *d = &db;
    const QString project = lib.openProject;
    return {
        { "libraryList members off", [d] { assettray::libraryList(d, false); } },
        { "libraryList members on", [d] { assettray::libraryList(d, true); } },
        { "the custom drawer, a project open", [d, project] { assettray::customDrawer(d, project); } },
        { "the open project's tray", [d, project] { assettray::list(d, project, QString(), -1, false); } },
        { "fetchProjects (the Desktop)", [d] { d->fetchProjects(1); } },
    };
}

/// The V-2 rule's branches, on five pictures of the Assets home.
static void theRule(Database &db)
{
    const auto picture = [&](const QString &name) {
        const QString guid = newGuid();
        db.createAssetEntry(guid, name, static_cast<int>(ModelTypes::Texture), QString(), assethome::assets(),
                            assethome::Origin::Import);
        return guid;
    };
    const auto material = [&](const QString &name) {
        const QString guid = newGuid();
        db.createAssetEntry(guid, name, static_cast<int>(ModelTypes::Material), QString(), assethome::assets(),
                            assethome::Origin::ExplicitSave);
        return guid;
    };
    const QString mat = material(QStringLiteral("rule material")), other = material(QStringLiteral("rule other"));
    const QString model = newGuid();
    db.createAssetEntry(model, QStringLiteral("rule model"), static_cast<int>(ModelTypes::Object), QString(),
                        assethome::assets(), assethome::Origin::Import);
    const QString folds = picture(QStringLiteral("rule folds.png")), nodeUses = picture(QStringLiteral("rule node.png")),
                  modelUses = picture(QStringLiteral("rule model.png")), unused = picture(QStringLiteral("rule unused.png")),
                  unstamped = picture(QStringLiteral("rule unstamped.png"));
    const int M = static_cast<int>(ModelTypes::Material), T = static_cast<int>(ModelTypes::Texture);
    db.createDependency(M, T, mat, folds);
    db.createDependency(M, T, other, folds);
    db.createDependency(M, T, mat, nodeUses);
    db.createDependency(static_cast<int>(ModelTypes::Object), T, newGuid(), nodeUses);   // a scene node's id
    db.createDependency(M, T, mat, modelUses);
    db.createDependency(static_cast<int>(ModelTypes::Object), T, model, modelUses);
    db.createDependency(M, T, mat, unstamped);
    for (const QString &g : { folds, nodeUses, modelUses, unused }) memberstamp::stamp(&db, g, mat);
    const QStringList hidden = assettray::libraryHidden(&db, assettray::libraryList(&db, true), false);
    CHECK(hidden.contains(folds), "V-2: a stamped picture two materials use folds into its bundle");
    CHECK(!hidden.contains(nodeUses), "V-2: ...one a scene node also uses stays a tile");
    CHECK(!hidden.contains(modelUses), "V-2: ...one a model also uses stays a tile");
    CHECK(!hidden.contains(unused), "V-2: ...one nothing uses stays a tile");
    CHECK(!hidden.contains(unstamped), "V-2: ...and the user's own picture a material uses is never folded");
    CHECK(assettray::libraryHidden(&db, assettray::libraryList(&db, true), true).isEmpty(),
          "V-2: with the members shown nothing is folded");
    // The editor tray applies the same rule to a project's pinned pictures.
    const QString project = newGuid();
    db.createProject(project, QStringLiteral("rule project"));
    for (const QString &g : { folds, nodeUses }) AssetCas::writePin(QSqlDatabase::database(), project, g, QString());
    QStringList tray;
    for (const AssetRecord &r : assettray::list(&db, project, QString(), -1, false)) tray << r.guid;
    QStringList trayMembers;
    for (const AssetRecord &r : assettray::list(&db, project, QString(), -1, true)) trayMembers << r.guid;
    CHECK(!tray.contains(folds) && tray.contains(nodeUses) && trayMembers.contains(folds),
          "V-2 in the tray: the folding picture is hidden, the node's picture listed, and both with the members shown");
}

// ---- STAGE 1b ----------------------------------------------------------------------------------------------
static const char *const kNewIndexes[] = { "idx_assets_library", "idx_assets_collection_listed", "idx_assets_material_copies" };

/// The index set of the build before 1b: the three dropped, the one widened back.
static void previousIndexSet()
{
    QSqlQuery q(QSqlDatabase::database());
    for (const char *name : kNewIndexes) q.exec(QStringLiteral("DROP INDEX IF EXISTS %1").arg(QLatin1String(name)));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_assets_collection ON assets (collection)"));
}

static QStringList indexNames()
{
    QStringList out;
    QSqlQuery q(QSqlDatabase::database());
    q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'index'"));
    while (q.next()) out << q.value(0).toString();
    return out;
}

static qint64 scalar(const QString &sql)
{
    QSqlQuery q(QSqlDatabase::database());
    return q.exec(sql) && q.next() ? q.value(0).toLongLong() : -1;
}

/// The pages this connection's pager wrote (SQLITE_DBSTATUS_CACHE_WRITE), the write half of databaseReads.
static qint64 pagesWritten()
{
#if defined(JAH_HAVE_SQLITE3)
    const QVariant h = QSqlDatabase::database().driver()->handle();
    if (!h.isValid() || std::strcmp(h.typeName(), "sqlite3*") != 0) return -1;
    int cur = 0, high = 0;
    sqlite3_db_status(*static_cast<sqlite3 *const *>(h.constData()), SQLITE_DBSTATUS_CACHE_WRITE, &cur, &high, 0);
    return cur;
#else
    return -1;
#endif
}

/// `sql` with each `?` / `:name` placeholder replaced by the next of `values`, for EXPLAIN QUERY PLAN.
static QString planOf(QString sql, const QStringList &values)
{
    int next = 0;
    static const QRegularExpression placeholder(QStringLiteral("\\?|:[A-Za-z_]+"));
    QRegularExpressionMatch m;
    while ((m = placeholder.match(sql)).hasMatch()) sql.replace(m.capturedStart(), m.capturedLength(), values.value(next++));
    QSqlQuery q(QSqlDatabase::database());
    QString plan;
    if (q.exec(QStringLiteral("EXPLAIN QUERY PLAN ") + sql))
        while (q.next()) plan += q.value(3).toString() + QStringLiteral(" ; ");
    return plan;
}

/// A preset's copies, the statement of presetedit.cpp copiesOf with its type (that file is not in this row's
/// closure; the row checks the text is still the one there).
static const char *const kCopiesOfText[] = { "SELECT guid, properties FROM assets INDEXED BY idx_assets_material_copies ",
                                             "WHERE type = %1 ORDER BY rowid" };
static QString copiesOfSql()
{
    return QStringLiteral("SELECT guid, properties FROM assets INDEXED BY idx_assets_material_copies WHERE type = %1 ORDER BY rowid")
        .arg(static_cast<int>(ModelTypes::Material));
}

static void copiesOfRun()
{
    QSqlQuery q(QSqlDatabase::database());
    q.exec(copiesOfSql());
    while (q.next()) {}
}

/// The three statements the covering indexes serve, each on a cold connection.
struct Covered {
    Reading listing;   ///< fetchAssetsForAssetView: the Assets page (libraryList members on)
    Reading counts;    ///< countAssetsInCollections: the drawers' counts
    Reading copies;    ///< presetedit copiesOf
};

static Covered coldCovered(Database &db, const QString &dbPath, const libraryfixture::Library &lib)
{
    const auto cold = [&](const std::function<void()> &step) {
        db.closeDatabase();
        db.initializeDatabase(dbPath);
        return around(step);
    };
    Covered out;
    out.listing = cold([&] { db.fetchAssetsForAssetView(); });
    out.counts = cold([&] { db.countAssetsInCollections({ lib.drawer }); });
    out.copies = cold([&] { copiesOfRun(); });
    return out;
}

/// PREVIEWS-1 FIX 1: what previews.state's one read (Database::fetchPreviewRows over the whole library) costs on a
/// cold connection — under the product's default cache, and under one that holds the working set (no page is read
/// twice) beside a HEADER-ONLY scan of the assets table (length() of a BLOB reads the record header, never its chain).
struct PreviewPages {
    Reading state;      ///< fetchPreviewRows, the default cache
    Reading stateWide;  ///< fetchPreviewRows, a 64-MB cache
    Reading scanWide;   ///< the assets table's b-tree once, header-only, a 64-MB cache
    /// previews.state / previews.counts over the OPEN PROJECT's rows (fetchPreviewRows, projectOnly), and its control:
    /// a header-only read of the SAME rows by the SAME WHERE and order (the planner reaches the assets table alike).
    Reading projectWide;
    Reading projectScanWide;
    Reading projectChainWide;   ///< NEGATIVE CONTROL: the same rows reading `properties` from the table (a chain walk)
};

static PreviewPages coldPreviewPages(Database &db, const QString &dbPath, const QString &openProject)
{
    const auto cold = [&](bool wide, const std::function<void()> &step) {
        db.closeDatabase();
        db.initializeDatabase(dbPath);
        if (wide) QSqlQuery(QSqlDatabase::database()).exec(QStringLiteral("PRAGMA cache_size = -65536"));
        return around(step);
    };
    const auto state = [&db] { db.fetchPreviewRows(QString(), false, -1); };
    PreviewPages out;
    out.state = cold(false, state);
    out.stateWide = cold(true, state);
    out.scanWide = cold(true, [] {
        QSqlQuery q(QSqlDatabase::database());
        q.exec(QStringLiteral("SELECT sum(length(thumbnail) > 0) + sum(length(asset) > 0) FROM assets NOT INDEXED"));
        while (q.next()) {}
    });
    out.projectWide = cold(true, [&db, &openProject] { db.fetchPreviewRows(openProject, true, -1); });
    out.projectScanWide = cold(true, [&openProject] {
        QSqlQuery q(QSqlDatabase::database());
        q.prepare(QStringLiteral("SELECT length(a.thumbnail) > 0, length(a.asset) > 0 FROM assets a WHERE 1 AND a.guid IN "
                                 "(SELECT asset_guid FROM project_assets WHERE project_guid = ?) ORDER BY a.rowid"));
        q.addBindValue(openProject);
        q.exec();
        while (q.next()) {}
    });
    out.projectChainWide = cold(true, [&openProject] {
        QSqlQuery q(QSqlDatabase::database());
        q.prepare(QStringLiteral("SELECT length(a.thumbnail) > 0, instr(COALESCE(a.properties, ''), '\"platform\"') > 0 "
                                 "FROM assets a WHERE 1 AND a.guid IN "
                                 "(SELECT asset_guid FROM project_assets WHERE project_guid = ?) ORDER BY a.rowid"));
        q.addBindValue(openProject);
        q.exec();
        while (q.next()) {}
    });
    return out;
}

/// (B): the listings an index cannot serve, on a cold connection — printed with their factor, never asserted.
struct ResidueRow {
    const char *name;
    const char *where;
    const char *scale;
    std::function<void(Database &, const libraryfixture::Library &)> run;
};

static QVector<ResidueRow> residueRows()
{
    using L = const libraryfixture::Library &;
    return {
        { "the Materials custom drawer (fetchAssetsInHome: asset, properties)", "database.cpp fetchAssetsInHome", "library",
          [](Database &d, L l) { assettray::customDrawer(&d, l.openProject); } },
        { "the open project's tray (assettray::list)", "services/assettray.cpp list", "project",
          [](Database &d, L l) { assettray::list(&d, l.openProject, QString(), -1, false); } },
        { "fetchProjectPinnedAssets (by-guid join: properties, view_filter, tags, listed)", "database.cpp fetchProjectPinnedAssets",
          "project", [](Database &d, L l) { d.fetchProjectPinnedAssets(l.openProject); } },
        { "fetchChildAssets (properties)", "database.cpp fetchChildAssets", "project",
          [](Database &d, L l) { d.fetchChildAssets(l.openProject, l.openProject, -1); } },
        { "fetchAssetsByType (asset)", "database.cpp fetchAssetsByType", "project",
          [](Database &d, L l) { d.fetchAssetsByType(static_cast<int>(ModelTypes::Texture), l.openProject); } },
        { "fetchFavorites (listed, by guid)", "database.cpp fetchFavorites", "favourites",
          [](Database &d, L) { d.fetchFavorites(); } },
        { "a project open's session rows (readSessionRows, Texture: a scan by type, asset)", "services/projectassets.cpp readSessionRows",
          "library", [](Database &, L l) {
              int queries = 0;
              ProjectAssets::readSessionRows(QSqlDatabase::database(), QDir::currentPath(), l.openProject,
                                             static_cast<int>(ModelTypes::Texture), &queries);
          } },
    };
}

static QVector<Reading> coldResidue(Database &db, const QString &dbPath, const libraryfixture::Library &lib)
{
    QVector<Reading> out;
    for (const ResidueRow &r : residueRows()) {
        db.closeDatabase();
        db.initializeDatabase(dbPath);
        out.append(around([&] { r.run(db, lib); }));
    }
    return out;
}

/// THE SWEEP'S PLANNING ON ITS OWN CONNECTION (LARGE-LIBRARY-1 R5): MeshBakeStore::modelBakesNeeded as the boot's
/// bake sweep runs it — a read-only connection of its own (meshbakestore.cpp startBackgroundRebuild) — its
/// statements counted by an SQLite statement trace this row registers on THAT connection's handle (the app's
/// trace, databasereads, is the default connection's only: the ruled limit). `perModel` true runs the single-row
/// planner (MeshBakeStore::planFor) once per model instead: the negative control, N statements and more.
struct SweepPlan { qint64 statements = -1; int needed = -1; qint64 ms = 0; };
#if defined(JAH_HAVE_SQLITE3)
static int countStatement(unsigned type, void *counter, void *, void *sql)
{
    const char *text = static_cast<const char *>(sql);
    if (type == SQLITE_TRACE_STMT && !(text && text[0] == '-' && text[1] == '-')) ++*static_cast<qint64 *>(counter);
    return 0;
}
#endif
static SweepPlan sweepPlan(const QString &dbPath, const QString &store, const QStringList &perModel = {})
{
    SweepPlan out;
#if defined(JAH_HAVE_SQLITE3)
    const QString name = QStringLiteral("sweep-plan-") + newGuid();
    {
        QSqlDatabase conn = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        conn.setDatabaseName(dbPath);
        conn.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000"));
        if (conn.open()) {
            qint64 counter = 0;
            sqlite3 *handle = *static_cast<sqlite3 *const *>(conn.driver()->handle().constData());
            sqlite3_trace_v2(handle, SQLITE_TRACE_STMT, countStatement, &counter);
            QElapsedTimer t;
            t.start();
            if (perModel.isEmpty()) {
                out.needed = int(MeshBakeStore::modelBakesNeeded(conn, store).size());
            } else {
                out.needed = 0;
                for (const QString &guid : perModel)
                    if (MeshBakeStore::planFor(conn, store, MeshBakeStore::modelSourceFor(conn, store, guid), guid).bakePath.isEmpty())
                        ++out.needed;
            }
            out.ms = t.elapsed();
            out.statements = counter;
            sqlite3_trace_v2(handle, 0, nullptr, nullptr);
            conn.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
#else
    Q_UNUSED(dbPath); Q_UNUSED(store); Q_UNUSED(perModel);
#endif
    return out;
}

/// How many index statements an open of the library ran (the query log's CreateIndex entries).
static int indexStatementsAtOpen(Database &db)
{
    Database::setQueryLog(true);
    db.createAllTables();   // ShellLifecycle::openLibrary
    int n = 0;
    for (const Database::QueryLogEntry &e : Database::queryLogEntries())
        if (e.name == QLatin1String("CreateIndex")) n += e.count;
    Database::setQueryLog(false);
    return n;
}

/// THE BOOT'S INDEX GATE (main.cpp, before the shell opens the library): Database::prepareIndexes builds on its
/// own connection what an UPGRADED library (the previous index set, rows in it) lacks; after it the shell's open
/// runs NO index statement and says no defect. NEGATIVE CONTROL: the same library opened unprepared builds them
/// on the opening thread and says DEFECT. A build that FAILS (a read-only file) answers false, the statement and
/// SQLite's reason named — the boot's error (exit 4).
static void theBootGate()
{
    const QString path = QStringLiteral("library_statements_gate.db");
    const QString store = QDir::currentPath() + QStringLiteral("/library_statements_gate_store");
    // The file AND its -wal / -shm: the read-only open below leaves both, read-only like the file.
    const auto drop = [&] {
        for (const QString &suffix : { QString(), QStringLiteral("-wal"), QStringLiteral("-shm") }) {
            QFile(path + suffix).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            QFile::remove(path + suffix);
        }
    };
    const auto upgraded = [&] {
        drop();
        QDir(store).removeRecursively();
        Database db;
        db.initializeDatabase(path);
        db.createAllTables();
        libraryfixture::Library lib;
        QString error;
        libraryfixture::grow(&db, store, 100, 2, lib, {}, &error);
        previousIndexSet();
        db.closeDatabase();
    };
    QStringList defects;
    iris::Logger::setSink([&defects](int, const QString &text) {
        if (text.contains(QLatin1String("DEFECT"))) defects << text;
    });

    upgraded();
    const int before = Database::indexStatementsToRun(path);
    QString error;
    const bool built = Database::prepareIndexes(path, &error);
    CHECK(before == 4 && built && error.isEmpty() && Database::indexStatementsToRun(path) == 0,
          "GATE: an upgraded library needs %d index statements (the three covering CREATEs and the retired DROP: 4); "
          "the gate builds them and then it needs none%s%s", before, error.isEmpty() ? "" : ": ", qPrintable(error));
    {
        Database db;
        db.initializeDatabase(path);
        const int atOpen = indexStatementsAtOpen(db);
        CHECK(atOpen == 0 && defects.isEmpty(),
              "GATE: a PREPARED library's open runs 0 index statements (%d) and says no defect (%d)", atOpen,
              int(defects.size()));
        db.closeDatabase();
    }

    upgraded();
    {
        Database db;
        db.initializeDatabase(path);
        const int atOpen = indexStatementsAtOpen(db);
        CHECK(atOpen == 4 && defects.size() == 1,
              "GATE NEGATIVE CONTROL: the same library opened UNPREPARED runs its %d index statements on the opening "
              "thread and says DEFECT (%d)", atOpen, int(defects.size()));
        db.closeDatabase();
    }
    iris::Logger::setSink({});

    upgraded();
    QFile(path).setPermissions(QFileDevice::ReadOwner | QFileDevice::ReadGroup);
    error.clear();
    const bool refused = !Database::prepareIndexes(path, &error);
    CHECK(refused && error.contains(QLatin1String("CREATE INDEX")) && error.contains(QLatin1String("readonly")),
          "GATE: a build that fails (a read-only library) answers false with the statement and its reason: %s",
          qPrintable(error));
    drop();
    QDir(store).removeRecursively();
    QDir(store + QStringLiteral("-sources")).removeRecursively();
}

/// (1): a library written with the previous index set keeps its generation, its rows and its schema check.
static void theOldLibraryKeeps()
{
    const QString path = QStringLiteral("library_statements_old.db");
    const QString store = QDir::currentPath() + QStringLiteral("/library_statements_old_store");
    QFile::remove(path);
    QDir(store).removeRecursively();
    Database db;
    db.initializeDatabase(path);
    db.createAllTables();
    libraryfixture::Library lib;
    QString error;
    libraryfixture::grow(&db, store, 100, 2, lib, {}, &error);
    previousIndexSet();
    const qint64 rows = scalar(QStringLiteral("SELECT COUNT(*) FROM assets"));
    db.closeDatabase();

    db.initializeDatabase(path);   // what librarygeneration::checkAndWipe reads before it decides
    const qint64 generation = scalar(QStringLiteral("PRAGMA user_version"));
    const bool columnsMatch = db.schemaMatchesFresh();
    CHECK(generation == CasSchema::kUserVersion && columnsMatch,
          "(1) a library with the previous index set keeps its generation (%lld = %d) and passes the schema check: "
          "nothing resets it", static_cast<long long>(generation), CasSchema::kUserVersion);
    db.closeDatabase();
    QString built;
    CHECK(Database::prepareIndexes(path, &built), "(1) the boot's gate builds the indexes over its rows%s%s",
          built.isEmpty() ? "" : ": ", qPrintable(built));
    db.initializeDatabase(path);
    db.createAllTables();   // ShellLifecycle::openLibrary
    const QStringList names = indexNames();
    bool all = true;
    for (const char *name : kNewIndexes) all &= names.contains(QLatin1String(name));
    CHECK(all && names.contains(QStringLiteral("idx_assets_parent")) && !names.contains(QStringLiteral("idx_assets_collection")),
          "(1) ...it gains the three indexes, keeps idx_assets_parent, and the retired idx_assets_collection is gone");
    CHECK(scalar(QStringLiteral("SELECT COUNT(*) FROM assets")) == rows && db.schemaMatchesFresh(),
          "(1) ...with every one of its %lld rows, and the schema check still passes", static_cast<long long>(rows));
    db.closeDatabase();
    QFile::remove(path);
    QDir(store).removeRecursively();
    QDir(store + QStringLiteral("-sources")).removeRecursively();
}

static void printIndexSizes(const char *label)
{
    QStringList parts;
    QSqlQuery q(QSqlDatabase::database());
    if (q.exec(QStringLiteral("SELECT name, SUM(pgsize) FROM dbstat WHERE name LIKE 'idx_assets%' OR name = 'assets' "
                              "GROUP BY name ORDER BY name")))
        while (q.next()) parts << QStringLiteral("%1 %2 B").arg(q.value(0).toString()).arg(q.value(1).toLongLong());
    std::printf("   (3) sizes at %s: %s\n", label, qPrintable(parts.join(QStringLiteral(", "))));
}

/// (3): what the hot writers write — a properties update, a rename, 100 imported assets.
static void printWriteCost(Database &db, const QString &store, libraryfixture::Library &lib, const char *label)
{
    const auto cost = [&](const std::function<void()> &step) {
        const qint64 w0 = pagesWritten(), t0 = traced();
        step();
        return QStringLiteral("%1 pages written, %2 statements").arg(pagesWritten() - w0).arg(traced() - t0);
    };
    const QString target = lib.textures.at(lib.textures.size() / 2);
    const QString a = cost([&] { db.updateAssetProperties(target, db.fetchAsset(target).properties + " "); });
    const QString b = cost([&] { db.updateAssetMetadata(target, QStringLiteral("renamed %1").arg(lib.assets), QByteArray()); });
    QString error;
    const QString c = cost([&] { libraryfixture::grow(&db, store, 100, 0, lib, {}, &error); });
    std::printf("   (3) writes, %s: a properties update %s; a rename %s; 100 imported assets %s\n", label, qPrintable(a),
                qPrintable(b), qPrintable(c));
}

/// `--generate`: one library, written and closed.
static int generateOnly(const QStringList &args)
{
    const QString dbPath = args.at(2), store = args.at(3);
    const int assets = args.at(4).toInt(), projects = args.at(5).toInt();
    QFile::remove(dbPath);
    Database db;
    if (!db.initializeDatabase(dbPath)) { std::printf("generate: open failed\n"); return 2; }
    db.createAllTables();
    libraryfixture::Library lib;
    QString error;
    QElapsedTimer timer;
    timer.start();
    const bool ok = libraryfixture::grow(&db, store, assets, projects, lib, {}, &error);
    std::printf("generate: %s — %d assets, %d projects in %lld ms (thumbnails %lld B); open project %s%s%s\n",
                ok ? "ok" : "FAILED", lib.assets, int(lib.projects.size()), static_cast<long long>(timer.elapsed()),
                static_cast<long long>(lib.thumbnailBytes), qPrintable(lib.openProject), ok ? "" : " — ",
                qPrintable(error));
    std::printf("generate: texture %s material %s picked %s home %s homePicked %s object %s mesh %s project %s\n",
                qPrintable(lib.textures.value(1)), qPrintable(lib.assetsMaterials.value(0)),
                qPrintable(lib.stampedTextures.value(0)), qPrintable(lib.homeMaterials.value(0)),
                qPrintable(lib.homePicked.value(0)),
                qPrintable(lib.objects.value(0)), qPrintable(lib.meshes.value(0)), qPrintable(lib.projects.value(1)));
    db.closeDatabase();
    return ok ? 0 : 1;
}

/// The generator's rows, as the listings see them.
static void theGenerator(Database &db, const libraryfixture::Library &lib, int assets)
{
    CHECK(lib.assets == assets && lib.textures.size() == assets * 46 / 100 && lib.stampedTextures.size() == assets * 7 / 100
              && lib.homeMaterials.size() == assets / 10 && lib.homePicked.size() == assets / 10
              && lib.objects.size() == assets / 10,
          "the mix at %d: %d textures, %d stamped, %d Materials-home materials, %d models", assets,
          int(lib.textures.size()), int(lib.stampedTextures.size()), int(lib.homeMaterials.size()),
          int(lib.objects.size()));
    // The Assets page lists the textures, the Assets-home materials and the models (a Mesh is a member);
    // the stamped pictures fold into their materials unless the members are shown.
    const int shown = assets * (46 + 7 + 10) / 100;
    const int listed = int(assettray::libraryList(&db, false).size());
    const int withMembers = int(assettray::libraryList(&db, true).size());
    CHECK(listed == shown, "the Library listing at %d assets holds %d rows (%d)", assets, shown, listed);
    CHECK(withMembers == shown + int(lib.stampedTextures.size()), "...and %d with the member textures shown (%d)",
          shown + int(lib.stampedTextures.size()), withMembers);
    CHECK(db.fetchAssetsInHome(assethome::Kind::MaterialsLibrary, static_cast<int>(ModelTypes::Material)).size()
              == lib.homeMaterials.size(),
          "the Materials home holds the %d generated materials", int(lib.homeMaterials.size()));
}

static void theTrace(Database &db)
{
    const databasereads::Stats first = databasereads::read();
    CHECK(first.available, "databaseReads is available here (%s)", qPrintable(first.why));
    CHECK(first.traced, "the statement trace is registered on the default connection (JAH_DB_STATEMENT_TRACE=1)");
    CHECK(first.statements > 0, "the open and the schema's statements were counted (%lld)",
          static_cast<long long>(first.statements));
    CHECK(traced() == first.statements, "a read of the count runs nothing that is counted (its own PRAGMA)");

    const int n = 50;
    Database::setQueryLog(true);
    qint64 t0 = traced();
    for (int i = 0; i < n; ++i) db.fetchAsset(newGuid());
    CHECK(traced() - t0 == n, "NEGATIVE CONTROL: a fetchAsset loop of %d grows the traced count by %d (%lld)", n, n,
          static_cast<long long>(traced() - t0));
    CHECK(Database::queryLogStatements() == n, "...and the query log's by %d (%d)", n, Database::queryLogStatements());

    Database::setQueryLog(true);
    t0 = traced();
    for (int i = 0; i < n; ++i) {
        QSqlQuery raw(QSqlDatabase::database());
        raw.prepare(QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE name = ?"));
        raw.addBindValue(newGuid());
        raw.exec();
    }
    CHECK(traced() - t0 == n, "NEGATIVE CONTROL: a raw QSqlQuery loop of %d grows the traced count by %d (%lld)", n, n,
          static_cast<long long>(traced() - t0));
    CHECK(Database::queryLogStatements() == 0, "...and the query log's not at all (%d)", Database::queryLogStatements());
    Database::setQueryLog(false);

    // A trigger's sub-statement is not a statement anyone asked for: one INSERT that fires one is ONE.
    QSqlQuery q(QSqlDatabase::database());
    q.exec(QStringLiteral("CREATE TEMP TABLE trace_a (x INTEGER)"));
    q.exec(QStringLiteral("CREATE TEMP TABLE trace_b (x INTEGER)"));
    q.exec(QStringLiteral("CREATE TEMP TRIGGER trace_t AFTER INSERT ON trace_a BEGIN INSERT INTO trace_b VALUES (new.x); END"));
    t0 = traced();
    q.exec(QStringLiteral("INSERT INTO trace_a VALUES (1)"));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM trace_b"));
    const bool fired = q.next() && q.value(0).toInt() == 1;
    CHECK(fired && traced() - t0 == 2, "an INSERT whose trigger fired counts once, not twice (2 statements: %lld)",
          static_cast<long long>(traced() - t0));
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() == 3 && args.at(1) == QLatin1String("--env-unset-control")) return envUnsetControl(args.at(2));
    if (args.size() == 6 && args.at(1) == QLatin1String("--generate")) return generateOnly(args);

    CHECK(qEnvironmentVariable("JAH_DB_STATEMENT_TRACE") == QLatin1String("1"),
          "the row runs with JAH_DB_STATEMENT_TRACE=1");

    // ---- THE ENV UNSET: NOTHING REGISTERED, NOTHING COUNTED (a child, since the door is read once) --
    {
        QProcess child;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("JAH_DB_STATEMENT_TRACE"));
        child.setProcessEnvironment(env);
        child.start(QCoreApplication::applicationFilePath(),
                    { QStringLiteral("--env-unset-control"), QStringLiteral("library_statements_control.db") });
        const bool done = child.waitForFinished(30000);
        const QString out = QString::fromLocal8Bit(child.readAllStandardOutput()).trimmed();
        std::printf("   child: %s\n", qPrintable(out));
        CHECK(done && child.exitCode() == 0 && out.contains(QStringLiteral("available=1")),
              "the env-unset child ran on an available connection");
        CHECK(out.contains(QStringLiteral("traced=0 statements=0")),
              "NEGATIVE CONTROL: with the env unset `traced` is false and `statements` 0");
    }

    const QString dbPath = QStringLiteral("library_statements_test.db");
    QFile::remove(dbPath);
    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");
    db.createAllTables();

    theTrace(db);
    db.closeDatabase();
    QFile::remove(dbPath);

    // ---- M1 WITH NO THUMBNAILS: its own library at 1,000 / 50 ---------------------------------------
    const QString store = QDir::currentPath() + QStringLiteral("/library_statements_store");
    const auto clearStore = [&] {
        QDir(store).removeRecursively();
        QDir(store + QStringLiteral("-sources")).removeRecursively();
    };
    QString error;
    ColdListing none;
    Covered coveredNone;
    QVector<Reading> residueNone;
    PreviewPages previewsNone;
    {
        clearStore();
        const QString bare = QStringLiteral("library_statements_bare.db");
        QFile::remove(bare);
        CHECK(db.initializeDatabase(bare), "the thumbnail-free database opened");
        db.createAllTables();
        libraryfixture::Library bareLib;
        libraryfixture::Options options;
        options.thumbnails = false;
        CHECK(libraryfixture::grow(&db, store, 1000, 50, bareLib, options, &error) && bareLib.thumbnailBytes == 0,
              "the generator wrote 1,000 assets with no thumbnail%s%s", error.isEmpty() ? "" : ": ", qPrintable(error));
        none = coldListing(db, bare);
        coveredNone = coldCovered(db, bare, bareLib);
        residueNone = coldResidue(db, bare, bareLib);
        previewsNone = coldPreviewPages(db, bare, bareLib.openProject);
        db.closeDatabase();
        QFile::remove(bare);
    }

    // ---- THE LIBRARY AT 1,000 ASSETS / 50 PROJECTS (thumbnails), the one the laws run on ---------------
    clearStore();
    QFile::remove(dbPath);
    CHECK(db.initializeDatabase(dbPath), "the library database opened");
    db.createAllTables();
    libraryfixture::Library lib;
    QElapsedTimer timer;
    timer.start();
    CHECK(libraryfixture::grow(&db, store, 1000, 50, lib, {}, &error), "the generator wrote 1,000 assets and 50 projects%s%s",
          error.isEmpty() ? "" : ": ", qPrintable(error));
    std::printf("   generated in %lld ms, %lld thumbnail bytes\n", static_cast<long long>(timer.elapsed()),
                static_cast<long long>(lib.thumbnailBytes));
    // THE PAGE LAWS' POSITIVE CONTROL: the with-thumbnails library really holds its pictures (else "the same pages
    // with thumbnails as with none" would pass on nothing): every texture, material and model row carries one.
    const qint64 pictures = scalar(QStringLiteral("SELECT COUNT(*) FROM assets WHERE length(thumbnail) > 0"));
    const qint64 pictureBytes = scalar(QStringLiteral("SELECT SUM(length(thumbnail)) FROM assets"));
    CHECK(lib.thumbnailBytes > 0 && pictures >= lib.assets - int(lib.meshes.size()) && pictureBytes == lib.thumbnailBytes,
          "POSITIVE CONTROL: the library the page laws read holds %lld thumbnails, %lld bytes (the generator wrote %lld)",
          static_cast<long long>(pictures), static_cast<long long>(pictureBytes), static_cast<long long>(lib.thumbnailBytes));
    theGenerator(db, lib, 1000);
    // A generated model is a real import's: its source parses and its bake is current, so the boot's bake sweep
    // (MeshBakeStore::startBackgroundRebuild plans with modelBakesNeeded) finds NOTHING to rebuild. A fake
    // model made the sweep "rebuild" every model at every boot, its failures landing on the UI thread.
    const SweepPlan sweepSmall = sweepPlan(dbPath, store);
    CHECK(sweepSmall.needed == 0,
          "the generated library's %d models have current bakes: the bake sweep has nothing to rebuild (%d)",
          int(lib.objects.size()), sweepSmall.needed);
    const SweepPlan perModelSmall = sweepPlan(dbPath, store, lib.objects);

    // ---- M1: THE TWO NUMBERS ----------------------------------------------------------------------------
    const ColdListing with = coldListing(db, dbPath);
    printM1("with 256-px thumbnails", with, lib.thumbnailBytes);
    printM1("with none             ", none, 0);
    CHECK(with.membersOn.statements == none.membersOn.statements,
          "M1: the listing's scan runs the same statements with and without thumbnails (%lld)",
          static_cast<long long>(with.membersOn.statements));

    // ---- 1b (4): THE BYTES LAW, and (2) the plans ----------------------------------------------------------
    {
        const Covered c = coveredNone, w = coldCovered(db, dbPath, lib);
        const auto law = [](const char *name, const Reading &with, const Reading &bare) {
            CHECK(with.pages == bare.pages, "(4) %s reads the same pages with 256-px thumbnails as with none: %lld = %lld "
                  "(%lld B and %lld B)", name, static_cast<long long>(with.pages), static_cast<long long>(bare.pages),
                  static_cast<long long>(with.bytes), static_cast<long long>(bare.bytes));
        };
        law("the Assets page's listing (fetchAssetsForAssetView)", w.listing, c.listing);
        // ...and the page's default, its members OFF: the listing plus the V-2 fold's one batched read, whose
        // "is this depender a material?" is answered by idx_assets_material_copies (partial over the material
        // rows, covering), never by the depender's table row. RED before: 190 pages against 142 (48 pages, one
        // per stamped picture's material: spikes/large-library-1/stage2/red-first-members-off.txt).
        law("the Assets page with its members off (libraryList: the listing and the fold)", with.membersOff,
            none.membersOff);
        law("the drawers' counts (countAssetsInCollections)", w.counts, c.counts);
        law("a preset's copies (presetedit copiesOf)", w.copies, c.copies);

        Database::setQueryLog(true);
        db.fetchAssetsForAssetView();
        QString listingSql;
        for (const Database::QueryLogEntry &e : Database::queryLogEntries())
            if (e.name == QLatin1String("FetchAssets")) listingSql = e.sql;
        Database::setQueryLog(false);
        const QString listingPlan = planOf(listingSql, { QString::number(assethome::StoredAssets) });
        // countAssetsInCollections runs its statement raw (the log never sees it): its text, checked below.
        static const char kCountsText[] = "SELECT COUNT(*) FROM assets WHERE listed = 1 AND collection IN (";
        const QString countsPlan = planOf(QString::fromLatin1(kCountsText) + QStringLiteral("?)"), { QString::number(lib.drawer) });
        QFile databaseSource((QString::fromUtf8(JAHSHAKA_TEST_SOURCE_DIR) + QStringLiteral("/src/data/database/database.cpp")));
        CHECK(databaseSource.open(QIODevice::ReadOnly) && databaseSource.readAll().contains(kCountsText),
              "(2) the counts statement planned is database.cpp's own text");
        const QString copiesPlan = planOf(copiesOfSql(), {});
        std::printf("   (2) plans: listing [%s]; counts [%s]; copies [%s]\n", qPrintable(listingPlan), qPrintable(countsPlan),
                    qPrintable(copiesPlan));
        CHECK(listingPlan.contains(QLatin1String("COVERING INDEX idx_assets_library"))
                  && listingPlan.contains(QLatin1String("MEMBER_OF USING COVERING INDEX sqlite_autoindex_assets_1 (guid=?)"))
                  && !listingPlan.contains(QLatin1String("SCAN")),
              "(2) the Assets page's listing is answered by idx_assets_library, its member test by one guid probe a row, no scan");
        CHECK(countsPlan.contains(QLatin1String("COVERING INDEX idx_assets_collection_listed")),
              "(2) the drawers' counts by idx_assets_collection_listed, covering");
        CHECK(copiesPlan.contains(QLatin1String("COVERING INDEX idx_assets_material_copies")),
              "(2) a preset's copies by idx_assets_material_copies, covering");
        // The tray's dependers (fetchProjectDependers, the open project's tray at every listing): asked of the
        // listed textures, one dependee seek each — never the project's every edge, which read `dependencies`
        // whole (no project_guid index): RED before, "SCAN dependencies USING INDEX idx_dependencies_dependee",
        // +98 pages at 10,000 against 1,000 at a project open (spikes/large-library-1/stage2/probe-c0466a549.txt).
        Database::setQueryLog(true);
        assettray::list(&db, lib.openProject, QString(), -1, false);
        QString dependersSql;
        for (const Database::QueryLogEntry &e : Database::queryLogEntries())
            if (e.name == QLatin1String("fetchProjectDependers")) dependersSql = e.sql;
        Database::setQueryLog(false);
        QStringList dependersValues{ QStringLiteral("'%1'").arg(lib.openProject) };
        for (const QString &guid : lib.textures.mid(0, 200)) dependersValues << QStringLiteral("'%1'").arg(guid);
        const QString dependersPlan = planOf(dependersSql, dependersValues);
        std::printf("   (2) the tray's dependers [%s]\n", qPrintable(dependersPlan));
        CHECK(!dependersSql.isEmpty() && dependersPlan.contains(QLatin1String("SEARCH dependencies USING INDEX idx_dependencies_dependee (dependee=?)"))
                  && !dependersPlan.contains(QLatin1String("SCAN")),
              "(2) the tray's dependers are one dependee seek a listed texture, no scan of the project's edges");
        QFile presetEdit((QString::fromUtf8(JAHSHAKA_TEST_SOURCE_DIR) + QStringLiteral("/src/services/presetedit.cpp")));
        const QByteArray presetSource = presetEdit.open(QIODevice::ReadOnly) ? presetEdit.readAll() : QByteArray();
        CHECK(presetSource.contains(kCopiesOfText[0]) && presetSource.contains(kCopiesOfText[1]),
              "(2) the copies statement measured is presetedit.cpp's own text");

        // THE RESIDUE (B): printed with its factor, never asserted (the owner's question).
        const QVector<Reading> r = coldResidue(db, dbPath, lib);
        CHECK(!r.isEmpty() && r.at(0).pages > 2 * residueNone.at(0).pages,
              "POSITIVE CONTROL: a statement that does read the rows behind their thumbnails reads more with them (the "
              "Materials drawer: %lld pages against %lld)", static_cast<long long>(r.value(0).pages),
              static_cast<long long>(residueNone.value(0).pages));
        std::printf("   residue: needs the thumbnail out of the row (the owner's question) — cold pages at 1,000, with / none:\n");
        const QVector<ResidueRow> rows = residueRows();
        for (int i = 0; i < rows.size(); ++i)
            std::printf("   residue | %-78s | %-8s | %5lld / %5lld pages = %.1fx | %s\n", rows.at(i).name, rows.at(i).scale,
                        static_cast<long long>(r.at(i).pages), static_cast<long long>(residueNone.at(i).pages),
                        residueNone.at(i).pages ? double(r.at(i).pages) / double(residueNone.at(i).pages) : 0.0,
                        rows.at(i).where);
        printIndexSizes("1,000 assets");
    }

    // ---- PREVIEWS-1 FIX 1: previews.state READS NO THUMBNAIL CHAIN -------------------------------------------
    // THE LAW: (the pages previews.state reads) - (the pages a header-only scan of the assets table reads) is EXACTLY
    // EQUAL with 256-px thumbnails and with none, on a cold connection whose cache holds the working set. Derivation:
    // the state must know, per row, whether it holds a picture (missing vs stale) and a blob (none), which it reads as
    // length(thumbnail) and length(asset) from the row's HEADER: one pass of the table's b-tree, the same pass the scan
    // makes, and no overflow page. Everything else it reads — idx_assets_library for the platform stamp (covering:
    // guid, properties), thumbnails, asset_files, project_assets, dependencies — the two libraries hold page for page
    // alike (the same calls grew them; they differ ONLY in their thumbnails). One chain walked (a column stored after
    // `thumbnail` read from the table: `properties`, `asset`'s content) breaks the equality. RED before (the platform
    // stamp read as instr(properties) in the main statement): 7,766 against 81
    // (spikes/previews-1/stage2/fix1-pages-red.txt). The default cache's reading is printed beside it (one page more
    // with thumbnails: the larger table pushes one page out of the 2-MB cache and it is read again).
    const PreviewPages previewsWith = coldPreviewPages(db, dbPath, lib.openProject);
    {
        const PreviewPages &w = previewsWith, &n = previewsNone;
        CHECK(w.stateWide.pages - w.scanWide.pages == n.stateWide.pages - n.scanWide.pages,
              "FIX 1: previews.state reads no thumbnail chain — beyond one header-only pass of the assets table it reads "
              "%lld pages with 256-px thumbnails and %lld with none (cache 64 MB)",
              static_cast<long long>(w.stateWide.pages - w.scanWide.pages),
              static_cast<long long>(n.stateWide.pages - n.scanWide.pages));
        CHECK(w.scanWide.pages > n.scanWide.pages,
              "FIX 1 POSITIVE CONTROL: the header-only scan does see the inline thumbnails (%lld pages against %lld)",
              static_cast<long long>(w.scanWide.pages), static_cast<long long>(n.scanWide.pages));
        // PREVIEWS-1 stage 2, previews.counts({scope: 'project'}) (and previews.state's project scope): THE SAME LAW
        // over the open project's rows - its OWN statement (the projectOnly WHERE), unproved by the library clause.
        // Its control is a header-only read of the same rows by the same WHERE: rows that carry an inline tile sit
        // one to a leaf page, so even a header read touches more pages with thumbnails; everything beyond it must be
        // page-for-page alike. A column stored after `thumbnail` read from the table breaks the equality.
        CHECK(w.projectWide.pages - w.projectScanWide.pages == n.projectWide.pages - n.projectScanWide.pages
                  && w.projectWide.pages > 0,
              "previews.counts / state over the open project read no thumbnail chain - beyond a header-only read of the "
              "same rows they read %lld pages with 256-px thumbnails and %lld with none (cache 64 MB)",
              static_cast<long long>(w.projectWide.pages - w.projectScanWide.pages),
              static_cast<long long>(n.projectWide.pages - n.projectScanWide.pages));
        CHECK(w.projectChainWide.pages - w.projectScanWide.pages != n.projectChainWide.pages - n.projectScanWide.pages,
              "...NEGATIVE CONTROL: the same rows reading `properties` from the table break it (%lld pages beyond the "
              "header read with thumbnails, %lld with none)",
              static_cast<long long>(w.projectChainWide.pages - w.projectScanWide.pages),
              static_cast<long long>(n.projectChainWide.pages - n.projectScanWide.pages));
        std::printf("   previews.state at the product's default cache: %lld pages with thumbnails, %lld with none\n",
                    static_cast<long long>(w.state.pages), static_cast<long long>(n.state.pages));
        std::printf("   residue | %-78s | %-8s | %5lld / %5lld pages = %.1fx | %s\n",
                    "previews.state / previews.counts: the assets table's own b-tree (header-only), at 1,000", "library",
                    static_cast<long long>(w.scanWide.pages), static_cast<long long>(n.scanWide.pages),
                    n.scanWide.pages ? double(w.scanWide.pages) / double(n.scanWide.pages) : 0.0,
                    "database.cpp fetchPreviewRows");
        std::fflush(stdout);
        std::printf("   residue (bytes) previews.state's table pass at 1,000: %lld B with thumbnails, %lld B with none\n",
                    static_cast<long long>(w.scanWide.bytes), static_cast<long long>(n.scanWide.bytes));
    }

    // ---- THE LAW: 1,000 / 50 AGAINST 10,000 / 500 ---------------------------------------------------------
    QVector<Count> small;
    for (const Listing &l : listings(db, lib)) small.append(countOf(l.run));
    // THE LAW's NEGATIVE CONTROLS (acceptance 4): two listings made N+1 on purpose — one fetchAsset per listed
    // row, one raw QSqlQuery per listed row — must differ between the sizes by exactly the rows added.
    const auto perRowLogged = [&db] {
        for (const AssetRecord &r : assettray::libraryList(&db, true)) db.fetchAsset(r.guid);
    };
    const auto perRowRaw = [&db] {
        for (const AssetRecord &r : assettray::libraryList(&db, true)) {
            QSqlQuery raw(QSqlDatabase::database());
            raw.prepare(QStringLiteral("SELECT type FROM assets WHERE guid = ?"));
            raw.addBindValue(r.guid);
            raw.exec();
        }
    };
    const int smallRows = int(assettray::libraryList(&db, true).size());
    const Count smallLogged = countOf(perRowLogged), smallRaw = countOf(perRowRaw);
    timer.start();
    CHECK(libraryfixture::grow(&db, store, 9000, 450, lib, {}, &error), "the generator grew the library to 10,000 / 500%s%s",
          error.isEmpty() ? "" : ": ", qPrintable(error));
    std::printf("   grown in %lld ms, %lld thumbnail bytes in all\n", static_cast<long long>(timer.elapsed()),
                static_cast<long long>(lib.thumbnailBytes));
    theGenerator(db, lib, 10000);
    {
        // R5: THE SWEEP'S PLANNING IS FLAT. RED before (meshbakestore.cpp modelBakesNeeded: per model an import
        // record walk and a bake lookup that SCANS asset_files: spikes/large-library-1/stage2/red-first-r5.txt).
        const SweepPlan large = sweepPlan(dbPath, store), perModelLarge = sweepPlan(dbPath, store, lib.objects);
        std::printf("   R5 the sweep's planning on its own connection: %lld statements in %lld ms at 1,000 (%d models), "
                    "%lld in %lld ms at 10,000 (%d models)\n", static_cast<long long>(sweepSmall.statements),
                    static_cast<long long>(sweepSmall.ms), int(lib.objects.size()) / 10, static_cast<long long>(large.statements),
                    static_cast<long long>(large.ms), int(lib.objects.size()));
        CHECK(large.needed == 0, "the bake sweep has nothing to rebuild at 10,000 either (%d)", large.needed);
        // A model's bake by its name, the UI thread's lookup at every model load (MeshBakeStore::planFor): one seek
        // of the partial idx_asset_files_bake, never a read of all asset_files (RED before: "SCAN AF",
        // spikes/large-library-1/stage2/probe-after-r5a.txt: +363 pages at a boot at 10,000).
        const QString bakePlan = planOf(QStringLiteral("SELECT AF.oid, F.ext, F.size FROM asset_files AF LEFT JOIN files F ON "
                                                       "AF.oid = F.oid WHERE AF.role = 'bake' AND AF.name = ? ORDER BY AF.rowid DESC"),
                                        { QStringLiteral("'x.jmb'") });
        QFile bakeSource((QString::fromUtf8(JAHSHAKA_TEST_SOURCE_DIR) + QStringLiteral("/src/services/meshbakestore.cpp")));
        CHECK(bakeSource.open(QIODevice::ReadOnly) && bakeSource.readAll().contains("\"WHERE AF.role = '%1' AND AF.name = ? ORDER BY AF.rowid DESC\")")
                  && bakePlan.contains(QLatin1String("USING INDEX idx_asset_files_bake (name=?)")) && !bakePlan.contains(QLatin1String("SCAN")),
              "R5: a model's bake is found by one seek of idx_asset_files_bake (planFor's own text) [%s]", qPrintable(bakePlan));
        CHECK(sweepSmall.statements > 0 && large.statements == sweepSmall.statements,
              "R5: the bake sweep's planning runs the SAME statements on its own connection at 1,000 as at 10,000 "
              "(%lld and %lld)", static_cast<long long>(sweepSmall.statements), static_cast<long long>(large.statements));
        CHECK(perModelLarge.statements - perModelSmall.statements >= int(lib.objects.size()) * 9 / 10,
              "R5 NEGATIVE CONTROL: the single-row planner once per model grows by at least the %d models added (%lld)",
              int(lib.objects.size()) * 9 / 10,
              static_cast<long long>(perModelLarge.statements - perModelSmall.statements));
    }
    printIndexSizes("10,000 assets");
    {
        // FIX 1 AT 10,000: previews.state is the same statements (a sweep's start and a verb), its table pass printed.
        const PreviewPages big = coldPreviewPages(db, dbPath, lib.openProject);
        CHECK(big.state.statements == previewsWith.state.statements && big.state.statements > 0,
              "FIX 1: previews.state runs the SAME statements at 10,000 as at 1,000 (%lld and %lld)",
              static_cast<long long>(big.state.statements), static_cast<long long>(previewsWith.state.statements));
        CHECK(big.projectWide.statements == previewsWith.projectWide.statements && big.projectWide.statements > 0,
              "previews.counts / state over the open project: the SAME statements at 10,000 as at 1,000 (%lld and %lld)",
              static_cast<long long>(big.projectWide.statements), static_cast<long long>(previewsWith.projectWide.statements));
        // RESIDUE, printed: the platform set is read over the WHOLE idx_assets_library whatever the scope
        // (database.cpp fetchPreviewRows, the FetchPreviewPlatform statement), so a project-scope read grows with the
        // library's index - no thumbnail chain (the clause above), but O(library) index pages.
        std::printf("   residue | %-78s | %-8s | %5lld / %5lld pages = %.1fx | %s\n",
                    "previews.counts / state, scope 'project' (fixed size): pages beyond its rows' header read",
                    "library", static_cast<long long>(big.projectWide.pages - big.projectScanWide.pages),
                    static_cast<long long>(previewsWith.projectWide.pages - previewsWith.projectScanWide.pages),
                    double(big.projectWide.pages - big.projectScanWide.pages)
                        / double(std::max<qint64>(1, previewsWith.projectWide.pages - previewsWith.projectScanWide.pages)),
                    "database.cpp fetchPreviewRows (the platform set: idx_assets_library whole)");
        std::printf("   residue (bytes) previews.counts / state, scope 'project': %lld B at 10,000 and %lld B at 1,000 (the "
                    "whole read, cache 64 MB; the named residue beside (B))\n",
                    static_cast<long long>(big.projectWide.bytes), static_cast<long long>(previewsWith.projectWide.bytes));
        std::fflush(stdout);
        std::printf("   residue (bytes) previews.state's table pass at 10,000 with thumbnails: %lld pages = %lld B (default "
                    "cache: the state reads %lld pages)\n", static_cast<long long>(big.scanWide.pages),
                    static_cast<long long>(big.scanWide.bytes), static_cast<long long>(big.state.pages));
        std::fflush(stdout);
    }
    const QVector<Listing> all = listings(db, lib);
    {
        const qint64 added = int(assettray::libraryList(&db, true).size()) - smallRows;
        const Count largeLogged = countOf(perRowLogged), largeRaw = countOf(perRowRaw);
        CHECK(largeLogged.traced - smallLogged.traced == added && largeLogged.logged - smallLogged.logged == added,
              "NEGATIVE CONTROL at the law: a fetchAsset per listed row grows traced by %lld and logged by %d (%lld rows added)",
              static_cast<long long>(largeLogged.traced - smallLogged.traced), largeLogged.logged - smallLogged.logged,
              static_cast<long long>(added));
        CHECK(largeRaw.traced - smallRaw.traced == added && largeRaw.logged == smallRaw.logged,
              "NEGATIVE CONTROL at the law: a raw query per listed row grows traced by %lld and logged by %d",
              static_cast<long long>(largeRaw.traced - smallRaw.traced), largeRaw.logged - smallRaw.logged);
    }
    for (int i = 0; i < all.size(); ++i) {
        const Count large = countOf(all.at(i).run);
        const Count &one = small.at(i);
        CHECK(large.traced == one.traced,
              "%s: %lld traced statements at 1,000 / 50 and %lld at 10,000 / 500 (logged %d and %d)", all.at(i).name,
              static_cast<long long>(one.traced), static_cast<long long>(large.traced), one.logged, large.logged);
        CHECK(one.thumbnailSelects == 0 && large.thumbnailSelects == 0,
              "%s: no statement selects a thumbnail (%d at 1,000, %d at 10,000; tile reads %d and %d, counted apart)",
              all.at(i).name, one.thumbnailSelects, large.thumbnailSelects, one.tileReads, large.tileReads);
    }

    theRule(db);

    // ---- 1b (3): THE WRITE COST, and the first open of a 10,000-asset library that must build the indexes ----
    printWriteCost(db, store, lib, "this build's indexes");
    previousIndexSet();
    printWriteCost(db, store, lib, "the previous index set");
    db.closeDatabase();
    {
        // THE ONE-TIME COST, printed, never barred: the boot's gate over this library (warm: the file was just
        // written; a cold first read is its bytes from the disk). Each statement its own transaction: the write
        // lock is released between them, and no window exists while it runs.
        const int statements = Database::indexStatementsToRun(dbPath);
        QElapsedTimer build;
        build.start();
        QString why;
        const bool ok = Database::prepareIndexes(dbPath, &why);
        std::printf("   (3) the boot's one-time index build of a %d-asset library with the previous index set: %d statements "
                    "in %lld ms (warm)\n", lib.assets, statements, static_cast<long long>(build.elapsed()));
        CHECK(ok && statements == 4 && Database::indexStatementsToRun(dbPath) == 0,
              "(3) ...the three indexes exist after it, the retired one gone%s%s", why.isEmpty() ? "" : ": ", qPrintable(why));
        db.initializeDatabase(dbPath);
        const int atOpen = indexStatementsAtOpen(db);
        CHECK(atOpen == 0, "(3) ...and the shell's open of the prepared 10,000-asset library runs 0 index statements (%d)",
              atOpen);
    }

    theOldLibraryKeeps();
    theBootGate();

    db.closeDatabase();
    QFile::remove(dbPath);
    clearStore();
    if (failures) std::printf("library.statements: FAILED (%d)\n", failures);
    else std::printf("library.statements: PASSED\n");
    return failures ? 1 : 0;
}
