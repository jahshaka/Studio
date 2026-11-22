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
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <cstdio>
#include <functional>

#include "data/database/database.h"
#include "data/database/databasereads.h"
#include "services/assettray.h"
#include "services/assetcas.h"
#include "services/memberstamp.h"
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
    theGenerator(db, lib, 1000);

    // ---- M1: THE TWO NUMBERS ----------------------------------------------------------------------------
    const ColdListing with = coldListing(db, dbPath);
    printM1("with 256-px thumbnails", with, lib.thumbnailBytes);
    printM1("with none             ", none, 0);
    CHECK(with.membersOn.statements == none.membersOn.statements,
          "M1: the listing's scan runs the same statements with and without thumbnails (%lld)",
          static_cast<long long>(with.membersOn.statements));

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

    db.closeDatabase();
    QFile::remove(dbPath);
    clearStore();
    if (failures) std::printf("library.statements: FAILED (%d)\n", failures);
    else std::printf("library.statements: PASSED\n");
    return failures ? 1 : 0;
}
