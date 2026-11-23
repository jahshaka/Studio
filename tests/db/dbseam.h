/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef TESTS_DB_DBSEAM_H
#define TESTS_DB_DBSEAM_H

// THE SUITES' FAILURE SEAM (NOQT-DATABASE-1 stage 3): a read made to FAIL on one connection, test-side only - never a
// product hook. SQLite's authorizer, set on the connection's own handle, denies the actions a case names at PREPARE
// (SQLITE_AUTH: "not authorized"); clearing it lets every statement through again. The one place outside src/data/db
// that reaches a connection's handle (`source.sqlite_one_door`: sqlite3_ lives in src/data/db/ and tests/db/ only).

#include "data/db/connection.h"
#include "data/db/state.h"

#include <cstring>
#include <functional>
#include <string>

namespace dbseam {

/// What a denial looks at: SQLite's authorizer action (SQLITE_READ, SQLITE_PRAGMA, ...) and its first argument (a
/// table for a read, a pragma's name for a pragma).
using Rule = std::function<bool(int action, const char *what)>;

inline Rule &rule()
{
    static Rule r;
    return r;
}

/// Denies every statement `deny` answers true for, on `c` only, until clear(c).
inline void deny(db::Connection &c, Rule deny)
{
    rule() = std::move(deny);
    sqlite3_set_authorizer(c.state()->handle, [](void *, int action, const char *what, const char *, const char *, const char *) {
        return rule() && rule()(action, what) ? SQLITE_DENY : SQLITE_OK;
    }, nullptr);
}

/// Every read of `table` on `c` refused at its prepare.
inline void denyReadsOf(db::Connection &c, const std::string &table)
{
    deny(c, [table](int action, const char *what) { return action == SQLITE_READ && what && table == what; });
}

inline void clear(db::Connection &c)
{
    sqlite3_set_authorizer(c.state()->handle, nullptr, nullptr);
    rule() = Rule();
}

constexpr int kRead = SQLITE_READ;
constexpr int kPragma = SQLITE_PRAGMA;

}   // namespace dbseam

#endif   // TESTS_DB_DBSEAM_H
