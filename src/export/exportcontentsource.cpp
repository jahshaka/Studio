/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "export/exportcontentsource.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "services/assetstorepaths.h"

namespace {

QString sha256Hex(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) return QString();
    // Lowercase hex: case-insensitive filesystems (APFS) must never alias
    // objects — preflight risk #3.
    return QString::fromLatin1(hash.result().toHex()).toLower();
}

} // namespace

CasContentSource::CasContentSource(const QString &storeRoot, const QString &projectGuid)
    : root(storeRoot), project(projectGuid)
{
}

QVector<ExportContentSource::Entry> CasContentSource::filesForAsset(const QString &guid,
                                                                    const QString &nameHint)
{
    Q_UNUSED(nameHint);   // the catalog IS the name authority
    QVector<Entry> entries;
    if (guid.isEmpty()) return entries;

    QSqlDatabase conn = QSqlDatabase::database();

    // The project's pinned source, when exporting in project context.
    QString pin;
    if (!project.isEmpty()) {
        QSqlQuery pinQuery(conn);
        pinQuery.prepare("SELECT oid_pin FROM project_assets WHERE project_guid = ? AND asset_guid = ?");
        pinQuery.addBindValue(project);
        pinQuery.addBindValue(guid);
        if (pinQuery.exec() && pinQuery.next()) pin = pinQuery.value(0).toString();
    }

    QSqlQuery files(conn);
    files.prepare("SELECT AF.role, AF.name, AF.oid, F.size, F.ext FROM asset_files AF "
                  "LEFT JOIN files F ON AF.oid = F.oid WHERE AF.asset_guid = ? "
                  "ORDER BY AF.role, AF.name");
    files.addBindValue(guid);
    files.exec();
    while (files.next()) {
        Entry e;
        e.role = files.value(0).toString();
        e.name = files.value(1).toString();
        e.oid = files.value(2).toString();
        e.size = files.value(3).toLongLong();
        QString ext = files.value(4).toString();

        if (e.role == QStringLiteral("source") && !pin.isEmpty() && pin != e.oid) {
            QSqlQuery pinned(conn);
            pinned.prepare("SELECT size, ext FROM files WHERE oid = ?");
            pinned.addBindValue(pin);
            if (pinned.exec() && pinned.next()) {
                e.oid = pin;
                e.size = pinned.value(0).toLongLong();
                ext = pinned.value(1).toString();
            }
        }

        e.path = AssetStorePaths::objectPathIn(root, e.oid, ext);
        if (!QFileInfo::exists(e.path)) continue;   // offline/purged object
        entries.append(e);
    }
    return entries;
}

LegacyStoreContentSource::LegacyStoreContentSource(const QString &storeRoot, bool computeHashes)
    : root(storeRoot), hashFiles(computeHashes)
{
}

QVector<ExportContentSource::Entry> LegacyStoreContentSource::filesForAsset(const QString &guid,
                                                                            const QString &nameHint)
{
    QVector<Entry> entries;
    if (root.isEmpty() || guid.isEmpty()) return entries;

    Q_UNUSED(nameHint);
    const QDir folder(QDir(root).filePath(guid));
    if (!folder.exists()) return entries;   // DB-only row: zero files, not an error

    // Stable order (name-sorted) so manifests are deterministic run to run.
    const auto infos = folder.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
    entries.reserve(infos.size());
    for (const QFileInfo &fi : infos) {
        Entry e;
        e.role = QStringLiteral("source");
        e.name = fi.fileName();
        e.path = fi.absoluteFilePath();
        e.size = fi.size();
        if (hashFiles) e.oid = sha256Hex(e.path);
        entries.append(e);
    }
    return entries;
}
