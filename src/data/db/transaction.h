/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DATA_DB_TRANSACTION_H
#define DATA_DB_TRANSACTION_H

#include "data/db/connection.h"
#include "data/db/error.h"

#include <string>
#include <string_view>

namespace db {

/// A unit of writes: the OUTERMOST guard on a connection begins IMMEDIATE (the write lock taken
/// here, its holder named "<connection> (<why>)" plus its first write, for any connection that
/// meets the lock: ruling 6); a guard nested inside another on the same connection rides it
/// (its commit answers true, its rollback does nothing). Rolled back at scope end unless
/// committed; a failed commit rolls back. A BEGIN THAT FAILS writes nothing: the scope REFUSES
/// every write on the connection (PRAGMA query_only) until it ends, and its commit answers false.
class Transaction
{
public:
    Transaction(Connection &connection, std::string_view why);
    ~Transaction();
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;

    bool commit(Error *e);
    void rollback();

    /// Why the outermost BEGIN failed (code 0 when it did not).
    const Error &beginError() const { return mBeginError; }

private:
    void end();

    Connection::State *mConnection = nullptr;
    bool mOutermost = false;
    bool mActive = false;      ///< the outermost BEGIN succeeded and the unit is not over
    bool mRefusing = false;    ///< the outermost BEGIN failed: query_only until the scope ends
    bool mCounted = false;     ///< this guard is in the connection's depth
    Error mBeginError;
};

}   // namespace db

#endif   // DATA_DB_TRANSACTION_H
