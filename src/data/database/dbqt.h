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
// Database's API keeps its Qt value types; the layer never sees one). The meanings QtSql gave:
// a NULL QString binds NULL, a non-null empty one '' (the live code's `length(...) > 0` and
// `IS NOT NULL` read that difference); a NULL column reads as QString().

#include "data/db/error.h"
#include "data/db/statement.h"

#include <QByteArray>
#include <QString>

namespace dbqt {

inline void bindText(db::Statement &s, int i, const QString &v)
{
    if (v.isNull()) { s.bindNull(i); return; }
    const QByteArray utf8 = v.toUtf8();
    s.bindText(i, std::string_view(utf8.constData(), std::size_t(utf8.size())));
}

inline QString text(const db::Statement &s, int column)
{
    return s.isNull(column) ? QString() : QString::fromStdString(s.text(column));
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
