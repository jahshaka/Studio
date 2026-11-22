/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "data/db/connection.h"
#include "data/db/state.h"

#include <cassert>
#include <map>
#include <mutex>
#include <utility>

namespace db {
namespace {

// THE LOCK's HOLDERS (ruling 6): per database file, the connection of this process whose
// outermost Transaction holds the write lock, and its first write. Read by a busy error.
std::mutex gHoldersMutex;
std::map<std::string, std::string> gHolders;

std::string fileOf(sqlite3 *handle)
{
    const char *file = sqlite3_db_filename(handle, "main");
    return file ? std::string(file) : std::string();
}

}   // namespace

namespace detail {

Error error(const Connection::State *s, int rc, std::string_view sql)
{
    Error e;
    e.code = rc;
    if (s && s->handle) {
        const int extended = sqlite3_extended_errcode(s->handle);
        if ((extended & 0xff) == (rc & 0xff)) e.code = extended;
    }
    const std::string name = s ? s->name : std::string();
    e.message = name + ": " + (s && s->handle ? sqlite3_errmsg(s->handle) : sqlite3_errstr(rc))
                + " (code " + std::to_string(e.code) + ")";
    e.statement = std::string(sql);
    if (e.busy()) {
        std::lock_guard<std::mutex> lock(gHoldersMutex);
        const auto it = s ? gHolders.find(s->file) : gHolders.end();
        e.holder = (s && !s->file.empty() && it != gHolders.end())
            ? it->second
            : std::string("no connection of this process holds it (another process, or a read's snapshot)");
    }
    return e;
}

Error refusal(const Connection::State *s, std::string message, std::string_view sql)
{
    Error e;
    e.code = SQLITE_MISUSE;
    e.message = (s && !s->name.empty() ? s->name + ": " : std::string()) + std::move(message);
    e.statement = std::string(sql);
    return e;
}

void noteWrite(Connection::State *s, sqlite3_stmt *statement)
{
    if (!s->holding || s->firstWriteNamed || s->file.empty() || sqlite3_stmt_readonly(statement)) return;
    s->firstWriteNamed = true;
    const char *sql = sqlite3_sql(statement);
    std::lock_guard<std::mutex> lock(gHoldersMutex);
    gHolders[s->file] += std::string(", its first write: ") + (sql ? sql : "");
}

void holdLock(Connection::State *s, std::string_view why)
{
    s->holding = true;
    s->firstWriteNamed = false;
    if (s->file.empty()) return;
    std::lock_guard<std::mutex> lock(gHoldersMutex);
    gHolders[s->file] = s->name + " (" + std::string(why) + ")";
}

void releaseLock(Connection::State *s)
{
    if (!s->holding) return;
    s->holding = false;
    if (s->file.empty()) return;
    std::lock_guard<std::mutex> lock(gHoldersMutex);
    gHolders.erase(s->file);
}

void assertOwner(const Connection::State *s)
{
    assert(!s || !s->handle || s->owner == std::this_thread::get_id());
    (void)s;
}

}   // namespace detail

int libraryVersion() { return sqlite3_libversion_number(); }

bool checkVersion(int minimum, Error *e)
{
    const int have = libraryVersion();
    if (have >= minimum) return true;
    if (e) {
        *e = detail::refusal(nullptr, "SQLite " + std::string(sqlite3_libversion()) + " (" + std::to_string(have)
                                 + ") is older than the library needs (" + std::to_string(minimum) + ")", {});
    }
    return false;
}

Connection::Connection() : mState(std::make_unique<State>()) {}

Connection::~Connection()
{
    if (mState && mState->owned && mState->handle) sqlite3_close_v2(mState->handle);
}

Connection::Connection(Connection &&other) noexcept : mState(std::move(other.mState))
{
    other.mState = std::make_unique<State>();
}

Connection &Connection::operator=(Connection &&other) noexcept
{
    if (this == &other) return *this;
    if (mState && mState->owned && mState->handle) sqlite3_close_v2(mState->handle);
    mState = std::move(other.mState);
    other.mState = std::make_unique<State>();
    return *this;
}

Connection Connection::open(const std::string &path, const Options &options, Error *e)
{
    Connection c;
    c.mState->name = options.name;
    c.mState->owner = std::this_thread::get_id();
    if (!checkVersion(kMinimumVersion, e)) {
        if (e) e->message = c.mState->name + ": " + e->message;
        return c;
    }
    sqlite3 *handle = nullptr;
    const int flags = options.readOnly ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    const int rc = sqlite3_open_v2(path.c_str(), &handle, flags, nullptr);
    if (rc != SQLITE_OK) {
        c.mState->handle = handle;   // its message, then closed
        if (e) *e = detail::error(c.mState.get(), rc, {});
        c.mState->handle = nullptr;
        sqlite3_close_v2(handle);
        return c;
    }
    c.mState->handle = handle;
    c.mState->owned = true;
    c.mState->file = fileOf(handle);
    sqlite3_extended_result_codes(handle, 1);
    sqlite3_busy_timeout(handle, options.busyMs);
    if (options.wal && !c.exec("PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL", e)) return Connection();
    if (e) *e = Error();
    return c;
}

Connection Connection::borrow(sqlite3 *handle, std::string name, Error *e)
{
    Connection c;
    c.mState->name = std::move(name);
    c.mState->owner = std::this_thread::get_id();
    if (!checkVersion(kMinimumVersion, e)) {
        if (e) e->message = c.mState->name + ": " + e->message;
        return c;
    }
    if (!handle) {
        if (e) *e = detail::refusal(c.mState.get(), "no handle to borrow", {});
        return c;
    }
    c.mState->handle = handle;
    c.mState->file = fileOf(handle);
    if (e) *e = Error();
    return c;
}

bool Connection::isOpen() const { return mState && mState->handle; }

const std::string &Connection::name() const
{
    static const std::string none;
    return mState ? mState->name : none;
}

bool Connection::exec(std::string_view sql, Error *e)
{
    detail::assertOwner(mState.get());
    if (!isOpen()) {
        if (e) *e = detail::refusal(mState.get(), "the connection has no handle", sql);
        return false;
    }
    const char *at = sql.data();
    const char *const end = sql.data() + sql.size();
    while (at < end) {
        sqlite3_stmt *statement = nullptr;
        const char *tail = nullptr;
        int rc = sqlite3_prepare_v2(mState->handle, at, int(end - at), &statement, &tail);
        const std::string_view text(at, std::size_t((tail ? tail : end) - at));
        if (rc != SQLITE_OK) {
            if (e) *e = detail::error(mState.get(), rc, text);
            return false;
        }
        at = tail ? tail : end;
        if (!statement) continue;   // whitespace or a comment
        detail::noteWrite(mState.get(), statement);
        while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {}
        if (rc != SQLITE_DONE) {
            if (e) *e = detail::error(mState.get(), rc, text);
            sqlite3_finalize(statement);
            return false;
        }
        sqlite3_finalize(statement);
    }
    if (e) *e = Error();
    return true;
}

std::int64_t Connection::lastInsertRowid() const { return isOpen() ? sqlite3_last_insert_rowid(mState->handle) : 0; }

int Connection::changes() const { return isOpen() ? sqlite3_changes(mState->handle) : 0; }

}   // namespace db
