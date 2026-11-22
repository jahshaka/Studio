/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "data/db/transaction.h"
#include "data/db/state.h"

namespace db {

Transaction::Transaction(Connection &connection, std::string_view why) : mConnection(connection.state())
{
    detail::assertOwner(mConnection);
    if (!connection.isOpen()) {
        mBeginError = detail::refusal(mConnection, "the connection has no handle", {});
        mOutermost = true;
        return;
    }
    mOutermost = mConnection->depth == 0;
    ++mConnection->depth;
    mCounted = true;
    if (!mOutermost) return;   // nested: it rides the outer transaction
    // IMMEDIATE: the write lock is taken HERE, so no other connection commits inside this unit and
    // its snapshot never goes stale (a DEFERRED read-then-write fails its first write with 517
    // SQLITE_BUSY_SNAPSHOT once another connection committed: PREVIEWS-1).
    Error e;
    if (connection.exec("BEGIN IMMEDIATE", &e)) {
        mActive = true;
        detail::holdLock(mConnection, why);
        return;
    }
    mBeginError = e;
    mRefusing = true;
    Error ignored;
    connection.exec("PRAGMA query_only = ON", &ignored);
}

Transaction::~Transaction()
{
    rollback();
    if (mCounted) --mConnection->depth;
}

void Transaction::end()
{
    detail::releaseLock(mConnection);
    mActive = false;
}

bool Transaction::commit(Error *e)
{
    if (!mOutermost) {
        if (e) *e = Error();
        return true;   // nested: the outer guard's commit is the unit's
    }
    if (mRefusing || !mActive) {
        if (e) *e = mBeginError.code ? mBeginError : detail::refusal(mConnection, "the transaction is over", "COMMIT");
        rollback();
        return false;
    }
    const int rc = sqlite3_exec(mConnection->handle, "COMMIT", nullptr, nullptr, nullptr);
    if (rc == SQLITE_OK) {
        end();
        if (e) *e = Error();
        return true;
    }
    if (e) *e = detail::error(mConnection, rc, "COMMIT");
    rollback();
    return false;
}

void Transaction::rollback()
{
    if (!mOutermost) return;
    if (mRefusing) {
        mRefusing = false;
        sqlite3_exec(mConnection->handle, "PRAGMA query_only = OFF", nullptr, nullptr, nullptr);
        return;
    }
    if (!mActive) return;
    sqlite3_exec(mConnection->handle, "ROLLBACK", nullptr, nullptr, nullptr);
    end();
}

}   // namespace db
