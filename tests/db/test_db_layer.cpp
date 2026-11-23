/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// db.layer (NOQT-DATABASE-1 stage 1) — the laws of the library's SQLite layer (src/data/db/) on a
// throwaway file of its own:
//   1. a typed round trip of every kind, NULL vs '' vs x'' told apart by typeof() — and the Database's
//      Qt adapter (dbqt.h) keeps QtSql's meaning: QString() binds NULL, QString("") binds '';
//   2. a named and a positional bind; an unbound parameter REFUSED by its name;
//   3. changes() and lastInsertRowid();
//   4. a failed statement answers a NAMED error (its connection, SQLite's message, the statement);
//   5. a Transaction rolls back at scope end; a nested one rides the outer;
//   6. TWO CONNECTIONS: A holds BEGIN IMMEDIATE, B meets the lock: busy(), and the holder line names A,
//      A's reason and A's first write (ruling 6); B's failed BEGIN refuses its writes and its commit says false;
//   7. A CRASH MID-TRANSACTION: a child begins, writes and _exits; the file is whole and the write absent,
//      beside a committed child's write that is present;
//   8. the version floor (ruling 8): a minimum above the library's is refused by name THROUGH open(); a connection
//      with no handle answers a named error;
//   9. the statement trace (stage 2): every statement on its connection, once per execution, none on another;
//      removed by an empty function; REFUSED on an observed connection (case 13);
//  10. run(): to the end, false with the failing step's error (a later row's included), once per reset();
//  11. the WAL answer kept: wal() true on a file, false where WAL is refused (':memory:');
//  12. walAutocheckpoint 0 is 0 on the connection (unset: SQLite's 1000);
//  13. the process-wide statement observer: per connection NAME, the open's pragmas counted, set once;
//  14. pagerStats: a cold read's misses, a write's writes, no handle no counts.

#include "data/database/dbqt.h"
#include "data/db/connection.h"
#include "data/db/statement.h"
#include "data/db/transaction.h"


#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
    std::fflush(stdout);
    if (!ok) ++failures;
}

bool contains(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }

const char *kFile = "db_layer_test.db";

void removeFile()
{
    for (const char *suffix : { "", "-wal", "-shm", "-journal" }) std::remove((std::string(kFile) + suffix).c_str());
}

db::Connection openNamed(const std::string &name, int busyMs = 0)
{
    db::Options o;
    o.name = name;
    o.busyMs = busyMs;
    o.wal = true;
    db::Error e;
    db::Connection c = db::Connection::open(kFile, o, &e);
    check(c.isOpen() && !e, "open " + name + (e ? ": " + e.message : std::string()));
    return c;
}

/// typeof() and the value of row `k`, as one line: "<type>:<value>".
std::string row(db::Connection &c, std::int64_t k)
{
    db::Statement s(c, "SELECT typeof(v), v FROM t WHERE k = ?");
    s.bindInt(1, k);
    db::Error e;
    if (!s.step(&e)) return "none" + (e ? " " + e.message : std::string());
    return s.text(0) + ":" + s.text(1);
}

bool insert(db::Connection &c, const std::string &value, db::Error *e)
{
    db::Statement s(c, "INSERT INTO t(v) VALUES (?)");
    s.bindText(1, value);
    s.step(e);
    return !*e;
}

int countOf(db::Connection &c, const std::string &value)
{
    db::Statement s(c, "SELECT count(*) FROM t WHERE v = ?");
    s.bindText(1, value);
    db::Error e;
    return s.step(&e) ? int(s.int64(0)) : -1;
}

int countIn(db::Connection &c, const std::string &table)
{
    db::Statement s(c, "SELECT count(*) FROM " + table);
    db::Error e;
    return s.step(&e) ? int(s.int64(0)) : -1;
}

/// A child process opens the file, begins, writes `value`, commits or not, and _exits without any cleanup.
void childWrites(const std::string &value, bool commit)
{
    const pid_t pid = fork();
    if (pid == 0) {
        db::Options o;
        o.name = "child";
        o.wal = true;
        db::Error e;
        db::Connection c = db::Connection::open(kFile, o, &e);
        db::Transaction tx(c, "the child's unit");
        insert(c, value, &e);
        if (commit) tx.commit(&e);
        _exit(e ? 3 : 0);   // no destructor runs: an uncommitted unit dies with the process
    }
    int status = 0;
    waitpid(pid, &status, 0);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          std::string("the child (") + (commit ? "committed" : "crashing") + ") ran to its _exit");
}

}   // namespace

int main()
{
    removeFile();
    std::printf("info: SQLite library %d (sqlite3_libversion_number), the floor %d\n", db::libraryVersion(),
                db::kMinimumVersion);
    check(db::libraryVersion() > 0, "CONTROL: the library's version reads above zero");

    // 8a. THE VERSION FLOOR (ruling 8).
    {
        db::Error e;
        check(db::checkVersion(db::kMinimumVersion, &e) && !e, "the library meets the floor (3.37: STRICT tables)");
        const bool refused = !db::checkVersion(db::libraryVersion() + 1, &e);
        check(refused && e && contains(e.message, "older than the library needs"),
              "a minimum above the library's is REFUSED by name: " + e.message);
        // ...and open() APPLIES its floor (ruling 8): a floor above the library is refused THROUGH open().
        db::Options raised;
        raised.name = "floor";
        raised.minimumVersion = db::libraryVersion() + 1;
        db::Error oe;
        db::Connection c = db::Connection::open(kFile, raised, &oe);
        check(!c.isOpen() && contains(oe.message, "floor: ") && contains(oe.message, "older than the library needs"),
              "open() refuses a library below its floor, by name: " + oe.message);
    }

    db::Connection a = openNamed("A");
    {
        db::Error e;
        check(a.exec("CREATE TABLE t(k INTEGER PRIMARY KEY, v)", &e), "a table with an untyped column");
    }

    // 1. THE TYPED ROUND TRIP.
    {
        const unsigned char bytes[] = { 0x00, 0xff, 0x10 };
        db::Statement s(a, "INSERT INTO t(k, v) VALUES (?, ?)");
        auto put = [&](std::int64_t k, auto bind) {
            s.reset();
            s.bindInt(1, k);
            bind();
            db::Error e;
            s.step(&e);
            check(!e, "insert row " + std::to_string(k) + (e ? ": " + e.message : std::string()));
        };
        put(1, [&] { s.bindInt(2, 9007199254740993LL); });
        put(2, [&] { s.bindReal(2, 0.25); });
        put(3, [&] { s.bindText(2, "h\xc3\xa9llo"); });
        put(4, [&] { s.bindBlob(2, bytes, sizeof bytes); });
        put(5, [&] { s.bindNull(2); });
        put(6, [&] { s.bindText(2, std::string_view()); });
        put(7, [&] { s.bindBlob(2, nullptr, 0); });
        put(8, [&] { dbqt::bindText(s, 2, QString()); });
        put(9, [&] { dbqt::bindText(s, 2, QStringLiteral("")); });
        put(10, [&] { dbqt::bindText(s, 2, QStringLiteral("x")); });
        check(row(a, 1) == "integer:9007199254740993", "an int64 past 2^53 round-trips: " + row(a, 1));
        check(row(a, 2) == "real:0.25", "a real round-trips: " + row(a, 2));
        check(row(a, 3) == "text:h\xc3\xa9llo", "UTF-8 text round-trips: " + row(a, 3));
        check(row(a, 5) == "null:", "bindNull is NULL: " + row(a, 5));
        check(row(a, 6) == "text:", "an EMPTY text is '' (never NULL): " + row(a, 6));
        check(row(a, 7) == "blob:", "an EMPTY blob is x'' (never NULL): " + row(a, 7));
        check(row(a, 8) == "null:", "dbqt: QString() binds NULL: " + row(a, 8));
        check(row(a, 9) == "text:", "dbqt: QString(\"\") binds '' (QtSql's meaning): " + row(a, 9));
        check(row(a, 10) == "text:x", "dbqt: a QString binds its UTF-8: " + row(a, 10));
        db::Statement r(a, "SELECT v FROM t WHERE k = 4");
        db::Error e;
        check(r.step(&e) && r.blob(0) == std::vector<std::uint8_t>(bytes, bytes + 3) && !r.isNull(0),
              "a blob with a zero byte round-trips");
        db::Statement n(a, "SELECT v FROM t WHERE k = 8");
        check(n.step(&e) && n.isNull(0) && dbqt::text(n, 0).isNull(), "dbqt: a NULL column reads as QString()");
    }

    // 2. BINDS: named, positional, unbound.
    {
        db::Statement s(a, "SELECT :x || ?2");
        s.bindText(":x", "na").bindText(2, "med");
        db::Error e;
        const bool got = s.step(&e);
        check(got && s.text(0) == "named", "a named and a positional bind: " + s.text(0));
        db::Statement u(a, "INSERT INTO t(k, v) VALUES (:k, :v)");
        u.bindInt(":k", 99);
        const bool ran = u.step(&e);
        check(!ran && e && contains(e.message, ":v"),
              "an UNBOUND parameter is refused by name: " + e.message);
        db::Statement w(a, "SELECT :x");
        w.bindText(":nope", "1");
        const bool wran = w.step(&e);
        check(!wran && contains(e.message, ":nope"), "a bind to a name the statement lacks is refused: " + e.message);
    }

    // 3. changes() and lastInsertRowid().
    {
        db::Error e;
        insert(a, "rowid", &e);
        db::Statement s(a, "SELECT k FROM t WHERE v = 'rowid'");
        const bool got = s.step(&e);
        check(got && s.int64(0) == a.lastInsertRowid() && a.lastInsertRowid() > 0,
              "lastInsertRowid is the inserted row's key: " + std::to_string(a.lastInsertRowid()));
        const bool updated = a.exec("UPDATE t SET v = v WHERE k IN (1, 2, 3)", &e);
        check(updated && a.changes() == 3,
              "changes() counts the rows an UPDATE touched: " + std::to_string(a.changes()));
    }

    // 4. A FAILED STATEMENT's NAMED ERROR.
    {
        db::Statement s(a, "SELECT * FROM nowhere");
        db::Error e;
        const bool ran = s.step(&e);
        check(!ran && e.code == 1 /* SQLITE_ERROR */ && contains(e.message, "A: ") && contains(e.message, "no such table")
                  && e.statement == "SELECT * FROM nowhere",
              "a failed statement names its connection, SQLite's message and itself: " + e.message);
    }

    // 5. THE TRANSACTION GUARD.
    {
        db::Error e;
        { db::Transaction tx(a, "dropped"); insert(a, "dropped", &e); }
        check(countOf(a, "dropped") == 0, "a Transaction never committed ROLLS BACK at scope end");
        {
            db::Transaction tx(a, "kept");
            insert(a, "kept", &e);
            check(tx.commit(&e) && !e, "a commit answers true");
        }
        check(countOf(a, "kept") == 1, "CONTROL: a committed write is present");
        {
            db::Transaction outer(a, "outer");
            {
                db::Transaction inner(a, "inner");
                insert(a, "inner", &e);
                check(inner.commit(&e), "a nested commit answers true (the outer's commit is the unit's)");
            }
            outer.rollback();
        }
        check(countOf(a, "inner") == 0, "a nested Transaction RIDES the outer: the outer's rollback takes its write");
    }

    // 6. TWO CONNECTIONS: the lock and its holder.
    {
        db::Connection b = openNamed("B", 0);
        int busySeen = 0;
        db::Error e;
        db::Transaction hold(a, "the test's hold");
        insert(a, "held", &e);
        {
            db::Statement w(b, "INSERT INTO t(v) VALUES ('b')");
            db::Error be;
            w.step(&be);
            busySeen += be.busy();
            check(be.busy() && contains(be.holder, "A (the test's hold)")
                      && contains(be.holder, "its first write: INSERT INTO t(v) VALUES (?)"),
                  "B's write meets A's lock: busy, the holder named: " + be.holder);
        }
        {
            db::Transaction tx(b, "B's unit");
            busySeen += tx.beginError().busy();
            check(tx.beginError().busy() && contains(tx.beginError().holder, "A (the test's hold)"),
                  "B's BEGIN IMMEDIATE meets A's lock: busy, the holder named: " + tx.beginError().holder);
            db::Error we;
            hold.rollback();   // the lock is free: only the refusal can stop B's write now
            db::Statement w(b, "INSERT INTO t(v) VALUES ('refused')");
            w.step(&we);
            check(we && !we.busy(), "B's failed BEGIN REFUSES its writes until its scope ends: " + we.message);
            const bool committed = tx.commit(&we);
            check(!committed && we.busy(), "B's commit answers false with the BEGIN's error: " + we.message);
        }
        check(busySeen >= 2, "CONTROL: B observed SQLITE_BUSY " + std::to_string(busySeen) + " times");
        check(insert(b, "after", &e) && countOf(a, "after") == 1, "the refusal ends with its scope: B writes again");
    }

    // 7. A CRASH MID-TRANSACTION.
    {
        childWrites("crashed", false);
        childWrites("committed", true);
        db::Connection c = openNamed("C");
        db::Statement s(c, "PRAGMA integrity_check");
        db::Error e;
        const bool got = s.step(&e);
        check(got && s.text(0) == "ok", "the file after a crash is whole: integrity_check = " + s.text(0));
        check(countOf(c, "crashed") == 0, "the crashed child's uncommitted write is ABSENT");
        check(countOf(c, "committed") == 1, "CONTROL: the committed child's write is PRESENT");
    }

    // 8b. A CONNECTION WITH NO HANDLE (the borrow is gone with stage 3: a connection is opened, never lent): its
    //     statement answers a named error.
    {
        db::Connection none;
        db::Error e;
        db::Statement s(none, "SELECT 1");
        const bool ran = s.step(&e);
        check(!ran && contains(e.message, "no handle"), "a statement on no handle answers a named error: " + e.message);
    }

    // 9. THE STATEMENT TRACE (stage 2): it sees every statement run on the connection it is installed on, once per
    //    execution, and none run on another connection; removed, it sees nothing more.
    {
        db::Connection a = openNamed("A");
        db::Connection b = openNamed("B");
        std::vector<std::string> seen;
        a.trace([&seen](std::string_view sql) { seen.emplace_back(sql); });
        db::Error e;
        countOf(a, "kept");
        insert(a, "traced", &e);
        countOf(b, "kept");   // another connection: never seen
        insert(b, "untraced", &e);
        check(seen.size() == 2, "CONTROL: the trace saw ABOVE zero statements: " + std::to_string(seen.size())
                                    + " (2 run on A; 2 on B not seen)");
        check(seen.size() == 2 && contains(seen[0], "SELECT count(*)") && contains(seen[1], "INSERT INTO t"),
              "the trace names each statement A ran, in order" + (seen.empty() ? std::string() : ": " + seen[0]));
        {
            db::Statement twice(a, "SELECT count(*) FROM t WHERE v = ?");
            twice.bindText(1, "kept");
            twice.step(&e);
            twice.reset();
            twice.step(&e);
        }
        check(seen.size() == 4, "a statement run twice is seen twice (once per execution): " + std::to_string(seen.size()));
        a.trace({});
        countOf(a, "kept");
        check(seen.size() == 4, "a removed trace sees nothing more: " + std::to_string(seen.size()));
    }

    // 10. run() (stage 2, the lead's ruling A): a statement run TO ITS END - a failing write answers false and names
    //     its error; a statement that fails on a LATER row answers false (run() did not stop at the first row);
    //     its rows are discarded; a second run() without reset() is refused by name, and after reset() runs again.
    {
        db::Connection a = openNamed("A");
        db::Error e;
        a.exec("CREATE TABLE IF NOT EXISTS once(k INTEGER PRIMARY KEY); DELETE FROM once; "
               "CREATE TABLE IF NOT EXISTS later(v INTEGER); DELETE FROM later; INSERT INTO later VALUES (1), (2)", &e);
        {
            db::Statement ok(a, "INSERT INTO once(k) VALUES (1)");
            const bool ran = ok.run(&e);
            check(ran && !e && countIn(a, "once") == 1, "CONTROL: run() of a write answers true and the row is there");
        }
        {
            db::Statement dup(a, "INSERT INTO once(k) VALUES (1)");
            const bool ran = dup.run(&e);
            check(!ran && e && contains(e.message, "UNIQUE"), "run() of a failing write answers false and names it: " + e.message);
        }
        {
            db::Statement whole(a, "SELECT v FROM later");
            const bool ran = whole.run(&e);
            check(ran && !e, "CONTROL: run() of a two-row read reaches its end and answers true");
        }
        {
            db::Statement broken(a, "SELECT CASE WHEN v = 2 THEN abs(-9223372036854775807 - 1) ELSE v END FROM later");
            const bool ran = broken.run(&e);
            check(!ran && contains(e.message, "overflow"),
                  "run() of a statement that fails on its SECOND row answers false with that step's error: " + e.message);
        }
        {
            db::Statement twice(a, "SELECT v FROM later");
            const bool first = twice.run(&e);
            const bool again = twice.run(&e);
            check(first && !again && contains(e.message, "already ran"), "a second run() without reset() is refused by name: " + e.message);
            twice.reset();
            const bool afterReset = twice.run(&e);
            check(afterReset && !e, "...and after reset() it runs again");
        }
    }

    // 11. THE WAL ANSWER IS KEPT (stage 3): a file opened with Options.wal answers "wal" (wal() true); a database
    //     whose journal cannot be WAL (":memory:" answers "memory") opens, wal() false - a refused WAL is the
    //     previous mode, never a guess from the option.
    {
        db::Connection file = openNamed("walFile");
        db::Options o;
        o.name = "walMemory";
        o.wal = true;
        db::Error e;
        db::Connection memory = db::Connection::open(":memory:", o, &e);
        check(file.wal(), "11: CONTROL: a file opened with WAL answers it: wal() true");
        check(memory.isOpen() && !e && !memory.wal(), "11: a REFUSED WAL (':memory:' answers 'memory') opens with wal() false"
                                                          + (e ? ": " + e.message : std::string()));
    }

    // 12. wal_autocheckpoint (stage 3): 0 asked is 0 on the connection; CONTROL the option unset keeps SQLite's 1000.
    {
        const auto autocheckpoint = [](db::Connection &c) {
            db::Statement s(c, "PRAGMA wal_autocheckpoint");
            db::Error e;
            return s.step(&e) ? int(s.int64(0)) : -1;
        };
        db::Options o;
        o.name = "noCheckpoint";
        o.wal = true;
        o.walAutocheckpoint = 0;
        db::Error e;
        db::Connection none = db::Connection::open(kFile, o, &e);
        db::Connection plain = openNamed("ownCheckpoint");
        check(none.wal() && autocheckpoint(none) == 0, "12: walAutocheckpoint 0 is 0 on the connection: "
                                                             + std::to_string(autocheckpoint(none)));
        check(autocheckpoint(plain) == 1000, "12: CONTROL: unset, SQLite's own 1000: " + std::to_string(autocheckpoint(plain)));
    }

    // 13. THE STATEMENT OBSERVER (stage 3, ruled call 5): set ONCE; a connection opened after it is counted under ITS
    //     name, its open's pragmas included; another connection's statements never reach a name not its own; one
    //     opened BEFORE it is not observed; a second observer is refused.
    {
        db::Connection before = openNamed("beforeObserver");
        std::map<std::string, int> byName;
        check(db::observeStatements([&byName](std::string_view name, std::string_view sql) {
                  if (!(sql.size() >= 2 && sql[0] == '-' && sql[1] == '-')) ++byName[std::string(name)];
              }),
              "13: the observer is set");
        check(!db::observeStatements([](std::string_view, std::string_view) {}), "13: a second observer is refused");
        db::Connection a = openNamed("obsA");
        const int aOpen = byName["obsA"];
        db::Connection b = openNamed("obsB");
        countOf(a, "kept");
        countOf(a, "kept");
        countOf(b, "kept");
        countOf(before, "kept");
        check(aOpen == 2, "13: an observed open's own pragmas count under its name (journal_mode, synchronous): "
                              + std::to_string(aOpen));
        check(byName["obsA"] == aOpen + 2 && byName["obsB"] == aOpen + 1,
              "13: each connection's statements count under ITS name (obsA " + std::to_string(byName["obsA"]) + ", obsB "
                  + std::to_string(byName["obsB"]) + ")");
        check(a.observed() && !before.observed() && byName.count("beforeObserver") == 0,
              "13: a connection opened before the observer is not observed");
        // THE OBSERVER's SLOT IS NEVER TAKEN (the reader's note 1): trace() on an observed connection is refused, and
        // its statements still count under its name - beside the unobserved connection, whose trace() is taken.
        const bool replaced = a.trace([](std::string_view) {});
        const bool removed = a.trace({});
        countOf(a, "kept");
        check(!replaced && !removed && byName["obsA"] == aOpen + 3,
              "13: trace() on an observed connection is REFUSED and its count goes on (obsA " + std::to_string(byName["obsA"])
                  + ")");
        check(before.trace({}), "13: CONTROL: trace() on an unobserved connection is taken");
    }

    // 14. THE PAGER's COUNTS (stage 3: databasereads' page reads and the suites' page writes, through the layer): a
    //     cold read misses pages, a write writes pages; a connection with no handle answers no counts.
    {
        db::Connection cold = openNamed("cold");
        const db::PagerStats start = db::pagerStats(cold);
        countOf(cold, "kept");
        const db::PagerStats read = db::pagerStats(cold);
        db::Error e;
        {
            db::Transaction tx(cold, "pager writes");
            insert(cold, "paged", &e);
            tx.commit(&e);
        }
        const db::PagerStats wrote = db::pagerStats(cold);
        check(start.ok && read.cacheMisses > start.cacheMisses, "14: a cold read misses pages: " + std::to_string(start.cacheMisses)
                                                                     + " -> " + std::to_string(read.cacheMisses));
        check(wrote.cacheWrites > read.cacheWrites, "14: a committed write writes pages: " + std::to_string(read.cacheWrites)
                                                        + " -> " + std::to_string(wrote.cacheWrites));
        check(!db::pagerStats(db::Connection()).ok, "14: no handle answers no counts");
    }

    removeFile();
    if (failures) {
        std::printf("db.layer: FAILED (%d)\n", failures);
        return 1;
    }
    std::printf("db.layer: PASSED\n");
    return 0;
}
