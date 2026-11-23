/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DATA_DB_CONNECTION_H
#define DATA_DB_CONNECTION_H

// THE LIBRARY's SQLITE LAYER (NOQT-DATABASE-1, SPECS/NOQT_DATABASE_1_SPEC.md "THE LAYER"): a
// connection, a prepared statement with typed binds and reads (statement.h), a transaction guard
// (transaction.h) and a named error (error.h). Std only; the ONE place that calls sqlite3_*.
//
// THE THREADING LAW: one connection per thread, never shared. A connection belongs to the
// thread that opened it (asserted in Debug at every statement and transaction).
// Only one BEGIN IMMEDIATE holds a file at a time, by SQLite's own lock; a connection that
// meets it waits its busy timeout and then answers a busy() Error naming the holder.
//
// THE LAYER GUARD (NOQT-DATABASE-1 stage 3, the lead's ruling): A FAILED READ NEVER DECIDES A WRITE UNSEEN.
//   - THE MARK: a READ statement (sqlite3_stmt_readonly; a statement whose prepare failed counts as one) whose step
//     answers an ERROR - "no row" is an answer, never a failure - marks its connection with its statement and
//     SQLite's text. The Error it answered carries the mark's flag: TESTING that Error (its truth test; DbQuery's
//     failed() / error(); run()'s answer) is LOOKING, and looking clears the mark. A log line never looks (it reads
//     `.code`: source.db_error_looks). An unlooked mark is never replaced by a newer failure. AN ERROR IS TESTED ON
//     ITS CONNECTION's THREAD (the threading law above): the flag is a plain bool, shared by nothing else.
//   - THE REFUSAL: the next WRITE on that connection (a Statement's first step, an exec() - a BEGIN IMMEDIATE is a
//     write) while the mark stands is REFUSED BY NAME ("write refused: an earlier read failed and nobody looked:
//     <statement>: <SQLite's text>"). Inside an open UNIT (a Transaction) the unit fails whole - its commit answers
//     false and rolls back - and a mark the unit refused under ends with it unless a gesture scope holds it (a mark
//     the unit never refused under - a read that failed with nothing written after it - stands); inside a GESTURE SCOPE (the
//     script run's batch: gestureScope) every write to the scope's end is refused and the mark ends with the scope;
//     a bare autocommit write consumes it: ONE refusal.
//   - A PENDING LIBRARY: refuseWrites(why) refuses EVERY write by that name from then on, never consumed (a failed
//     reset over a library at generation 0: its session ends).

#include "data/db/error.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

struct sqlite3;

namespace db {

/// The oldest SQLite the library runs on: 3.37.0, the first with STRICT tables (ruling 8).
constexpr int kMinimumVersion = 3037000;

/// The busy timeout of the editor's own connection: what Qt's QSQLITE driver gives a connection
/// opened with no options, MEASURED on the editor's default connection (`PRAGMA busy_timeout` =
/// 5000 ms, Qt 6.10.2, NOQT-DATABASE-1 stage 1's first interim).
constexpr int kEditorBusyMs = 5000;

/// The SQLite library this process calls, as sqlite3_libversion_number() (3.46.1 = 3046001).
int libraryVersion();

/// Refuses, by name, a library older than `minimum`. open() asks it with its floor (kMinimumVersion).
bool checkVersion(int minimum, Error *e);

struct Options
{
    bool readOnly = false;
    int busyMs = 0;          ///< sqlite3_busy_timeout: how long a locked file is waited for
    bool wal = false;        ///< journal_mode WAL (its ANSWER kept: wal()) + synchronous NORMAL
    /// With `wal` and WAL answered: PRAGMA wal_autocheckpoint = N when >= 0 (0: no checkpoint ever runs inside a
    /// commit on this connection - the editor's, whose checkpoints are a worker's). -1: SQLite's own.
    int walAutocheckpoint = -1;
    std::string name;        ///< the connection's name in every error and holder line, and its statements' count
    int minimumVersion = kMinimumVersion;   ///< the floor open() refuses below (db.layer raises it to prove the refusal)
};

class Connection
{
public:
    /// No handle: every statement on it answers a named error.
    Connection();
    ~Connection();
    Connection(Connection &&) noexcept;
    Connection &operator=(Connection &&) noexcept;
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    /// Opens (and owns) the file. A failure returns a connection with no handle and fills `e`.
    static Connection open(const std::string &path, const Options &options, Error *e);

    bool isOpen() const;
    const std::string &name() const;
    /// open() asked for WAL (Options.wal) and SQLite ANSWERED "wal": a refused WAL (a filesystem with no shared
    /// memory, a read-only file) answers the previous mode, and this is false.
    bool wal() const;
    /// Opened after observeStatements: its statements reach the observer under its name.
    bool observed() const;
    /// THE LAYER GUARD (above): a GESTURE SCOPE opens and closes (counted; the Database's script-run batch): while
    /// one is open a failed read's mark is held to its end and every write meanwhile refused; the last close clears it.
    void gestureScope(bool open);
    /// THE LAYER GUARD: every write on this connection refused by `why` from now on (a pending library).
    void refuseWrites(std::string why);
    /// A failed read's mark stands (nobody looked yet).
    bool marked() const;

    /// Runs SQL with no binds and no rows (pragmas, DDL; several statements allowed).
    bool exec(std::string_view sql, Error *e);
    std::int64_t lastInsertRowid() const;
    int changes() const;
    /// Live Transaction guards on this connection (0: the next one is the outermost).
    int guards() const;
    /// The serial of the outermost unit opened on this connection: it moves each time a Transaction opens with
    /// none live, so a unit that ended and a new one at the same depth are told apart.
    std::uint64_t unit() const;
    /// The outermost Transaction's BEGIN failed and its scope still stands: writes are refused.
    bool refusesWrites() const;
    /// THE STATEMENT TRACE (stage 2): `onStatement` is called with the text of EVERY statement run on this
    /// connection, once per execution, at its first step (SQLite's SQLITE_TRACE_STMT; a trigger's sub-statements
    /// arrive as their `--` comment text). An empty function removes it; so does the end of the connection.
    /// REFUSED (false, nothing changed) on an OBSERVED connection: the observer holds the connection's one trace
    /// slot, and replacing or removing it would take the connection's statements out of its name's count.
    bool trace(std::function<void(std::string_view sql)> onStatement);

    struct State;   // the layer's own (connection.cpp, statement.cpp, transaction.cpp)
    State *state() const { return mState.get(); }

private:
    std::shared_ptr<State> mState;
};

/// THE STATEMENT OBSERVER (process-wide, NOQT-DATABASE-1 stage 3): set ONCE, before the first open it should see;
/// every connection opened after it is traced from inside open() - its own pragmas counted - and each statement
/// it runs reaches `observer` with the connection's Options.name (Connection::trace's rules: once per execution, a
/// trigger's sub-statements as their `--` text). Called on the connection's own thread. A second call is ignored
/// (false). THE OBSERVER RUNS NO STATEMENT: it is called inside SQLite's own step, and one would re-enter it. Set
/// it before the connections it should count open (the Database's door: its first initializeDatabase); whether a
/// connection is counted is its observed(), never "an observer is set".
using StatementObserver = std::function<void(std::string_view name, std::string_view sql)>;
bool observeStatements(StatementObserver observer);

/// THE PAGER's COUNTS of an open connection (SQLite's db_status, its current values): pages read from the file
/// (cache misses) and pages written. `ok` false on a connection with no handle or a status SQLite refused: every
/// caller tests `ok` before either number.
struct PagerStats
{
    std::int64_t cacheMisses = 0;
    std::int64_t cacheWrites = 0;
    bool ok = false;
};
PagerStats pagerStats(const Connection &connection);

}   // namespace db

#endif   // DATA_DB_CONNECTION_H
