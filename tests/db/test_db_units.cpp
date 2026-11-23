/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// db.units (NOQT-DATABASE-1 stage 2, the transaction core) — the REAL Database's units of writes on
// the std-only layer, on a throwaway library of its own:
//   1. where the layer cannot borrow the driver's SQLite the library does NOT open: a named error;
//   2. a unit whose BEGIN fails REFUSES its writes: the funnel refuses them unexecuted, the BEGIN's
//      warning names the lock's holder, its commit answers false; the next scope writes again;
//   3. the gesture batch STANDS DOWN for a unit on its connection (the batch-commit listener fires
//      once, the batch's row is committed before the unit's), and while it holds the lock another
//      connection's busy error names it;
//   4. a unit never committed leaves the library's logical content exactly as it was (a dump of
//      every table's rows compared), beside a committed unit that changes it;
//   5. a process killed between two statements of a unit leaves the library whole and the unit
//      absent, beside a killed process whose unit committed first;
//   6. the funnel's cursor: a read that fails on its SECOND step is FAILED and said by name, never a short result;
//   7. a cursor's step after the transaction it was opened in has ended is refused by name (FAILED);
//   8. an import's archive that cannot be opened read-only is refused by name before any read, and never created;
//   9. ...nor a step inside the NEXT unit at the same depth (the unit's serial, not its depth: the audit's W1);
//  10. an import whose insert failed answers false and leaves nothing (its unit rolls back: the audit's W3);
//  11. "is it pinned here" that could not be read (a failed probe, a closed library) is no answer: readOk cleared;
//  12. an import's archive whose PROJECT ROW cannot be read is refused by name - never an empty, unnamed world;
//  13. "does it have dependencies" that could not be read is no answer: readOk cleared;
//  14. where a project's pins are filed, unread, is no answer: readOk cleared;
//  15. the boot's schema check over live columns it could not read answers "cannot judge" (matches), never "differs";
//  16. the present indexes unread build nothing: the boot's gate answers -1 and REFUSES the boot by name, the open's
//      report reads -1;
//  17. a tile read that failed, or has no library, answers no answer (unread), never an empty one ("no picture"); a
//      failure streak is said once, and ended by its count;
//  18. the preview rows read short (a later row's step failed) answer no rows, never a cut list.

#include <QApplication>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlDriver>

#include <sqlite3.h>

#include <csignal>
#include <cstring>
#include <cstdio>
#include <optional>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "data/database/database.h"
#include "data/db/connection.h"
#include "data/db/statement.h"
#include "data/db/transaction.h"
#include "services/jahlog.h"
#include "tests/db/dbseam.h"

namespace {

int failures = 0;
QStringList gWarnings;

void check(bool ok, const QString &what)
{
    std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", qUtf8Printable(what));
    std::fflush(stdout);
    if (!ok) ++failures;
}

void collect(QtMsgType type, const QMessageLogContext &, const QString &text)
{
    if (type >= QtWarningMsg) gWarnings << text;
}

const char *kFile = "db_units_test.db";

void removeFile()
{
    for (const char *suffix : { "", "-wal", "-shm", "-journal" }) QFile::remove(QString::fromLatin1(kFile) + suffix);
}

db::Connection openSide(const std::string &name)
{
    db::Options o;
    o.name = name;
    o.wal = true;
    db::Error e;
    db::Connection c = db::Connection::open(kFile, o, &e);
    check(c.isOpen(), QStringLiteral("a side connection %1 opens %2").arg(QString::fromStdString(name),
                                                                        QString::fromStdString(e.message)));
    return c;
}

int folders(db::Connection &c, const QString &name)
{
    db::Statement s(c, "SELECT count(*) FROM folders WHERE name = ?");
    const QByteArray utf8 = name.toUtf8();
    s.bindText(1, std::string_view(utf8.constData(), std::size_t(utf8.size())));
    db::Error e;
    return s.step(&e) ? int(s.int64(0)) : -1;
}

/// Every table's rows as text, in rowid order: the library's logical content.
std::string dump(db::Connection &c)
{
    std::string out;
    std::vector<std::string> tables;
    {
        db::Statement s(c, "SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name");
        db::Error e;
        while (s.step(&e)) tables.push_back(s.text(0));
    }
    for (const std::string &table : tables) {
        std::string columns;
        {
            db::Statement s(c, "SELECT name FROM pragma_table_info(?) ORDER BY cid");
            s.bindText(1, table);
            db::Error e;
            while (s.step(&e)) columns += (columns.empty() ? "" : " || '|' || ") + std::string("quote(\"") + s.text(0) + "\")";
        }
        if (columns.empty()) continue;
        db::Statement rows(c, "SELECT " + columns + " FROM \"" + table + "\" ORDER BY rowid");
        db::Error e;
        out += "[" + table + "]\n";
        while (rows.step(&e)) out += rows.text(0) + "\n";
    }
    return out;
}

/// THE CHILD (case 5): opens the library, begins a unit, writes one folder, and is KILLED - before its
/// second statement, or after its commit.
int child(const char *file, bool commitFirst)
{
    Database database;
    if (!database.initializeDatabase(QString::fromLocal8Bit(file))) return 3;
    DbTransaction tx(database.connection());
    if (commitFirst) {
        database.createFolder(QStringLiteral("done-1"), QString(), QStringLiteral("done1"), QString());
        database.createFolder(QStringLiteral("done-2"), QString(), QStringLiteral("done2"), QString());
        if (!tx.commit()) return 4;
    } else {
        database.createFolder(QStringLiteral("mid-1"), QString(), QStringLiteral("mid1"), QString());
    }
    std::raise(SIGKILL);   // no destructor, no rollback: the process dies mid-unit
    return 5;
}

void killedChild(const char *self, bool commitFirst)
{
    const pid_t pid = fork();
    if (pid == 0) {
        execl(self, self, "--child", kFile, commitFirst ? "1" : "0", static_cast<char *>(nullptr));
        _exit(6);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    check(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
          QStringLiteral("the child (%1) was killed by SIGKILL").arg(commitFirst ? "committed first" : "mid-unit"));
}

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // database.cpp links QtWidgets
    if (argc == 4 && std::string(argv[1]) == "--child") return child(argv[2], std::string(argv[3]) == "1");

    removeFile();
    qInstallMessageHandler(collect);
    Database database;
    check(database.initializeDatabase(QString::fromLatin1(kFile)), QStringLiteral("CONTROL: the library opens (the borrow holds)"));
    check(database.connection().isOpen(), QStringLiteral("CONTROL: connection() is the borrowed handle"));
    database.createAllTables();

    // 1. NO BORROW, NO LIBRARY.
    {
        Database other;
        QString error;
        const bool borrowed = other.borrowConnection(QVariant(), &error);
        check(!borrowed && error.contains(QStringLiteral("this build cannot open the library until NOQT-DATABASE-1 stage 3")),
              QStringLiteral("a handle the layer cannot borrow is a named refusal: ") + error);
    }

    // 2. A UNIT WHOSE BEGIN FAILS REFUSES ITS WRITES.
    {
        db::Error e;
        database.connection().exec("PRAGMA busy_timeout = 50", &e);
        db::Connection holder = openSide("holder");
        db::Transaction hold(holder, "the test's hold");
        holder.exec("INSERT INTO folders (name, guid) VALUES ('held', 'h')", &e);
        gWarnings.clear();
        {
            DbTransaction tx(database.connection());
            const QString said = gWarnings.join(QLatin1Char('\n'));
            check(said.contains(QStringLiteral("could NOT BEGIN")) && said.contains(QStringLiteral("holder (the test's hold)"))
                      && said.contains(QStringLiteral("its first write: INSERT INTO folders")),
                  QStringLiteral("the failed BEGIN is said once, naming the lock's holder: ") + said);
            hold.rollback();   // the lock is free: only the refusal can stop the write now
            gWarnings.clear();
            const bool wrote = database.createFolder(QStringLiteral("refused"), QString(), QStringLiteral("r"), QString());
            check(!wrote && folders(database.connection(), QStringLiteral("refused")) == 0,
                  QStringLiteral("the unit's write is REFUSED"));
            check(gWarnings.filter(QStringLiteral("failed to execute")).isEmpty(),
                  QStringLiteral("...by the funnel, UNEXECUTED (no statement failed in SQLite): ") + gWarnings.join(QLatin1Char('\n')));
            check(!tx.commit(), QStringLiteral("its commit answers false"));
        }
        const bool after = database.createFolder(QStringLiteral("after"), QString(), QStringLiteral("a"), QString());
        check(after && folders(database.connection(), QStringLiteral("after")) == 1,
              QStringLiteral("CONTROL: the refusal ends with its scope: the next write lands"));
        database.connection().exec("PRAGMA busy_timeout = 5000", &e);
    }

    // 3. THE BATCH STANDS DOWN FOR A UNIT.
    {
        int okCommits = 0;
        Database::setBatchCommitListener([&okCommits](bool ok) { okCommits += ok; });
        db::Connection side = openSide("reader");
        database.beginBatch();
        database.createFolder(QStringLiteral("batched"), QString(), QStringLiteral("b"), QString());
        check(database.batchTransactionLive(), QStringLiteral("CONTROL: the batch began at its first write"));
        {
            db::Error busy;
            side.exec("INSERT INTO folders (name, guid) VALUES ('side', 's')", &busy);
            // (its first write is named once the funnel's statements run on the layer: today they are QtSql's)
            check(busy.busy() && busy.holder.find("editor (the editor's gesture batch)") != std::string::npos,
                  QStringLiteral("another connection meets the batch's lock: busy, the batch named: ")
                      + QString::fromStdString(busy.holder));
        }
        check(folders(side, QStringLiteral("batched")) == 0, QStringLiteral("CONTROL: the batch's row is not committed yet"));
        {
            DbTransaction tx(database.connection());
            check(okCommits == 1 && !database.batchTransactionLive(),
                  QStringLiteral("a unit makes the batch commit and stand down (the listener fired %1 time)").arg(okCommits));
            check(folders(side, QStringLiteral("batched")) == 1, QStringLiteral("...its row committed BEFORE the unit's"));
            database.createFolder(QStringLiteral("unit"), QString(), QStringLiteral("u"), QString());
            check(tx.commit(), QStringLiteral("the unit commits"));
        }
        check(folders(side, QStringLiteral("unit")) == 1, QStringLiteral("CONTROL: the unit's row is committed"));
        check(database.endBatch(), QStringLiteral("the batch's scope ends balanced"));
        Database::setBatchCommitListener(nullptr);
    }

    // 4. A UNIT NEVER COMMITTED LEAVES THE LIBRARY AS IT WAS.
    {
        db::Connection side = openSide("dump");
        const std::string before = dump(side);
        {
            DbTransaction tx(database.connection());
            database.createFolder(QStringLiteral("dropped-1"), QString(), QStringLiteral("d1"), QString());
            database.renameFolder(QStringLiteral("a"), QStringLiteral("renamed"));
        }
        const std::string after = dump(side);
        check(!before.empty() && after == before,
              QStringLiteral("a dropped unit leaves the logical content byte-equal (%1 bytes of rows)").arg(before.size()));
        {
            DbTransaction tx(database.connection());
            database.createFolder(QStringLiteral("kept"), QString(), QStringLiteral("k"), QString());
            tx.commit();
        }
        check(dump(side) != before, QStringLiteral("CONTROL: a committed unit changes the dump"));
    }

    // 5. A PROCESS KILLED MID-UNIT.
    {
        killedChild(argv[0], false);
        killedChild(argv[0], true);
        db::Connection side = openSide("after the kill");
        db::Statement s(side, "PRAGMA integrity_check");
        db::Error e;
        const bool got = s.step(&e);
        check(got && s.text(0) == "ok", QStringLiteral("the library after the kill is whole: ") + QString::fromStdString(s.text(0)));
        check(folders(side, QStringLiteral("done-1")) == 1 && folders(side, QStringLiteral("done-2")) == 1,
              QStringLiteral("CONTROL: the unit committed before the kill is present (both rows)"));
        check(folders(side, QStringLiteral("mid-1")) == 0, QStringLiteral("the unit killed between its statements is ABSENT"));
    }

    // 6. THE FUNNEL's CURSOR: A READ THAT FAILS ON ITS SECOND STEP IS FAILED, NEVER A SHORT RESULT (the lead's
    //    ruling A, condition 2). abs() of the smallest integer raises "integer overflow" when SQLite reaches row 2.
    {
        db::Error e;
        database.connection().exec("CREATE TEMP TABLE steps(v INTEGER); INSERT INTO steps VALUES (1), (2)", &e);
        {
            DbQuery whole(database.connection(), "SELECT v FROM steps ORDER BY v");
            int rows = 0;
            if (database.executeAndCheckQuery(whole, QStringLiteral("unitsWhole")))
                while (whole.next()) ++rows;
            check(rows == 2 && !whole.failed(), QStringLiteral("CONTROL: the same rows read whole: %1 rows, not failed").arg(rows));
        }
        const quint64 mark = JahLog::mark(QStringLiteral("db.units case 6"));
        DbQuery broken(database.connection(),
                       "SELECT v, CASE WHEN v = 2 THEN abs(-9223372036854775807 - 1) ELSE v END FROM steps");   // rowid order: one row a step (an ORDER BY would sort, and so compute, every row first)
        const bool ran = database.executeAndCheckQuery(broken, QStringLiteral("unitsSecondStep"));
        int rows = 0;
        while (broken.next()) ++rows;
        check(ran && rows == 1, QStringLiteral("CONTROL: the first step ran and handed ONE row (%1) before the failure").arg(rows));
        check(broken.failed() && QString::fromStdString(broken.error().message).contains(QStringLiteral("overflow")),
              QStringLiteral("a read that fails on its SECOND step is FAILED, never a short result: failed %1, '%2'")
                  .arg(broken.failed()).arg(QString::fromStdString(broken.error().message)));
        check(JahLog::since(mark).filter(QStringLiteral("unitsSecondStep query failed to execute")).size() == 1,
              QStringLiteral("...and the failure is said ONCE, by the statement's name"));
        check(!broken.next() && broken.failed(), QStringLiteral("a failed cursor stays failed and hands no more rows"));
    }

    // 7. THE CURSOR CANNOT OUTLIVE THE TRANSACTION IT WAS OPENED IN (condition 3): its next step after that unit
    //    ended is REFUSED by name and the cursor is FAILED.
    {
        std::optional<db::Transaction> unit;
        unit.emplace(database.connection(), "the test's unit");
        DbQuery inside(database.connection(), "SELECT v FROM steps ORDER BY v");
        database.executeAndCheckQuery(inside, QStringLiteral("unitsInside"));
        const bool first = inside.next();
        check(first && inside.int64(0) == 1, QStringLiteral("CONTROL: inside its unit the cursor reads its first row"));
        db::Error e;
        unit->commit(&e);
        unit.reset();
        const bool after = inside.next();
        check(!after && inside.failed()
                  && QString::fromStdString(inside.error().message).contains(QStringLiteral("outlived the transaction")),
              QStringLiteral("a step after its unit ended is refused by name: '%1'")
                  .arg(QString::fromStdString(inside.error().message)));
    }

    // 8. AN ARCHIVE THAT CANNOT BE OPENED READ-ONLY IS REFUSED BY NAME BEFORE ANY READ (the import's archive
    //    connection is the layer's from stage 2; a read-write open used to CREATE a missing one, empty).
    {
        const QString missing = QStringLiteral("db_units_missing_archive");
        QFile::remove(missing + QStringLiteral(".db"));
        const quint64 mark = JahLog::mark(QStringLiteral("db.units case 8"));
        QString world;
        QMap<QString, QString> guids;
        const bool imported = database.importProject(missing, QStringLiteral("units-import"), world, guids);
        const QStringList said = JahLog::since(mark).filter(QStringLiteral("Couldn't open the import database!"));
        check(!imported && said.size() == 1 && said.first().contains(QStringLiteral("ProjectImport")),
              QStringLiteral("a missing archive is refused by name before any read: %1")
                  .arg(said.isEmpty() ? QStringLiteral("(nothing said)") : said.first()));
        check(!QFile::exists(missing + QStringLiteral(".db")), QStringLiteral("...and no archive file was created"));
        check(JahLog::since(mark).filter(QStringLiteral("query failed to execute")).isEmpty(),
              QStringLiteral("...and no statement ran on it"));
    }

    // 9. ...AND NOT INTO THE NEXT ONE AT THE SAME DEPTH (the audit's W1): a cursor opened inside a unit that ended,
    //    stepped after a NEW unit opened on the connection (the depth back where it was), is refused by name.
    {
        std::optional<db::Transaction> unit;
        unit.emplace(database.connection(), "the test's first unit");
        DbQuery inside(database.connection(), "SELECT v FROM steps ORDER BY v");
        database.executeAndCheckQuery(inside, QStringLiteral("unitsAcrossUnits"));
        const bool first = inside.next();
        check(first && inside.int64(0) == 1, QStringLiteral("CONTROL: inside its unit the cursor reads its first row"));
        db::Error e;
        unit->commit(&e);
        unit.reset();
        unit.emplace(database.connection(), "the test's second unit");
        check(database.connection().guards() == 1, QStringLiteral("CONTROL: the second unit stands at the first one's depth (%1)")
                                                        .arg(database.connection().guards()));
        const bool after = inside.next();
        check(!after && inside.failed()
                  && QString::fromStdString(inside.error().message).contains(QStringLiteral("outlived the transaction")),
              QStringLiteral("a step inside the NEXT unit at the same depth is refused by name: '%1'")
                  .arg(QString::fromStdString(inside.error().message)));
        unit.reset();
    }

    // 10. AN IMPORT WHOSE INSERT FAILED IS NO IMPORT (the audit's W3): a project archive whose asset row this
    //     library refuses (a TEMP trigger raising on it) answers false and leaves no project row - beside the same
    //     archive imported whole once the trigger is gone (the control).
    {
        const QString archive = QStringLiteral("db_units_w3_archive");
        QFile::remove(archive + QStringLiteral(".db"));
        {
            db::Options o;
            o.name = "w3-archive";
            db::Error e;
            db::Connection a = db::Connection::open((archive + QStringLiteral(".db")).toStdString(), o, &e);
            a.exec("CREATE TABLE projects (name TEXT, scene BLOB, thumbnail BLOB, version TEXT, guid TEXT);"
                   "CREATE TABLE assets (guid TEXT, type INTEGER, name TEXT, collection INTEGER, times_used INTEGER, "
                   "project_guid TEXT, date_created TEXT, last_updated TEXT, author TEXT, license TEXT, hash TEXT, "
                   "version TEXT, parent TEXT, tags BLOB, properties BLOB, asset BLOB, thumbnail BLOB);"
                   "CREATE TABLE dependencies (depender_type INTEGER, dependee_type INTEGER, project_guid TEXT, "
                   "depender TEXT, dependee TEXT, id TEXT);"
                   "CREATE TABLE folders (guid TEXT, name TEXT, parent TEXT, count INTEGER, project_guid TEXT, "
                   "date_created TEXT, last_updated TEXT, visible INTEGER);"
                   "INSERT INTO projects VALUES ('W3', CAST('{\"formatVersion\": 2}' AS BLOB), NULL, '0.9', 'w3-old');"
                   "INSERT INTO assets (guid, type, name, project_guid, parent) VALUES ('w3-asset', 2, 'w3', 'w3-old', 'w3-old')",
                   &e);
            check(!e, QStringLiteral("10: the archive is written %1").arg(QString::fromStdString(e.message)));
        }
        const auto projectRows = [&database](const char *guid) {
            db::Statement s(database.connection(), "SELECT count(*) FROM projects WHERE guid = ?");
            s.bindText(1, guid);
            db::Error e;
            return s.step(&e) ? int(s.int64(0)) : -1;
        };
        db::Error e;
        database.connection().exec("CREATE TEMP TRIGGER w3poison BEFORE INSERT ON main.assets WHEN NEW.guid = 'w3-asset' "
                                   "BEGIN SELECT RAISE(ABORT, 'injected: this row is refused'); END", &e);
        QString world;
        QMap<QString, QString> guids;
        const bool refused = database.importProject(archive, QStringLiteral("w3-poisoned"), world, guids);
        check(!refused && projectRows("w3-poisoned") == 0,
              QStringLiteral("10: an import whose asset insert failed answers false and leaves no project row (answer %1, rows %2)")
                  .arg(refused).arg(projectRows("w3-poisoned")));
        database.connection().exec("DROP TRIGGER temp.w3poison", &e);
        const bool imported = database.importProject(archive, QStringLiteral("w3-control"), world, guids);
        check(imported && projectRows("w3-control") == 1,
              QStringLiteral("10: CONTROL: the same archive imports whole without the trigger (answer %1, rows %2)")
                  .arg(imported).arg(projectRows("w3-control")));
        QFile::remove(archive + QStringLiteral(".db"));
    }

    // 11. "IS IT PINNED HERE" THAT COULD NOT BE READ IS NO ANSWER (NOQT-DATABASE-1 stage 2, the isAssetPinnedBy
    //     limit): a failed table probe (an SQLite authorizer, test-side, denying sqlite_master on the library's
    //     handle) and a closed library (below) answer false WITH readOk cleared - beside a pin that reads true.
    {
        db::Error e;
        database.connection().exec("INSERT INTO project_assets (project_guid, asset_guid, oid_pin) VALUES ('p11', 'a11', '')", &e);
        bool read = true;
        const bool pinned = database.isAssetPinnedBy(QStringLiteral("p11"), QStringLiteral("a11"), &read);
        check(pinned && read, QStringLiteral("11: CONTROL: a pin reads true (pinned %1, read %2)").arg(pinned).arg(read));
        sqlite3 *raw = *static_cast<sqlite3 *const *>(QSqlDatabase::database().driver()->handle().constData());
        sqlite3_set_authorizer(raw, [](void *, int action, const char *table, const char *, const char *, const char *) {
            const bool master = table && (std::strcmp(table, "sqlite_master") == 0 || std::strcmp(table, "sqlite_schema") == 0);
            return action == SQLITE_READ && master ? SQLITE_DENY : SQLITE_OK;
        }, nullptr);
        read = true;
        const bool probed = database.isAssetPinnedBy(QStringLiteral("p11"), QStringLiteral("a11"), &read);
        sqlite3_set_authorizer(raw, nullptr, nullptr);
        check(!probed && !read, QStringLiteral("11: a FAILED probe answers no read (pinned %1, read %2)").arg(probed).arg(read));
    }

    // 12. AN ARCHIVE WHOSE PROJECT ROW CANNOT BE READ IS REFUSED BY NAME (the confirm's F5) - never an empty, unnamed
    //     world: its `projects` is a view raising "integer overflow" on its row; every other table reads (empty).
    {
        const QString archive = QStringLiteral("db_units_f5_archive");
        QFile::remove(archive + QStringLiteral(".db"));
        {
            db::Options o;
            o.name = "f5-archive";
            db::Error e;
            db::Connection a = db::Connection::open((archive + QStringLiteral(".db")).toStdString(), o, &e);
            a.exec("CREATE VIEW projects AS SELECT 'F5' AS name, NULL AS scene, NULL AS thumbnail, '0.9' AS version, "
                   "abs(-9223372036854775807 - 1) AS guid;"
                   "CREATE TABLE assets (guid TEXT, type INTEGER, name TEXT, collection INTEGER, times_used INTEGER, "
                   "project_guid TEXT, date_created TEXT, last_updated TEXT, author TEXT, license TEXT, hash TEXT, "
                   "version TEXT, parent TEXT, tags BLOB, properties BLOB, asset BLOB, thumbnail BLOB);"
                   "CREATE TABLE dependencies (depender_type INTEGER, dependee_type INTEGER, project_guid TEXT, "
                   "depender TEXT, dependee TEXT, id TEXT);"
                   "CREATE TABLE folders (guid TEXT, name TEXT, parent TEXT, count INTEGER, project_guid TEXT, "
                   "date_created TEXT, last_updated TEXT, visible INTEGER)",
                   &e);
            check(!e, QStringLiteral("12: the archive is written %1").arg(QString::fromStdString(e.message)));
        }
        QString world, why;
        QMap<QString, QString> guids;
        const bool imported = database.importProject(archive, QStringLiteral("f5-import"), world, guids, nullptr, &why);
        check(!imported && why.contains(QStringLiteral("could not be read")),
              QStringLiteral("12: an archive whose project row could not be read is REFUSED by name (answer %1, world '%2', "
                             "why '%3')").arg(imported).arg(world, why));
        QFile::remove(archive + QStringLiteral(".db"));
    }

    // 13. "DOES IT HAVE DEPENDENCIES" THAT COULD NOT BE READ IS NO ANSWER (the confirm's F3: the panel's legacy delete
    //     picks the branch that deletes the whole closure on "no"): the dependencies read denied by an authorizer.
    {
        db::Error e;
        database.connection().exec("INSERT INTO dependencies (depender_type, dependee_type, project_guid, depender, dependee, id) "
                                   "VALUES (1, 2, NULL, 'f3-depender', 'f3-dependee', 'f3-edge')", &e);
        check(database.hasDependencies(QStringLiteral("f3-depender")),
              QStringLiteral("13: CONTROL: an asset with an edge has dependencies"));
        sqlite3 *raw = *static_cast<sqlite3 *const *>(QSqlDatabase::database().driver()->handle().constData());
        sqlite3_set_authorizer(raw, [](void *, int action, const char *table, const char *, const char *, const char *) {
            return action == SQLITE_READ && table && std::strcmp(table, "dependencies") == 0 ? SQLITE_DENY : SQLITE_OK;
        }, nullptr);
        bool read = true;
        const bool unread = database.hasDependencies(QStringLiteral("f3-depender"), &read);
        sqlite3_set_authorizer(raw, nullptr, nullptr);
        check(!read, QStringLiteral("13: a FAILED read is no answer, never 'no dependencies' (answer %1, read %2)")
                         .arg(unread).arg(read));
        read = true;
        database.hasDependencies(QStringLiteral("f3-depender"), &read);
        check(read, QStringLiteral("13: CONTROL: a read that ran keeps readOk"));
    }

    // 14. WHERE A PROJECT's PINS ARE FILED, unread, is no answer (the sweep's d: an archive would put every pinned row
    //     at its root): the project_assets read denied by an authorizer.
    {
        db::Error e;
        database.connection().exec("INSERT INTO project_assets (project_guid, asset_guid, oid_pin, folder) "
                                   "VALUES ('p14', 'a14', '', 'f14')", &e);
        bool read = true;
        const QHash<QString, QString> filed = database.fetchProjectPinFolders(QStringLiteral("p14"), &read);
        check(read && filed.value(QStringLiteral("a14")) == QLatin1String("f14"),
              QStringLiteral("14: CONTROL: a filed pin reads its folder (%1)").arg(filed.value(QStringLiteral("a14"))));
        sqlite3 *raw = *static_cast<sqlite3 *const *>(QSqlDatabase::database().driver()->handle().constData());
        sqlite3_set_authorizer(raw, [](void *, int action, const char *table, const char *, const char *, const char *) {
            return action == SQLITE_READ && table && std::strcmp(table, "project_assets") == 0 ? SQLITE_DENY : SQLITE_OK;
        }, nullptr);
        read = true;
        database.fetchProjectPinFolders(QStringLiteral("p14"), &read);
        sqlite3_set_authorizer(raw, nullptr, nullptr);
        check(!read, QStringLiteral("14: a FAILED read of the pins' folders is no answer (read %1)").arg(read));
    }

    // 15. THE LIVE COLUMNS UNREAD ARE NO VERDICT (NOQT-DATABASE-1 stage 3, step 1: the boot's schema check resets a
    //     library it judges different - so a columns read that FAILED answers "cannot judge", never "differs"): the
    //     table-info reads denied on the library's own handle, beside a library that matches and one that truly differs.
    {
        check(database.schemaMatchesFresh(), QStringLiteral("15: CONTROL: a fresh library matches this build's schema"));
        dbseam::deny(database.connection(), [](int action, const char *what) {
            return action == dbseam::kPragma || (action == dbseam::kRead && what && std::strncmp(what, "pragma_", 7) == 0);
        });
        const bool unread = database.schemaMatchesFresh();
        dbseam::clear(database.connection());
        check(unread, QStringLiteral("15: a library whose columns could NOT be read is never judged different (answer %1)")
                          .arg(unread));
        db::Error e;
        database.connection().exec("ALTER TABLE favorites ADD COLUMN stage3_extra INTEGER", &e);
        check(!e && !database.schemaMatchesFresh(),
              QStringLiteral("15: CONTROL: a table with a column this build does not make differs %1")
                  .arg(QString::fromStdString(e.message)));
        database.connection().exec("ALTER TABLE favorites DROP COLUMN stage3_extra", &e);
        check(!e && database.schemaMatchesFresh(), QStringLiteral("15: CONTROL: the column dropped, it matches again"));
    }

    // 16. THE PRESENT INDEXES UNREAD BUILD NOTHING (stage 3, step 1: a failed read of the schema made every CREATE
    //     "needed"): the boot's gate over a file it cannot read answers -1 (said), never a count to build; the open's
    //     createIndexes with sqlite_master denied builds nothing and reports -1 - beside a library missing ONE index,
    //     which the gate counts (1) and builds (then 0).
    {
        db::Error e;
        database.connection().exec("DROP INDEX idx_assets_parent", &e);
        QString readable = QStringLiteral("(not set)");
        const int missing = Database::indexStatementsToRun(QString::fromLatin1(kFile), &readable);
        QString why;
        const bool built = Database::prepareIndexes(QString::fromLatin1(kFile), &why);
        const int after = Database::indexStatementsToRun(QString::fromLatin1(kFile), nullptr);
        check(!e && missing == 1 && readable.isEmpty() && built && after == 0,
              QStringLiteral("16: CONTROL: one index missing is counted (%1, the boot goes on: refusal '%2'), built (%3 %4), "
                             "then none (%5)").arg(missing).arg(readable).arg(built).arg(why).arg(after));
        const QString garbage = QStringLiteral("db_units_garbage.db");
        {
            QFile f(garbage);
            f.open(QIODevice::WriteOnly);
            f.write(QByteArray(8192, 'x'));
        }
        QString refusal;
        const int unreadGate = Database::indexStatementsToRun(garbage, &refusal);
        QFile::remove(garbage);
        check(unreadGate == -1, QStringLiteral("16: the gate over a file it cannot read answers -1, never a count to build (%1)")
                                    .arg(unreadGate));
        // THE BOOT's DECISION (the reader's point 2): main.cpp refuses the boot (exit 4) on this text, by name.
        check(refusal.startsWith(QStringLiteral("the library's indexes could not be read: "))
                  && refusal.contains(QStringLiteral("not a database")),
              QStringLiteral("16: the boot's decision over an unread gate is REFUSE, naming SQLite's text: '%1'").arg(refusal));
        dbseam::deny(database.connection(), [](int action, const char *what) {
            return action == dbseam::kRead && what
                && (std::strcmp(what, "sqlite_master") == 0 || std::strcmp(what, "sqlite_schema") == 0);
        });
        database.createIndexes();
        dbseam::clear(database.connection());
        check(database.indexStatementsAtOpen() == -1,
              QStringLiteral("16: the open's present indexes unread: nothing built, the report reads -1 (%1)")
                  .arg(database.indexStatementsAtOpen()));
    }

    // 17. A TILE READ THAT FAILED IS UNREAD, NEVER "NO PICTURE" (stage 3, step 1: the tile cache marks a guid the read
    //     did not answer as missing for good, and the preview demand renders one for it): a file it cannot read
    //     answers no answer at all - beside the library's own file, whose row with a picture is answered.
    {
        db::Error e;
        database.connection().exec("INSERT INTO assets (guid, name, thumbnail) VALUES ('t17', 't17', x'0102')", &e);
        const std::optional<QHash<QString, QByteArray>> read =
            Database::readThumbnailBytes(QString::fromLatin1(kFile), Database::ThumbnailTable::Assets, { QStringLiteral("t17") });
        check(!e && read && read->value(QStringLiteral("t17")) == QByteArray("\x01\x02", 2),
              QStringLiteral("17: CONTROL: the library's row with a picture is answered (%1 rows)").arg(read ? read->size() : -1));
        const QString garbage = QStringLiteral("db_units_garbage17.db");
        {
            QFile f(garbage);
            f.open(QIODevice::WriteOnly);
            f.write(QByteArray(8192, 'x'));
        }
        const std::optional<QHash<QString, QByteArray>> unread =
            Database::readThumbnailBytes(garbage, Database::ThumbnailTable::Assets, { QStringLiteral("t17") });
        QFile::remove(garbage);
        check(!unread, QStringLiteral("17: a file it cannot read answers NO answer (unread), never an empty one (%1)")
                           .arg(unread ? QStringLiteral("an answer of %1 rows").arg(unread->size()) : QStringLiteral("none")));
        // NO LIBRARY (the reader's point 7): an empty path is unread too, never "no picture".
        const std::optional<QHash<QString, QByteArray>> none =
            Database::readThumbnailBytes(QString(), Database::ThumbnailTable::Assets, { QStringLiteral("t17") });
        check(!none, QStringLiteral("17: no library (an empty path) answers NO answer (%1)")
                         .arg(none ? QStringLiteral("an answer of %1 rows").arg(none->size()) : QStringLiteral("none")));
        // SAID ONCE A STREAK (the reader's point 6a): a fresh streak of three failures says ONE warning; the read that
        // succeeds after it ends the streak, saying the count.
        Database::readThumbnailBytes(QString::fromLatin1(kFile), Database::ThumbnailTable::Assets, { QStringLiteral("t17") });
        const quint64 from = JahLog::mark(QStringLiteral("db.units: case 17's streak"));
        for (int i = 0; i < 3; ++i) Database::readThumbnailBytes(QString(), Database::ThumbnailTable::Assets, { QStringLiteral("t17") });
        const int warned = int(JahLog::since(from).filter(QStringLiteral("tile read: fetchAssetThumbnailBytes could not be read")).size());
        Database::readThumbnailBytes(QString::fromLatin1(kFile), Database::ThumbnailTable::Assets, { QStringLiteral("t17") });
        const QStringList ended = JahLog::since(from).filter(QStringLiteral("tile read: reads again after"));
        check(warned == 1 && ended.size() == 1 && ended.first().contains(QStringLiteral("after 3 failed batch(es) (2 repeat(s)")),
              QStringLiteral("17: three failures say ONE warning (%1), and the read after them ends the streak by its count "
                             "(%2)").arg(warned).arg(ended.join(QLatin1Char('|'))));
    }

    // 18. THE PREVIEW ROWS CUT SHORT ARE NO ANSWER (stage 3, step 1: fetchPreviewRows read to its loop's end and
    //     answered what it had): a read that fails on a LATER row answers no rows, said by name - beside the same
    //     listing read whole. The failure: a TEMP VIEW shadowing asset_files raises on one asset's files (the scan
    //     is in rowid order, one row a step, so the rows before it were already handed out).
    {
        db::Error e;
        database.connection().exec("INSERT INTO assets (guid, name, type) VALUES ('p18a', 'p18a', 1), ('p18b', 'p18b', 1);"
                                   "INSERT INTO asset_files (asset_guid, role, oid) VALUES ('p18a', 'source', 'o18a'), "
                                   "('p18b', 'source', 'o18b')", &e);
        const QVector<Database::PreviewRow> whole = database.fetchPreviewRows(QString(), false, -1);
        int named = 0;
        for (const Database::PreviewRow &row : whole) named += row.guid.startsWith(QStringLiteral("p18")) ? 1 : 0;
        check(!e && named == 2, QStringLiteral("18: CONTROL: the listing read whole names both rows (%1 of %2) %3")
                                    .arg(named).arg(whole.size()).arg(QString::fromStdString(e.message)));
        database.connection().exec("CREATE TEMP VIEW asset_files AS SELECT asset_guid, role, "
                                   "CASE WHEN asset_guid = 'p18b' THEN abs(-9223372036854775807 - 1) ELSE oid END AS oid "
                                   "FROM main.asset_files", &e);
        const QVector<Database::PreviewRow> cut = database.fetchPreviewRows(QString(), false, -1);
        db::Error dropped;
        database.connection().exec("DROP VIEW temp.asset_files", &dropped);
        check(!e && cut.isEmpty(), QStringLiteral("18: a listing that failed on a later row answers NO rows, never a short "
                                                  "list (%1 rows) %2").arg(cut.size()).arg(QString::fromStdString(e.message)));
    }

    database.closeDatabase();
    {
        bool read = true;
        const bool closed = database.isAssetPinnedBy(QStringLiteral("p11"), QStringLiteral("a11"), &read);
        check(!closed && !read, QStringLiteral("11: a CLOSED library answers no read (pinned %1, read %2)").arg(closed).arg(read));
    }
    removeFile();
    qInstallMessageHandler(nullptr);
    if (failures) {
        std::printf("db.units: FAILED (%d)\n", failures);
        return 1;
    }
    std::printf("db.units: PASSED\n");
    return 0;
}
