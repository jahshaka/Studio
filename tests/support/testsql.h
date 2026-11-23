/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef TESTS_SUPPORT_TESTSQL_H
#define TESTS_SUPPORT_TESTSQL_H

// THE SUITES' OWN STATEMENTS ON THE LAYER (NOQT-DATABASE-1 stage 3: no QtSql in tests/). A suite seeds, pokes and
// reads its throwaway library through these, on the connection it names (the Database's connection(), or a side
// connection it opened: open()). Binds by KIND through dbqt.h (QString text, QByteArray bytes, integers, doubles).
// A FAILED STATEMENT IS NEVER A SENTINEL THAT PASSES: every helper answers its empty value AND COUNTS the failure in
// failures() (said on stdout), and each suite's verdict adds that count - a suite whose own seed or probe failed
// reads FAILED, never a pass on a -1 or an empty list. `column` answers NO rows when a later step failed (never the
// rows before it). The one exception: exec() handed an `error` is a case that expects to see the failure - it is
// that case's, not counted.

#include "data/database/dbqt.h"
#include "data/db/connection.h"
#include "data/db/statement.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace testsql {

/// The suite's own statements that failed (each said on stdout when it failed).
inline int &failureCount()
{
    static int n = 0;
    return n;
}
inline int failures() { return failureCount(); }

namespace detail {
inline void counted(const QString &sql, const db::Error &e)
{
    ++failureCount();
    std::printf("testsql: COUNTED a failed statement of the suite's own: %s (%s)\n", qUtf8Printable(sql.simplified().left(120)),
                e.message.c_str());
    std::fflush(stdout);
}
inline void bindOne(db::Statement &s, int i, const QString &v) { dbqt::bindText(s, i, v); }
inline void bindOne(db::Statement &s, int i, const char *v) { dbqt::bindText(s, i, QString::fromUtf8(v)); }
inline void bindOne(db::Statement &s, int i, const QByteArray &v) { dbqt::bindBytes(s, i, v); }
inline void bindOne(db::Statement &s, int i, double v) { s.bindReal(i, v); }
template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
inline void bindOne(db::Statement &s, int i, T v) { s.bindInt(i, std::int64_t(v)); }

template <typename... Args>
void bindAll(db::Statement &s, const Args &...args)
{
    int i = 0;
    (bindOne(s, ++i, args), ...);
}
}   // namespace detail

/// A side connection to `path` named `name` (the library's busy wait; read-only when asked).
inline db::Connection open(const QString &path, const char *name, bool readOnly = false)
{
    db::Options o;
    o.name = name;
    o.readOnly = readOnly;
    o.busyMs = db::kEditorBusyMs;
    db::Error e;
    db::Connection opened = db::Connection::open(path.toStdString(), o, &e);
    if (!opened.isOpen()) detail::counted(QStringLiteral("open %1").arg(path), e);
    return opened;
}

/// SQL with no binds and no rows, several statements allowed. Handed an `error`, a failure is the caller's to see
/// (not counted).
inline bool exec(db::Connection &c, const QString &sql, db::Error *error = nullptr)
{
    db::Error e;
    const bool ok = c.exec(sql.toStdString(), &e);
    if (error) *error = e;
    else if (!ok) detail::counted(sql, e);
    return ok;
}

/// One statement with its positional binds, run to its end.
template <typename... Args>
bool run(db::Connection &c, const QString &sql, const Args &...binds)
{
    db::Statement s(c, sql.toStdString());
    detail::bindAll(s, binds...);
    db::Error e;
    const bool ok = s.run(&e);
    if (!ok) detail::counted(sql, e);
    return ok;
}

/// The first row's first column as an integer; -1 when there is no row (a failed read: -1 AND counted).
template <typename... Args>
std::int64_t integer(db::Connection &c, const QString &sql, const Args &...binds)
{
    db::Statement s(c, sql.toStdString());
    detail::bindAll(s, binds...);
    db::Error e;
    if (s.step(&e)) return s.int64(0);
    if (e) detail::counted(sql, e);
    return -1;
}

/// The first row's first column as text; QString() for no row or NULL (a failed read: QString() AND counted).
template <typename... Args>
QString text(db::Connection &c, const QString &sql, const Args &...binds)
{
    db::Statement s(c, sql.toStdString());
    detail::bindAll(s, binds...);
    db::Error e;
    if (s.step(&e)) return dbqt::text(s, 0);
    if (e) detail::counted(sql, e);
    return QString();
}

/// The first row's first column as bytes; QByteArray() for no row or NULL (a failed read: AND counted).
template <typename... Args>
QByteArray bytes(db::Connection &c, const QString &sql, const Args &...binds)
{
    db::Statement s(c, sql.toStdString());
    detail::bindAll(s, binds...);
    db::Error e;
    if (s.step(&e)) return dbqt::bytes(s, 0);
    if (e) detail::counted(sql, e);
    return QByteArray();
}

/// Every row's first column as text; a step that FAILED - the first or a later one - answers NO rows, counted.
template <typename... Args>
QStringList column(db::Connection &c, const QString &sql, const Args &...binds)
{
    db::Statement s(c, sql.toStdString());
    detail::bindAll(s, binds...);
    db::Error e;
    QStringList out;
    while (s.step(&e)) out << dbqt::text(s, 0);
    if (e) {
        detail::counted(sql, e);
        return {};
    }
    return out;
}

}   // namespace testsql

#endif   // TESTS_SUPPORT_TESTSQL_H
