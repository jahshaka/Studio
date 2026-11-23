/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// db.layer_guard (NOQT-DATABASE-1 stage 3, the lead's ruling) — A FAILED READ NEVER DECIDES A WRITE UNSEEN: the
// layer's guard in db::Connection (connection.h, THE LAYER GUARD), on a throwaway file of its own. The failure is
// test-side only: an authorizer denying a table's reads at prepare (tests/db/dbseam.h).
//   1. CONTROL: "no row" is an answer - it marks nothing, the next write goes;
//   2. a failed read NOBODY LOOKED AT refuses the next write BY NAME (its statement, SQLite's text); a bare
//      autocommit write consumes it: ONE refusal, the write after it goes;
//   3. LOOKED AT (its Error tested) - the write goes;
//   4. a DIRECT connection write (exec) is refused too;
//   5. inside a UNIT: the unit fails whole - the write before the refusal is rolled back, every later write refused,
//      its commit answers false; the mark ends with the unit;
//   6. a unit opened under a standing mark is refused at its own BEGIN (a write) and writes nothing;
//   7. run() on a failed read is looking (its answer cannot be dropped);
//   8. an unlooked mark is never replaced: looking at a NEWER failure leaves the older one standing;
//   9. row 136 (the sweep's materialsapi.cpp:1799) in a SCRIPT RUN's gesture batch: a one-argument fetchAsset that
//      failed, then deleteDependency and createDependency: BOTH refused, no second edge; the batch's earlier write
//      kept; the next batch clean;
//  10. a PENDING library (refuseWrites): every write refused by name, never consumed; a read still answers; another
//      connection to the file - the next start's retry - writes;
//  11. a unit inside a script run does not end the run's hold: the run's later bare write is refused too;
//  12. a unit that only read (failed, unlooked) and committed leaves the mark: the write after it is refused;
//  13. a write that failed to prepare is no read: it marks nothing.

#include <QApplication>
#include <QFile>

#include <cstdio>
#include <string>

#include "data/database/database.h"
#include "data/db/connection.h"
#include "data/db/statement.h"
#include "data/db/transaction.h"
#include "tests/db/dbseam.h"

namespace {

int failures = 0;

void check(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
    std::fflush(stdout);
    if (!ok) ++failures;
}

bool contains(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }

const char *kFile = "db_layer_guard_test.db";
const char *kLibrary = "db_layer_guard_library.db";

void removeFiles()
{
    for (const char *file : { kFile, kLibrary })
        for (const char *suffix : { "", "-wal", "-shm", "-journal" }) QFile::remove(QString::fromLatin1(file) + suffix);
}

db::Connection openNamed(const char *file, const std::string &name)
{
    db::Options o;
    o.name = name;
    o.wal = true;
    db::Error e;
    return db::Connection::open(file, o, &e);
}

/// A write on `c`; its error in `e`.
bool insert(db::Connection &c, const std::string &value, db::Error *e)
{
    db::Statement s(c, "INSERT INTO t(v) VALUES (?)");
    s.bindText(1, value);
    return s.run(e);
}

int countOf(db::Connection &c, const std::string &value)
{
    db::Statement s(c, "SELECT count(*) FROM t WHERE v = ?");
    s.bindText(1, value);
    db::Error e;
    return s.step(&e) ? int(s.int64(0)) : -1;
}

/// A read of t made to FAIL (denied at its prepare) and NOT looked at: its Error is never tested.
void failedReadUnlooked(db::Connection &c, const char *sql = "SELECT v FROM t")
{
    dbseam::denyReadsOf(c, "t");
    db::Statement s(c, sql);
    db::Error e;
    s.step(&e);
    dbseam::clear(c);
}

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // database.cpp links QtWidgets
    removeFiles();
    db::Connection c = openNamed(kFile, "guarded");
    db::Error e;
    check(c.isOpen() && c.exec("CREATE TABLE t (k INTEGER PRIMARY KEY, v TEXT)", &e), "CONTROL: the file and its table");

    // 1. "NO ROW" MARKS NOTHING.
    {
        db::Statement none(c, "SELECT v FROM t WHERE v = 'none'");
        none.step(&e);
        check(!c.marked() && insert(c, "after-no-row", &e), "1: CONTROL: a read that answered no row marks nothing; the "
                                                             "next write goes");
    }

    // 2. UNLOOKED: THE NEXT WRITE REFUSED BY NAME, ONE REFUSAL (a bare autocommit write consumes it).
    {
        failedReadUnlooked(c);
        check(c.marked(), "2: a failed read nobody looked at marks the connection");
        db::Error refused;
        const bool wrote = insert(c, "refused-2", &refused);
        check(!wrote && contains(refused.message, "write refused: an earlier read failed and nobody looked")
                  && contains(refused.message, "SELECT v FROM t") && contains(refused.message, "is prohibited")
                  && countOf(c, "refused-2") == 0,
              "2: the next write is REFUSED BY NAME (the read's statement, SQLite's text): " + refused.message);
        check(!c.marked() && insert(c, "after-2", &e) && countOf(c, "after-2") == 1,
              "2: a bare autocommit write CONSUMED the mark: the write after it goes");
    }

    // 3. LOOKED AT: THE WRITE GOES.
    {
        dbseam::denyReadsOf(c, "t");
        db::Statement s(c, "SELECT v FROM t");
        db::Error failed;
        const bool row = s.step(&failed);
        dbseam::clear(c);
        const bool looked = !row && static_cast<bool>(failed);   // THE LOOK: the caller tested the failure
        check(looked && !c.marked() && insert(c, "looked-3", &e) && countOf(c, "looked-3") == 1,
              "3: a failed read LOOKED AT (its error tested) marks nothing that stands: the write goes");
    }

    // 4. A DIRECT CONNECTION WRITE IS REFUSED TOO.
    {
        failedReadUnlooked(c);
        db::Error refused;
        const bool wrote = c.exec("INSERT INTO t(v) VALUES ('direct-4')", &refused);
        check(!wrote && contains(refused.message, "write refused") && countOf(c, "direct-4") == 0,
              "4: an exec() write is refused by the same guard: " + refused.message);
    }

    // 5. INSIDE A UNIT: IT FAILS WHOLE; THE MARK ENDS WITH IT.
    {
        bool committed = true;
        db::Error commitError;
        {
            db::Transaction unit(c, "the test's unit");
            insert(c, "before-5", &e);
            failedReadUnlooked(c);
            db::Error refused;
            const bool b = insert(c, "refused-5", &refused);
            const bool later = insert(c, "later-5", &e);
            check(!b && !later, "5: inside a unit every write after the unlooked failure is refused (the mark held)");
            committed = unit.commit(&commitError);
        }
        check(!committed && contains(commitError.message, "rolled back") && countOf(c, "before-5") == 0,
              "5: the unit FAILS WHOLE - its commit answers false and its earlier write is rolled back: "
                  + commitError.message);
        check(!c.marked() && insert(c, "after-5", &e), "5: the mark ended with the unit: the next write goes");
    }

    // 6. A UNIT OPENED UNDER A STANDING MARK IS REFUSED AT ITS BEGIN.
    {
        failedReadUnlooked(c);
        bool committed = true;
        {
            db::Transaction unit(c, "opened under a mark");
            check(contains(unit.beginError().message, "write refused"),
                  "6: the BEGIN IMMEDIATE is a write: refused by name: " + unit.beginError().message);
            insert(c, "inside-6", &e);
            committed = unit.commit(&e);
        }
        check(!committed && countOf(c, "inside-6") == 0 && insert(c, "after-6", &e),
              "6: it writes nothing, its commit answers false, and the next write goes");
    }

    // 7. run() ON A FAILED READ IS LOOKING.
    {
        dbseam::denyReadsOf(c, "t");
        bool ran = true;
        {
            db::Statement s(c, "SELECT v FROM t");
            db::Error failed;
            ran = s.run(&failed);
        }
        dbseam::clear(c);
        check(!ran && !c.marked() && insert(c, "after-7", &e), "7: a read through run() answered false: looked at");
    }

    // 8. AN UNLOOKED MARK IS NEVER REPLACED.
    {
        failedReadUnlooked(c, "SELECT v FROM t WHERE k = 1");
        dbseam::denyReadsOf(c, "t");
        {
            db::Statement newer(c, "SELECT count(*) FROM t");
            db::Error failed;
            if (!newer.step(&failed) && failed) {}   // the NEWER failure looked at
        }
        dbseam::clear(c);
        db::Error refused;
        const bool wrote = insert(c, "refused-8", &refused);
        check(!wrote && contains(refused.message, "WHERE k = 1"),
              "8: looking at a newer failure leaves the older unlooked one standing (it names the older): "
                  + refused.message);
    }

    // 9. ROW 136 IN A SCRIPT RUN's GESTURE BATCH: BOTH WRITES REFUSED, NO SECOND EDGE; THE NEXT BATCH CLEAN.
    {
        Database database;
        check(database.initializeDatabase(QString::fromLatin1(kLibrary)), "9: CONTROL: the library opens");
        database.createAllTables();
        const auto edges = [&database] {
            db::Statement s(database.connection(), "SELECT count(*) FROM dependencies WHERE depender = 'node9' "
                                                   "AND dependee = 'tex9'");
            db::Error se;
            return s.step(&se) ? int(s.int64(0)) : -1;
        };
        database.createDependency(1, 2, "node9", "tex9", QString());
        check(edges() == 1, "9: CONTROL: the node's one edge to its texture");
        database.beginBatch();
        const bool before = database.createFolder(QStringLiteral("kept-9"), QString(), QStringLiteral("k9"), QString());
        dbseam::denyReadsOf(database.connection(), "assets");
        const AssetRecord row = database.fetchAsset(QStringLiteral("tex9"));   // ONE-ARGUMENT: nobody looks
        dbseam::clear(database.connection());
        const bool deleted = database.deleteDependency(QStringLiteral("node9"), QStringLiteral("tex9"));
        const bool created = database.createDependency(1, 2, "node9", "tex9", QString());
        check(before && row.guid.isEmpty() && !deleted && !created && edges() == 1,
              "9: after the unlooked failed read BOTH writes of the gesture are refused - no second edge (delete "
              + std::to_string(deleted) + ", create " + std::to_string(created) + ", edges " + std::to_string(edges())
              + ")");
        database.endBatch();
        db::Statement kept(database.connection(), "SELECT count(*) FROM folders WHERE guid = 'k9'");
        db::Error ke;
        check(kept.step(&ke) && kept.int64(0) == 1, "9: the batch's earlier write is kept (a scope, never one unit)");
        kept.reset();
        database.beginBatch();
        const bool clean = database.createDependency(1, 2, "node9", "tex9b", QString());
        database.endBatch();
        check(clean, "9: the NEXT batch is clean: the mark ended with the run");

        // 11. A UNIT INSIDE THE RUN DOES NOT END THE RUN's HOLD (the reader's FIX 1a): a failed read, then a
        //     DbTransaction (it stands the batch down and is refused at its BEGIN), then a bare write of the same run:
        //     REFUSED; the run's end clears it.
        database.beginBatch();
        dbseam::denyReadsOf(database.connection(), "assets");
        database.fetchAsset(QStringLiteral("tex9"));   // unlooked
        dbseam::clear(database.connection());
        bool unitWrote = true;
        {
            DbTransaction unit(database.connection());
            unitWrote = database.createFolder(QStringLiteral("unit-11"), QString(), QStringLiteral("u11"), QString())
                        && unit.commit();
        }
        const bool afterUnit = database.createFolder(QStringLiteral("after-11"), QString(), QStringLiteral("a11"), QString());
        database.endBatch();
        check(!unitWrote && !afterUnit, "11: in a run, the unit after an unlooked failed read is refused AND the run's "
                                        "later bare write is refused too (unit " + std::to_string(unitWrote) + ", after "
                                        + std::to_string(afterUnit) + ")");
        check(database.createFolder(QStringLiteral("next-11"), QString(), QStringLiteral("n11"), QString()),
              "11: the run's end cleared it: the next write goes");
        database.closeDatabase();
    }

    // 12. A UNIT THAT ONLY READ (and failed, unlooked) AND COMMITTED DOES NOT CLEAR THE MARK (the reader's FIX 1b): the
    //     caller acts on that read after the unit - its write is refused.
    {
        bool committed = false;
        {
            db::Transaction unit(c, "a unit that only reads");
            failedReadUnlooked(c);
            committed = unit.commit(&e);
        }
        db::Error refused;
        const bool wrote = insert(c, "after-12", &refused);
        check(committed && !wrote && contains(refused.message, "write refused"),
              "12: a unit that read-failed and committed leaves the mark: the write after it is refused: " + refused.message);
    }

    // 13. A WRITE THAT FAILS TO PREPARE IS NO READ (the reader's CHEAP item): it marks nothing - the next, unrelated
    //     write goes (classified by its first keyword, nothing prepared to ask).
    {
        db::Statement broken(c, "INSERT INTO nowhere VALUES (1)");
        broken.step(&e);   // not tested: no look
        check(!c.marked() && insert(c, "after-13", &e), "13: a write that failed to prepare marks nothing: the next write goes");
    }

    // 10. A PENDING LIBRARY: EVERY WRITE REFUSED BY NAME, NEVER CONSUMED; THE NEXT START's CONNECTION WRITES.
    {
        c.refuseWrites("injected: the reset did not finish");
        db::Error first, second;
        const bool w1 = insert(c, "pending-1", &first);
        const bool w2 = insert(c, "pending-2", &second);
        check(!w1 && !w2 && contains(first.message, "pending a reset") && contains(second.message, "injected"),
              "10: every write is refused by name, the refusal never consumed: " + second.message);
        check(countOf(c, "after-2") == 1, "10: CONTROL: a read still answers");
        db::Connection retry = openNamed(kFile, "the next start");
        check(insert(retry, "retry-10", &e) && countOf(retry, "retry-10") == 1,
              "10: another connection to the file (the next start's retry) writes");
    }

    c = db::Connection();
    removeFiles();
    if (failures) {
        std::printf("db.layer_guard: FAILED (%d)\n", failures);
        return 1;
    }
    std::printf("db.layer_guard: PASSED\n");
    return 0;
}
