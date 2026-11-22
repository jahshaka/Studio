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

#include "data/database/database.h"
#include "data/database/databasereads.h"
#include "services/assettray.h"
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

    // ---- THE LIBRARY AT 1,000 ASSETS / 50 PROJECTS ----------------------------------------------------
    const QString store = QDir::currentPath() + QStringLiteral("/library_statements_store");
    QDir(store).removeRecursively();
    QDir(store + QStringLiteral("-sources")).removeRecursively();
    libraryfixture::Library lib;
    QString error;
    QElapsedTimer timer;
    timer.start();
    CHECK(libraryfixture::grow(&db, store, 1000, 50, lib, {}, &error), "the generator wrote 1,000 assets and 50 projects%s%s",
          error.isEmpty() ? "" : ": ", qPrintable(error));
    std::printf("   generated in %lld ms, %lld thumbnail bytes\n", static_cast<long long>(timer.elapsed()),
                static_cast<long long>(lib.thumbnailBytes));
    theGenerator(db, lib, 1000);

    db.closeDatabase();
    QFile::remove(dbPath);
    QDir(store).removeRecursively();
    QDir(store + QStringLiteral("-sources")).removeRecursively();
    if (failures) std::printf("library.statements: FAILED (%d)\n", failures);
    else std::printf("library.statements: PASSED\n");
    return failures ? 1 : 0;
}
