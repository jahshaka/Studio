/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DATA_DATABASE_DBQT_H
#define DATA_DATABASE_DBQT_H

// The Database's Qt adapter over the std-only layer (src/data/db/, NOQT-DATABASE-1 ruling 1: the
// Database's API keeps its Qt value types; the layer never sees one). ONE FUNCTION PER KIND, each named
// by its kind (the lead, 2026-11-23: the library spec that retires the Qt types deletes each kind with
// one script line); no conversion is written inline at a call site. The meanings QtSql gave: a NULL
// QString binds NULL, a non-null empty one '' (the live code's `length(...) > 0` and `IS NOT NULL` read
// that difference); a NULL column reads as QString() / QByteArray().

#include "data/db/error.h"
#include "data/db/statement.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <string>
#include <string_view>

namespace dbqt {

/// KIND text, out: a QString as the UTF-8 the layer takes (an SQL text built with Qt).
inline std::string utf8(const QString &v) { return v.toStdString(); }

/// KIND text, bound: QString() binds NULL, "" binds ''.
inline void bindText(db::Statement &s, int i, const QString &v)
{
    if (v.isNull()) { s.bindNull(i); return; }
    const QByteArray utf8 = v.toUtf8();
    s.bindText(i, std::string_view(utf8.constData(), std::size_t(utf8.size())));
}
inline void bindText(db::Statement &s, std::string_view name, const QString &v)
{
    if (v.isNull()) { s.bindNull(name); return; }
    const QByteArray utf8 = v.toUtf8();
    s.bindText(name, std::string_view(utf8.constData(), std::size_t(utf8.size())));
}
/// Bytes are never bound as text (QByteArray converts to QString implicitly): bindBytes.
void bindText(db::Statement &, int, const QByteArray &) = delete;
void bindText(db::Statement &, std::string_view, const QByteArray &) = delete;

/// KIND bytes, bound: a null QByteArray binds NULL (QtSql's meaning), any other its bytes (empty: x'').
inline void bindBytes(db::Statement &s, int i, const QByteArray &v)
{
    if (v.isNull()) { s.bindNull(i); return; }
    s.bindBlob(i, v.constData(), std::size_t(v.size()));
}
inline void bindBytes(db::Statement &s, std::string_view name, const QByteArray &v)
{
    if (v.isNull()) { s.bindNull(name); return; }
    s.bindBlob(name, v.constData(), std::size_t(v.size()));
}

/// KIND time, bound: as QtSql bound a QDateTime - its ISO 8601 text with milliseconds; an invalid one NULL.
inline void bindTime(db::Statement &s, std::string_view name, const QDateTime &v)
{
    if (!v.isValid()) { s.bindNull(name); return; }
    const QByteArray utf8 = v.toString(Qt::ISODateWithMs).toUtf8();
    s.bindText(name, std::string_view(utf8.constData(), std::size_t(utf8.size())));
}

/// KIND text, read: a NULL column is QString().
inline QString text(const db::Statement &s, int column)
{
    return s.isNull(column) ? QString() : QString::fromStdString(s.text(column));
}

/// KIND bytes, read: a NULL column is QByteArray(); a text column reads as its UTF-8.
inline QByteArray bytes(const db::Statement &s, int column)
{
    if (s.isNull(column)) return QByteArray();
    const std::vector<std::uint8_t> v = s.blob(column);
    return QByteArray(reinterpret_cast<const char *>(v.data()), qsizetype(v.size()));
}

/// KIND time, read: the stored `datetime()` text as QVariant::toDateTime read it (ISO 8601); NULL or
/// unreadable is an invalid QDateTime.
inline QDateTime dateTime(const db::Statement &s, int column)
{
    return s.isNull(column) ? QDateTime() : QDateTime::fromString(text(s, column), Qt::ISODateWithMs);
}

/// The error as one log line: its message, its statement and, for a busy lock, its holder.
inline QString describe(const db::Error &e)
{
    QString line = QString::fromStdString(e.message);
    if (!e.statement.empty()) line += QStringLiteral(" in: ") + QString::fromStdString(e.statement);
    if (!e.holder.empty()) line += QStringLiteral("; the lock is held by ") + QString::fromStdString(e.holder);
    return line;
}

}   // namespace dbqt

#endif   // DATA_DATABASE_DBQT_H
