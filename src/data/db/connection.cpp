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
#include "data/db/statement.h"

#include <cassert>
#include <cctype>
#include <map>
#include <mutex>
#include <utility>

namespace db {
namespace {

// THE LOCK's HOLDERS (ruling 6): per database file, the connection of this process whose
// outermost Transaction holds the write lock, and its first write. Read by a busy error.
std::mutex gHoldersMutex;
std::map<std::string, std::string> gHolders;

// THE STATEMENT OBSERVER (observeStatements): set once; read by every open() after it.
std::mutex gObserverMutex;
StatementObserver gObserver;

StatementObserver currentObserver()
{
    std::lock_guard<std::mutex> lock(gObserverMutex);
    return gObserver;
}

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

/// The handle is closed and forgotten (its State must not touch it again).
/// Its TRACE goes first (the audit's W2): close_v2 may leave a zombie handle behind an unfinalized statement, and
/// that statement's late step would call the trace with this State, which is about to be freed.
static void closeOwned(Connection::State *s)
{
    if (!s || !s->handle) return;
    if (s->trace) {
        sqlite3_trace_v2(s->handle, 0, nullptr, nullptr);
        s->trace = nullptr;
    }
    sqlite3_close_v2(s->handle);
    s->handle = nullptr;
}

static int traceStatement(unsigned type, void *context, void *, void *sql)
{
    const auto *s = static_cast<const Connection::State *>(context);
    if (type == SQLITE_TRACE_STMT && s->trace) s->trace(sql ? std::string_view(static_cast<const char *>(sql)) : std::string_view());
    return 0;
}

Connection::Connection() : mState(std::make_shared<State>()) {}

Connection::~Connection() { closeOwned(mState.get()); }

Connection::Connection(Connection &&other) noexcept : mState(std::move(other.mState))
{
    other.mState = std::make_shared<State>();
}

Connection &Connection::operator=(Connection &&other) noexcept
{
    if (this == &other) return *this;
    closeOwned(mState.get());
    mState = std::move(other.mState);
    other.mState = std::make_shared<State>();
    return *this;
}

Connection Connection::open(const std::string &path, const Options &options, Error *e)
{
    Connection c;
    c.mState->name = options.name;
    c.mState->owner = std::this_thread::get_id();
    if (!checkVersion(options.minimumVersion, e)) {
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
    c.mState->file = fileOf(handle);
    sqlite3_extended_result_codes(handle, 1);
    sqlite3_busy_timeout(handle, options.busyMs);
    // THE OBSERVER FIRST, so the open's own pragmas are counted under the connection's name.
    if (const StatementObserver observer = currentObserver()) {
        const std::string name = options.name;
        c.trace([observer, name](std::string_view sql) { observer(name, sql); });
        c.mState->observed = true;   // after its own trace: from here trace() is refused
    }
    if (options.wal) {
        // SQLite reports a REFUSED journal mode by its ANSWER, not by an error (a filesystem with no shared memory, a
        // read-only file: it answers the previous mode) - so the answer is read and kept (wal()).
        std::string mode;
        {
            Statement journal(c, "PRAGMA journal_mode = WAL");
            Error failed;
            if (journal.step(&failed)) mode = journal.text(0);
            else if (failed.code) {
                if (e) *e = failed;
                return Connection();
            }
        }
        for (char &ch : mode) ch = char(std::tolower(static_cast<unsigned char>(ch)));
        c.mState->wal = mode == "wal";
        std::string pragmas = "PRAGMA synchronous = NORMAL";
        if (c.mState->wal && options.walAutocheckpoint >= 0)
            pragmas += "; PRAGMA wal_autocheckpoint = " + std::to_string(options.walAutocheckpoint);
        if (!c.exec(pragmas, e)) return Connection();
    }
    if (e) *e = Error();
    return c;
}

bool Connection::isOpen() const { return mState && mState->handle; }

bool Connection::wal() const { return isOpen() && mState->wal; }

bool Connection::observed() const { return isOpen() && mState->observed; }

bool observeStatements(StatementObserver observer)
{
    std::lock_guard<std::mutex> lock(gObserverMutex);
    if (gObserver || !observer) return false;
    gObserver = std::move(observer);
    return true;
}

PagerStats pagerStats(const Connection &connection)
{
    PagerStats out;
    if (!connection.isOpen()) return out;
    sqlite3 *handle = connection.state()->handle;
    int current = 0, highwater = 0;
    if (sqlite3_db_status(handle, SQLITE_DBSTATUS_CACHE_MISS, &current, &highwater, 0) != SQLITE_OK) return out;
    out.cacheMisses = current;
    if (sqlite3_db_status(handle, SQLITE_DBSTATUS_CACHE_WRITE, &current, &highwater, 0) != SQLITE_OK) return out;
    out.cacheWrites = current;
    out.ok = true;
    return out;
}

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

int Connection::guards() const { return mState ? mState->depth : 0; }

std::uint64_t Connection::unit() const { return mState ? mState->unit : 0; }

bool Connection::refusesWrites() const { return mState && mState->refusing; }

bool Connection::trace(std::function<void(std::string_view sql)> onStatement)
{
    detail::assertOwner(mState.get());
    if (!isOpen() || mState->observed) return false;   // the observer's slot is never taken over
    mState->trace = std::move(onStatement);
    if (mState->trace) sqlite3_trace_v2(mState->handle, SQLITE_TRACE_STMT, traceStatement, mState.get());
    else sqlite3_trace_v2(mState->handle, 0, nullptr, nullptr);
    return true;
}

}   // namespace db
