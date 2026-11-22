/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DATA_DATABASE_DATABASEREADS_H
#define DATA_DATABASE_DATABASEREADS_H

// WHAT THE LIBRARY DATABASE READ ON THE REQUEST PATH (TESTING-CLEANUP-2; app.databaseReads).
//
// SQLite's own pager counts the pages it reads from the file per connection
// (SQLITE_DBSTATUS_CACHE_MISS) whether anyone asks or not; this reads that count off the UI
// thread's DEFAULT connection (the library's request path: worker connections are not in it, so
// a reading taken around one verb is what that verb read, whenever it is taken). Nothing here
// runs unless the verb is called: the product never asks.
//
// THE SAME SQLITE, OR NONE. The connection's handle belongs to the SQLite the Qt driver uses. This
// TU is linked to the SYSTEM SQLite (CMake's SQLite3) and calls it directly — so it answers only
// when the loaded qsqlite plugin is itself linked to that system library (Linux distribution Qt:
// read from the plugin's own dynamic section). Where qsqlite carries its own SQLite (the Qt
// installer's macOS and Windows builds) a call into ours with its handle would be undefined
// behaviour: `available` is false there, and the counted suites skip saying so.

//
// THE STATEMENT TRACE (LARGE-LIBRARY-1). The query log (Database::setQueryLog) sees only the statements
// that pass `executeAndCheckQuery`; raw SQL — `checkIfTableExists`, a service's own QSqlQuery — and the
// boot are invisible to it. SQLite's statement trace (sqlite3_trace_v2, SQLITE_TRACE_STMT) fires once per
// statement EXECUTION on its connection, whoever runs it: `installStatementTrace` registers it on a
// connection's handle, and `read` reports the count of every statement the traced connections ran
// (`statements`; a trigger's sub-statements — text starting `--` — are not counted, and `read`'s own
// `PRAGMA page_size` is not either). Database::initializeDatabase calls it ONLY when the process was
// started with JAH_DB_STATEMENT_TRACE=1 (read once): unset, no callback is registered and nothing is
// paid — `traced` false, `statements` 0. THE STATED LIMIT: only the DEFAULT (UI-thread) connection is
// traced; a statement a worker connection runs is not counted here.

#include <QString>
#include <QtGlobal>

class QSqlDatabase;
struct sqlite3;

namespace databasereads {

struct Stats {
    bool available = false;   ///< the driver's SQLite is the one this TU calls
    QString why;              ///< when not available: the reason, printed by the caller
    qint64 pages = 0;         ///< pages the default connection's pager read from the file
    int pageSize = 0;         ///< the file's page size
    bool traced = false;      ///< the statement trace is registered on the default connection
    qint64 statements = 0;    ///< statements the traced connections ran since the process started
};

Stats read();

/// Registers the statement trace on `connection`'s handle; false (nothing registered) where `read`
/// would answer unavailable.
bool installStatementTrace(const QSqlDatabase &connection);

/// The connection's sqlite3* when it belongs to the system SQLite this build links (the plugin's own
/// DT_NEEDED, read above), else null with the reason in `why`. Database::connection() borrows it.
sqlite3 *systemHandle(const QSqlDatabase &connection, QString *why);

}   // namespace databasereads

#endif   // DATA_DATABASE_DATABASEREADS_H
