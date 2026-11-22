/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DATA_DB_STATEMENT_H
#define DATA_DB_STATEMENT_H

#include "data/db/connection.h"
#include "data/db/error.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3_stmt;

namespace db {

/// One prepared statement: prepared on construction, finalized on destruction, forward only.
/// PARAMETERS are numbered from 1 (SQL's own `?NNN`), or named (`:name`, SQLite's syntax);
/// COLUMNS from 0. Text is UTF-8; an empty text binds '' and an empty blob x'' (never NULL:
/// a NULL is bindNull's alone). Every parameter must be bound before the first step: an
/// unbound one is refused by its name. A bind or prepare failure is kept and answered by step().
class Statement
{
public:
    Statement(Connection &connection, std::string_view sql);
    ~Statement();
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;

    Statement &bindInt(int i, std::int64_t v);
    Statement &bindReal(int i, double v);
    Statement &bindText(int i, std::string_view v);
    Statement &bindBlob(int i, const void *data, std::size_t size);
    Statement &bindNull(int i);
    Statement &bindInt(std::string_view name, std::int64_t v);
    Statement &bindReal(std::string_view name, double v);
    Statement &bindText(std::string_view name, std::string_view v);
    Statement &bindBlob(std::string_view name, const void *data, std::size_t size);
    Statement &bindNull(std::string_view name);

    /// A row? false at the end (e cleared) or on an error (e filled).
    bool step(Error *e);
    /// Back to before the first step; the binds stay.
    void reset();

    bool isNull(int column) const;
    std::int64_t int64(int column) const;
    double real(int column) const;
    std::string text(int column) const;
    std::vector<std::uint8_t> blob(int column) const;

private:
    int indexOf(std::string_view name);
    void bound(int i, int rc);

    Connection::State *mConnection = nullptr;
    sqlite3_stmt *mStatement = nullptr;
    std::string mSql;
    Error mError;                 ///< a prepare or bind failure, answered by step()
    std::vector<bool> mBound;     ///< [i] = parameter i was bound (index 0 unused)
    bool mStarted = false;        ///< stepped since the last reset
};

}   // namespace db

#endif   // DATA_DB_STATEMENT_H
