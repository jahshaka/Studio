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

LegacyStoreContentSource::LegacyStoreContentSource(const QString &storeRoot, bool computeHashes,
                                                   const QString &fallbackDir)
    : root(storeRoot), hashFiles(computeHashes), fallback(fallbackDir)
{
}

QVector<ExportContentSource::Entry> LegacyStoreContentSource::filesForAsset(const QString &guid,
                                                                            const QString &nameHint)
{
    QVector<Entry> entries;
    if (root.isEmpty() || guid.isEmpty()) return entries;

    const QDir folder(QDir(root).filePath(guid));
    if (!folder.exists()) {
        // Name-keyed flat-folder fallback (project rows) — see the header.
        if (!fallback.isEmpty() && !nameHint.isEmpty()) {
            const QFileInfo fi(QDir(fallback).filePath(nameHint));
            if (fi.isFile() && fi.isReadable()) {
                Entry e;
                e.role = QStringLiteral("source");
                e.name = fi.fileName();
                e.path = fi.absoluteFilePath();
                e.size = fi.size();
                if (hashFiles) e.oid = sha256Hex(e.path);
                entries.append(e);
            }
        }
        return entries;   // DB-only row: zero files, not an error
    }

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
