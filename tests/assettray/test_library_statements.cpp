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
//
// THE STATED LIMIT (ruled call 3): only the default connection is traced; a per-asset query moved onto
// a worker connection would not be seen here.
// Headless (offscreen); never touches the user's JahLibrary.db. The row's ENVIRONMENT sets
// JAH_DB_STATEMENT_TRACE=1 (tests/assettray/CMakeLists.txt).

#include <QApplication>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <cstdio>

#include "data/database/database.h"
#include "data/database/databasereads.h"

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
    if (failures) std::printf("library.statements: FAILED (%d)\n", failures);
    else std::printf("library.statements: PASSED\n");
    return failures ? 1 : 0;
}
