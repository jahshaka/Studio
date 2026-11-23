/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DATA_DB_STATE_H
#define DATA_DB_STATE_H

// The layer's private half (included by src/data/db/*.cpp only): a connection's state and the
// lock's holder table.

#include "data/db/connection.h"

#include <sqlite3.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <thread>

namespace db {

struct Connection::State
{
    sqlite3 *handle = nullptr;      ///< open()'s; closed by the Connection that owns this state
    std::string name;
    std::string file;               ///< the main database's file; empty for a temp or memory database
    std::thread::id owner;          ///< the thread that opened it
    int depth = 0;                  ///< live Transaction guards on this connection
    std::uint64_t unit = 0;         ///< the serial of the outermost unit: a new one each time one opens
    bool holding = false;           ///< this connection's outermost Transaction holds the write lock
    bool firstWriteNamed = false;   ///< the held transaction's first write is in the holder line
    bool refusing = false;          ///< the outermost Transaction's BEGIN failed: query_only until it ends
    bool wal = false;               ///< open()'s journal_mode = WAL was answered "wal"
    bool observed = false;          ///< opened after observeStatements: traced under its name
    std::function<void(std::string_view)> trace;   ///< Connection::trace's callback; SQLite holds `this`

    // THE LAYER GUARD (connection.h): the one mark of a failed read nobody looked at, and where it is held.
    std::shared_ptr<bool> mark;     ///< the unlooked failed read's flag (it stands while *mark is false)
    std::string markText;           ///< "<its statement>: <SQLite's text>", for the refusal
    bool outermostIsScope = false;  ///< the outermost Transaction is a SCOPE (the gesture batch), not a unit
    bool unitFailed = false;        ///< a write of the open UNIT was refused: its commit answers false, rolled back
    std::string unitRefusal;        ///< that refusal's text
    int gestureScopes = 0;          ///< Connection::gestureScope depth: a mark is held to the scope's end
    std::string pending;            ///< refuseWrites: every write refused by this name, never consumed
};

namespace detail {

/// A named error from SQLite's last answer on `s` (rc: the call's result). A busy answer
/// carries the lock's holder in this process.
Error error(const Connection::State *s, int rc, std::string_view sql);
/// A named refusal of the layer itself (no handle, an unbound parameter, an old library).
Error refusal(const Connection::State *s, std::string message, std::string_view sql);
/// The held transaction's first write is named in the holder line (once).
void noteWrite(Connection::State *s, sqlite3_stmt *statement);
/// The holder line of `s`'s file is "<name> (<why>)" from now until releaseLock.
void holdLock(Connection::State *s, std::string_view why);
void releaseLock(Connection::State *s);
/// Debug: the connection is used on the thread that owns it (the threading law).
void assertOwner(const Connection::State *s);
/// THE LAYER GUARD: a READ that failed with `e` (on statement `sql`) marks `s` - unless an unlooked mark already
/// stands (never replaced) - and `e` carries the flag that looking sets.
void markFailedRead(Connection::State *s, Error &e, std::string_view sql);
/// THE LAYER GUARD: a WRITE of `sql` about to run on `s`. True = REFUSED (`*e` the refusal by name): the library
/// is pending a reset, or a failed read's mark stands; the mark is then held (an open unit fails whole; a scope
/// holds it to its end) or consumed (a bare autocommit write).
bool refuseWrite(Connection::State *s, std::string_view sql, Error *e);
/// The mark goes (an outermost unit ended; a gesture scope ended).
void clearMark(Connection::State *s);

}   // namespace detail
}   // namespace db

#endif   // DATA_DB_STATE_H
