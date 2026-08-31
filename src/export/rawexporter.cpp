/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "export/rawexporter.h"

#include "export/exportcontentsource.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

namespace {

// "name.ext" -> "name 1.ext", "name 2.ext", … (the project-folder convention).
QString dedupedName(const QString &name, int attempt)
{
    if (attempt == 0) return name;
    const QFileInfo fi(name);
    const QString base = fi.completeBaseName();
    const QString suffix = fi.suffix();
    return suffix.isEmpty() ? QStringLiteral("%1 %2").arg(base).arg(attempt)
                            : QStringLiteral("%1 %2.%3").arg(base).arg(attempt).arg(suffix);
}

} // namespace

RawExporter::Result RawExporter::exportAssets(const QVector<AssetInfo> &assets,
                                              ExportContentSource &source,
                                              const QString &destDir,
                                              const QString &kind,
                                              bool copyFiles)
{
    Result res;
    if (destDir.isEmpty()) {
        res.error = QStringLiteral("no destination directory");
        return res;
    }
    if (assets.isEmpty()) {
        res.error = QStringLiteral("nothing to export");
        return res;
    }

    QDir dir(destDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        res.error = QStringLiteral("cannot create %1").arg(destDir);
        return res;
    }
    res.dir = dir.absolutePath();

    exportformat::ExportManifest manifest;
    manifest.kind = kind;
    manifest.generator = QStringLiteral("Jahshaka");
    manifest.created = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QHash<QString, QString> nameForOid;  // oid -> written name (content dedup)
    QHash<QString, int> nameUses;        // written-name collision counter

    for (const AssetInfo &info : assets) {
        exportformat::ManifestAsset ma;
        ma.guid = info.guid;
        ma.name = info.name;
        ma.type = info.type;
        ma.typeId = info.typeId;
        ma.dependencies = info.dependencies;

        const auto entries = source.filesForAsset(info.guid, info.name);
        for (const ExportContentSource::Entry &e : entries) {
            exportformat::ManifestFile mf;
            mf.role = e.role;
            mf.size = e.size;
            mf.oid = e.oid;

            if (!copyFiles) {
                // Manifest-only: describe the stored bytes, materialize nothing.
                mf.name = e.name;
                ma.files.append(mf);
                res.totalBytes += e.size > 0 ? e.size : 0;
                continue;
            }

            // Same bytes already written? Reference the existing file.
            if (!e.oid.isEmpty() && nameForOid.contains(e.oid)) {
                mf.name = nameForOid.value(e.oid);
                ma.files.append(mf);
                continue;
            }

            QString written = e.name;
            for (int attempt = 0; nameUses.contains(written.toLower()); ++attempt)
                written = dedupedName(e.name, attempt + 1);

            const QString destPath = dir.filePath(written);
            if (!QFile::copy(e.path, destPath)) {
                res.warnings.append(
                    QStringLiteral("could not copy %1 (asset %2)").arg(e.path, info.guid));
                continue;
            }
            nameUses.insert(written.toLower(), 1);
            if (!e.oid.isEmpty()) nameForOid.insert(e.oid, written);

            mf.name = written;
            ma.files.append(mf);
            res.exportedFiles.append(written);
            res.totalBytes += e.size > 0 ? e.size : QFileInfo(destPath).size();
        }

        manifest.assets.append(ma);
        ++res.assetCount;
    }

    res.manifestPath = dir.filePath(exportformat::manifestFileName());
    QString merror;
    if (!manifest.write(res.manifestPath, &merror)) {
        res.error = merror;
        return res;
    }

    res.ok = true;
    return res;
}
