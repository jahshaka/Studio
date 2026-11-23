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
// thread that opened or borrowed it (asserted in Debug at every statement and transaction).
// Only one BEGIN IMMEDIATE holds a file at a time, by SQLite's own lock; a connection that
// meets it waits its busy timeout and then answers a busy() Error naming the holder.

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

/// Refuses, by name, a library older than `minimum`. open() and borrow() ask it with their floor (kMinimumVersion).
bool checkVersion(int minimum, Error *e);

struct Options
{
    bool readOnly = false;
    int busyMs = 0;          ///< sqlite3_busy_timeout: how long a locked file is waited for
    bool wal = false;        ///< journal_mode WAL + synchronous NORMAL
    std::string name;        ///< the connection's name in every error and holder line
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
    /// Uses a handle someone else opened and will close (the Qt driver's, until the Database
    /// opens its own: ruling 3); the borrowed handle is never closed here. EVERY BORROWER OF ONE
    /// HANDLE SHARES ONE STATE (its transaction depth, its refusal, its holder): two borrowers of
    /// one connection are one connection. A null handle, or a library below `minimumVersion`,
    /// returns a connection with no handle and fills `e`. A BORROWER MUST NOT OUTLIVE THE HANDLE's
    /// OWNER: the state is keyed by the handle's address, so a borrow alive after Qt closes its
    /// connection reads freed memory or lends its state to a new handle at that address (no reach
    /// today: Database::closeDatabase ends its own borrow first; the borrow goes at stage 3).
    static Connection borrow(sqlite3 *handle, std::string name, Error *e, int minimumVersion = kMinimumVersion);

    bool isOpen() const;
    const std::string &name() const;

    /// Runs SQL with no binds and no rows (pragmas, DDL; several statements allowed).
    bool exec(std::string_view sql, Error *e);
    std::int64_t lastInsertRowid() const;
    int changes() const;
    /// Live Transaction guards on this connection (0: the next one is the outermost).
    int guards() const;
    /// The outermost Transaction's BEGIN failed and its scope still stands: writes are refused.
    bool refusesWrites() const;
    /// THE STATEMENT TRACE (stage 2; the library.statements counts): `onStatement` is called with the text of
    /// EVERY statement run on this connection, once per execution, at its first step (SQLite's
    /// SQLITE_TRACE_STMT; a trigger's sub-statements arrive as their `--` comment text). The trace is the
    /// handle's: every borrower of one handle shares it. An empty function removes it; so does the end of
    /// the connection (the last borrower's, for a borrowed handle).
    void trace(std::function<void(std::string_view sql)> onStatement);

    struct State;   // the layer's own (connection.cpp, statement.cpp, transaction.cpp)
    State *state() const { return mState.get(); }

private:
    std::shared_ptr<State> mState;
};

}   // namespace db

#endif   // DATA_DB_CONNECTION_H
