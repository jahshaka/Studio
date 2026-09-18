/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "app/notices.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace notices {

namespace {

QString resourceText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    return QString::fromUtf8(f.readAll());
}

QVector<Entry> parseManifest()
{
    QVector<Entry> out;
    const QString raw = resourceText(QStringLiteral(":/notices/notices.json"));
    if (raw.isEmpty()) return out;          // a build without the generated qrc
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    const QJsonArray components = doc.object().value(QStringLiteral("components")).toArray();
    for (const QJsonValue &v : components) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.id = o.value(QStringLiteral("id")).toString();
        if (e.id.isEmpty()) continue;
        e.name = o.value(QStringLiteral("name")).toString();
        e.role = o.value(QStringLiteral("role")).toString();
        e.homepage = o.value(QStringLiteral("homepage")).toString();
        e.licence = o.value(QStringLiteral("licence")).toString();
        e.path = o.value(QStringLiteral("path")).toString();
        e.file = o.value(QStringLiteral("file")).toString();
        // PRESENT means "this build carries the component". Only an `optional`
        // entry can be absent, and the build writes a note in place of its text
        // when it is (cmake/Notices.cmake) — so the honest reading is the
        // manifest's own flag, not a guess from the text's length.
        e.present = !o.value(QStringLiteral("optional")).toBool();
        out.append(e);
    }
    return out;
}

}   // namespace

const QVector<Entry> &entries()
{
    // Parsed ONCE: the manifest is embedded and cannot change under a running
    // process, and the About page opens more than once.
    static const QVector<Entry> table = parseManifest();
    return table;
}

QString text(const QString &id)
{
    for (const Entry &e : entries())
        if (e.id == id) return resourceText(QStringLiteral(":/notices/") + id + QStringLiteral(".txt"));
    return QString();
}

}   // namespace notices
