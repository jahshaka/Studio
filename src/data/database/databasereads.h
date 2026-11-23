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
// SQLite's own pager counts the pages it reads from the file per connection (db::pagerStats' cacheMisses) whether
// anyone asks or not; `read` reads that count off the connection it is handed (the verb hands it its Database's:
// the library's request path - a reading taken around one verb is what that verb read on it). Nothing here runs
// unless asked: the product never asks.
//
// THE STATEMENT COUNT, PER CONNECTION NAME (LARGE-LIBRARY-1; NOQT-DATABASE-1 stage 3, ruled call 5). The query log
// (Database::setQueryLog) sees only the statements that pass `executeAndCheckQuery`; raw statements and the boot
// are invisible to it. `observe` sets the layer's one process-wide statement observer (db::observeStatements): every
// connection opened after it - the editor's and every side connection - counts each statement EXECUTION under its
// Options.name (a trigger's sub-statements - text starting `--` - are not counted, nor `read`'s own
// `PRAGMA page_size`). The Database calls `observe` at its FIRST initializeDatabase of the process, ONLY when the
// process was started with JAH_DB_STATEMENT_TRACE=1 (read once): unset, nothing is set and nothing is paid. Two
// Databases open under one name ("editor": the boot check's and the shell's) count as one, as the old global count
// did; the workers' names are printed per name, never summed (statementsOf).

#include <QString>
#include <QtGlobal>

#include <string>

namespace db { class Connection; }

namespace databasereads {

struct Stats {
    bool available = false;   ///< the connection is open and SQLite answered its pager's counts
    QString why;              ///< not available, or not traced: the reason, printed by the caller
    qint64 pages = 0;         ///< pages the connection's pager read from the file
    int pageSize = 0;         ///< the file's page size
    bool traced = false;      ///< the connection was opened after the observer: its statements are counted
    qint64 statements = 0;    ///< statements run under the connection's NAME since the observer was set (traced only)
};

/// The measuring door (the Database's, JAH_DB_STATEMENT_TRACE=1): the per-name counter as the layer's observer.
/// False when another observer was already set (the first one stands).
bool observe();

Stats read(db::Connection &connection);

/// Statements run under `name` since the observer was set (0 for a name nothing ran under).
qint64 statementsOf(const std::string &name);

}   // namespace databasereads

#endif   // DATA_DATABASE_DATABASEREADS_H
