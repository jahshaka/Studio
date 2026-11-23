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
// connection it opened: open()). Binds by KIND through dbqt.h (QString text, QByteArray bytes, integers, doubles);
// a failed statement answers false / -1 / an empty value - and `error` (when given) says why.

#include "data/database/dbqt.h"
#include "data/db/connection.h"
#include "data/db/statement.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <type_traits>

namespace testsql {

namespace detail {
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
    return db::Connection::open(path.toStdString(), o, &e);
}

/// SQL with no binds and no rows, several statements allowed.
inline bool exec(db::Connection &c, const QString &sql, db::Error *error = nullptr)
{
    db::Error e;
    const bool ok = c.exec(sql.toStdString(), &e);
    if (error) *error = e;
    return ok;
}

/// One statement with its positional binds, run to its end.
template <typename... Args>
bool run(db::Connection &c, const QString &sql, const Args &...binds)
{
    db::Statement s(c, sql.toStdString());
    detail::bindAll(s, binds...);
    db::Error e;
    return s.run(&e);
}

/// The first row's first column as an integer; -1 when there is no row or the read failed.
template <typename... Args>
std::int64_t integer(db::Connection &c, const QString &sql, const Args &...binds)
{
    db::Statement s(c, sql.toStdString());
    detail::bindAll(s, binds...);
    db::Error e;
    return s.step(&e) ? s.int64(0) : -1;
}

/// The first row's first column as text; QString() when there is no row, the read failed or it is NULL.
template <typename... Args>
QString text(db::Connection &c, const QString &sql, const Args &...binds)
{
    db::Statement s(c, sql.toStdString());
    detail::bindAll(s, binds...);
    db::Error e;
    return s.step(&e) ? dbqt::text(s, 0) : QString();
}

/// The first row's first column as bytes; QByteArray() when there is no row, the read failed or it is NULL.
template <typename... Args>
QByteArray bytes(db::Connection &c, const QString &sql, const Args &...binds)
{
    db::Statement s(c, sql.toStdString());
    detail::bindAll(s, binds...);
    db::Error e;
    return s.step(&e) ? dbqt::bytes(s, 0) : QByteArray();
}

/// Every row's first column as text.
template <typename... Args>
QStringList column(db::Connection &c, const QString &sql, const Args &...binds)
{
    db::Statement s(c, sql.toStdString());
    detail::bindAll(s, binds...);
    db::Error e;
    QStringList out;
    while (s.step(&e)) out << dbqt::text(s, 0);
    return out;
}

}   // namespace testsql

#endif   // TESTS_SUPPORT_TESTSQL_H
