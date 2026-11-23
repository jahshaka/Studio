/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "data/db/statement.h"
#include "data/db/state.h"

#include <cctype>

namespace db {

bool isWriteSql(std::string_view sql)
{
    std::size_t at = 0;
    while (at < sql.size() && std::isspace(static_cast<unsigned char>(sql[at]))) ++at;
    std::string head;   // the first seven characters, upper-cased (the funnel's accounting read them so)
    for (; at < sql.size() && head.size() < 7; ++at) head += char(std::toupper(static_cast<unsigned char>(sql[at])));
    for (const std::string_view verb : { "INSERT", "UPDATE", "DELETE", "REPLACE", "CREATE", "DROP", "ALTER" })
        if (std::string_view(head).substr(0, verb.size()) == verb) return true;
    return false;
}

Statement::Statement(Connection &connection, std::string_view sql)
    : mConnection(connection.state()), mSql(sql)
{
    detail::assertOwner(mConnection);
    if (!connection.isOpen()) {
        mError = detail::refusal(mConnection, "the connection has no handle", mSql);
        return;
    }
    const int rc = sqlite3_prepare_v2(mConnection->handle, mSql.data(), int(mSql.size()), &mStatement, nullptr);
    if (rc != SQLITE_OK) {
        mError = detail::error(mConnection, rc, mSql);
        sqlite3_finalize(mStatement);
        mStatement = nullptr;
        return;
    }
    mBound.assign(std::size_t(sqlite3_bind_parameter_count(mStatement)) + 1, false);
}

Statement::~Statement() { sqlite3_finalize(mStatement); }

void Statement::bound(int i, int rc)
{
    if (rc == SQLITE_OK) {
        if (i > 0 && std::size_t(i) < mBound.size()) mBound[std::size_t(i)] = true;
        return;
    }
    if (!mError.code) mError = detail::error(mConnection, rc, mSql);
}

int Statement::indexOf(std::string_view name)
{
    if (!mStatement) return 0;
    const int i = sqlite3_bind_parameter_index(mStatement, std::string(name).c_str());
    if (i == 0 && !mError.code)
        mError = detail::refusal(mConnection, "the statement has no parameter " + std::string(name), mSql);
    return i;
}

Statement &Statement::bindInt(int i, std::int64_t v)
{
    if (mStatement) bound(i, sqlite3_bind_int64(mStatement, i, v));
    return *this;
}

Statement &Statement::bindReal(int i, double v)
{
    if (mStatement) bound(i, sqlite3_bind_double(mStatement, i, v));
    return *this;
}

Statement &Statement::bindText(int i, std::string_view v)
{
    // A null pointer would bind NULL: an empty text is '' (a non-null pointer, length 0).
    if (mStatement)
        bound(i, sqlite3_bind_text64(mStatement, i, v.data() ? v.data() : "", v.size(), SQLITE_TRANSIENT, SQLITE_UTF8));
    return *this;
}

Statement &Statement::bindBlob(int i, const void *data, std::size_t size)
{
    // A null pointer would bind NULL: an empty blob is x'' (a zero-length blob).
    if (mStatement)
        bound(i, size == 0 ? sqlite3_bind_zeroblob(mStatement, i, 0)
                           : sqlite3_bind_blob64(mStatement, i, data, size, SQLITE_TRANSIENT));
    return *this;
}

Statement &Statement::bindNull(int i)
{
    if (mStatement) bound(i, sqlite3_bind_null(mStatement, i));
    return *this;
}

Statement &Statement::bindInt(std::string_view name, std::int64_t v)
{
    if (const int i = indexOf(name)) bindInt(i, v);
    return *this;
}

Statement &Statement::bindReal(std::string_view name, double v)
{
    if (const int i = indexOf(name)) bindReal(i, v);
    return *this;
}

Statement &Statement::bindText(std::string_view name, std::string_view v)
{
    if (const int i = indexOf(name)) bindText(i, v);
    return *this;
}

Statement &Statement::bindBlob(std::string_view name, const void *data, std::size_t size)
{
    if (const int i = indexOf(name)) bindBlob(i, data, size);
    return *this;
}

Statement &Statement::bindNull(std::string_view name)
{
    if (const int i = indexOf(name)) bindNull(i);
    return *this;
}

bool Statement::failed(Error failure, Error *e)
{
    // THE LAYER GUARD's MARK: a READ that failed marks its connection; the Error handed back carries the flag looking
    // sets. A statement whose prepare failed has nothing to ask: its first keyword says whether it would have written
    // (a write that failed to prepare is no read: its own answer is its caller's).
    const bool read = mStatement ? sqlite3_stmt_readonly(mStatement) != 0 : !isWriteSql(mSql);
    if (read) detail::markFailedRead(mConnection, failure, mSql);
    if (e) *e = failure;
    return false;
}

bool Statement::step(Error *e)
{
    detail::assertOwner(mConnection);
    if (mError.code) return failed(mError, e);
    if (!mStarted) {
        for (std::size_t i = 1; i < mBound.size(); ++i) {
            if (mBound[i]) continue;
            const char *name = sqlite3_bind_parameter_name(mStatement, int(i));
            return failed(detail::refusal(mConnection, "parameter " + (name ? std::string(name) : "?" + std::to_string(i))
                                                           + " was never bound", mSql),
                          e);
        }
        // THE LAYER GUARD's REFUSAL: a write while a failed read's mark stands (or the library is pending a reset).
        if (!sqlite3_stmt_readonly(mStatement)) {
            Error refused;
            if (detail::refuseWrite(mConnection, mSql, &refused)) {
                if (e) *e = refused;
                return false;
            }
        }
        mStarted = true;
        detail::noteWrite(mConnection, mStatement);
    }
    const int rc = sqlite3_step(mStatement);
    if (rc == SQLITE_ROW) {
        if (e) *e = Error();
        return true;
    }
    if (rc == SQLITE_DONE) {
        if (e) *e = Error();
        return false;
    }
    return failed(detail::error(mConnection, rc, mSql), e);
}

bool Statement::run(Error *e)
{
    Error local;
    if (mStarted && !mError.code) {
        local = detail::refusal(mConnection, "the statement already ran (reset() runs it again)", mSql);
        if (e) *e = local;
        return false;
    }
    while (step(&local)) {}   // a returned row is discarded
    if (e) *e = local;
    return !local;
}

void Statement::reset()
{
    if (mStatement) sqlite3_reset(mStatement);
    mStarted = false;
}

bool Statement::isNull(int column) const
{
    return !mStatement || sqlite3_column_type(mStatement, column) == SQLITE_NULL;
}

std::int64_t Statement::int64(int column) const { return mStatement ? sqlite3_column_int64(mStatement, column) : 0; }

double Statement::real(int column) const { return mStatement ? sqlite3_column_double(mStatement, column) : 0.0; }

std::string Statement::text(int column) const
{
    if (!mStatement) return std::string();
    const auto *text = reinterpret_cast<const char *>(sqlite3_column_text(mStatement, column));
    return text ? std::string(text, std::size_t(sqlite3_column_bytes(mStatement, column))) : std::string();
}

std::vector<std::uint8_t> Statement::blob(int column) const
{
    if (!mStatement) return {};
    const auto *data = static_cast<const std::uint8_t *>(sqlite3_column_blob(mStatement, column));
    return data ? std::vector<std::uint8_t>(data, data + sqlite3_column_bytes(mStatement, column))
                : std::vector<std::uint8_t>();
}

}   // namespace db
