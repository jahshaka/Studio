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
//   8. an import's archive that cannot be opened read-only is refused by name before any read, and never created.

#include <QApplication>
#include <QFile>

#include <csignal>
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
    DbTransaction tx(database.getDb());
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
            DbTransaction tx(database.getDb());
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
            DbTransaction tx(database.getDb());
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
            DbTransaction tx(database.getDb());
            database.createFolder(QStringLiteral("dropped-1"), QString(), QStringLiteral("d1"), QString());
            database.renameFolder(QStringLiteral("a"), QStringLiteral("renamed"));
        }
        const std::string after = dump(side);
        check(!before.empty() && after == before,
              QStringLiteral("a dropped unit leaves the logical content byte-equal (%1 bytes of rows)").arg(before.size()));
        {
            DbTransaction tx(database.getDb());
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

    database.closeDatabase();
    removeFile();
    qInstallMessageHandler(nullptr);
    if (failures) {
        std::printf("db.units: FAILED (%d)\n", failures);
        return 1;
    }
    std::printf("db.units: PASSED\n");
    return 0;
}
