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

namespace db {

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
    if (!mError) mError = detail::error(mConnection, rc, mSql);
}

int Statement::indexOf(std::string_view name)
{
    if (!mStatement) return 0;
    const int i = sqlite3_bind_parameter_index(mStatement, std::string(name).c_str());
    if (i == 0 && !mError)
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

bool Statement::step(Error *e)
{
    detail::assertOwner(mConnection);
    if (mError) {
        if (e) *e = mError;
        return false;
    }
    if (!mStarted) {
        for (std::size_t i = 1; i < mBound.size(); ++i) {
            if (mBound[i]) continue;
            const char *name = sqlite3_bind_parameter_name(mStatement, int(i));
            if (e) {
                *e = detail::refusal(mConnection, "parameter " + (name ? std::string(name) : "?" + std::to_string(i))
                                                      + " was never bound", mSql);
            }
            return false;
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
    if (e) *e = detail::error(mConnection, rc, mSql);
    return false;
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
