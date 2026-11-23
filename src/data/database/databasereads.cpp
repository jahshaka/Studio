/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "data/database/databasereads.h"
#include "data/db/connection.h"
#include "data/db/statement.h"

#include <map>
#include <mutex>
#include <string_view>

namespace databasereads {
namespace {

/// The per-name counts, fed by the observer on each connection's own thread.
struct Counts
{
    std::mutex mutex;
    std::map<std::string, qint64, std::less<>> byName;
};

Counts &counts()
{
    static Counts c;
    return c;
}

thread_local bool tOwnStatement = false;   // `read`'s own PRAGMA, on the thread that runs it

void onStatement(std::string_view name, std::string_view sql)
{
    // The observer RUNS NO STATEMENT (it is called inside SQLite's own step): it counts and returns.
    if (tOwnStatement) return;
    if (sql.size() >= 2 && sql[0] == '-' && sql[1] == '-') return;   // a trigger's sub-statement
    Counts &c = counts();
    std::lock_guard<std::mutex> lock(c.mutex);
    ++c.byName[std::string(name)];
}

}   // namespace

bool observe()
{
    return db::observeStatements(onStatement);
}

qint64 statementsOf(const std::string &name)
{
    Counts &c = counts();
    std::lock_guard<std::mutex> lock(c.mutex);
    const auto it = c.byName.find(name);
    return it == c.byName.end() ? 0 : it->second;
}

Stats read(db::Connection &connection)
{
    Stats out;
    const db::PagerStats pager = db::pagerStats(connection);
    if (!pager.ok) {
        out.why = connection.isOpen() ? QStringLiteral("SQLite refused the connection's pager counts")
                                      : QStringLiteral("the library database is not open");
        return out;
    }
    out.available = true;
    out.pages = pager.cacheMisses;
    out.traced = connection.observed();
    if (out.traced) {
        out.statements = statementsOf(connection.name());
    } else {
        out.why = QStringLiteral("connection '%1' was opened before the statement observer (or with "
                                 "JAH_DB_STATEMENT_TRACE unset): its statements are not counted")
                      .arg(QString::fromStdString(connection.name()));
    }
    tOwnStatement = true;
    {
        db::Statement pageSize(connection, "PRAGMA page_size");
        db::Error e;
        if (pageSize.step(&e)) out.pageSize = int(pageSize.int64(0));
    }
    tOwnStatement = false;
    return out;
}

}   // namespace databasereads
