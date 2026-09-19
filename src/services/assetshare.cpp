/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/assetshare.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>

#include <limits>

#include "data/constants.h"
#include "data/database/database.h"
#include "data/project.h"
#include "export/exportmanifest.h"
#include "io/clipboardformat.h"
#include "io/ziphelper.h"
#include "scripting/modules/moduleshared.h"
#include "services/assetcas.h"
#include "services/assetclosure.h"
#include "services/assetstorepaths.h"
#include "services/clipboardresolver.h"

namespace {

QString manifestName() { return QStringLiteral("jah.manifest.json"); }
QString payloadName()  { return QStringLiteral("payload.json"); }

}   // namespace

namespace assetshare {

QString fileFilter()
{
    return QStringLiteral("Jahshaka asset bundle (*.%1)").arg(QLatin1String(extension()));
}

ExportResult exportBundle(Database *db, Project *project, const QString &guid,
                          const QString &destPath)
{
    ExportResult result;
    const auto fail = [&result](const QString &why) { result.error = why; return result; };
    if (!db) return fail(QStringLiteral("no library is open"));
    if (guid.isEmpty() || destPath.isEmpty())
        return fail(QStringLiteral("an asset and a destination are required"));

    const AssetRecord row = db->fetchAsset(guid);
    if (row.guid.isEmpty()) return fail(QStringLiteral("no asset '%1'").arg(guid));

    // THE WHOLE CLOSURE, WITH ITS BYTES. The budget is unbounded on purpose:
    // this is the owner's portability rule, not the clipboard's size policy —
    // a share file that referenced content by oid alone would open to nothing
    // on a machine that has never seen it (§5, "Import on an EMPTY library").
    assetclosure::Options options;
    options.inlineLimitBytes = std::numeric_limits<qint64>::max();
    options.includeRowBlobs = true;
    options.storeRoot = AssetStorePaths::root();
    if (project) options.projectGuid = project->getProjectGuid();

    const QStringList closure = assetclosure::expand({ guid }, db);
    int inlined = 0, referenced = 0;
    const QMap<QString, clipboardformat::ClipAsset> assets =
        assetclosure::describe(closure, db, options, &inlined, &referenced);
    if (assets.isEmpty()) return fail(QStringLiteral("'%1' has nothing to carry").arg(row.name));

    clipboardformat::Envelope envelope;
    envelope.version = clipboardformat::kVersion;
    envelope.app = Constants::CONTENT_VERSION;
    envelope.created = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    envelope.source.storeRoot = AssetStorePaths::root();
    AssetCas::readStoreInfo(AssetStorePaths::root(), &envelope.source.storeId, nullptr);
    if (project) envelope.source.projectGuid = project->getProjectGuid();
    envelope.assets = assets;

    // ONE item, and it is the asset the file is ABOUT. The rest of the map is
    // what it is made of; the resolver reads the item to know which guid the
    // import should answer with.
    clipboardformat::ClipItem item;
    item.kind = QLatin1String(clipboardformat::kind::asset());
    item.data = QJsonObject{ { QStringLiteral("guid"), guid },
                             { QStringLiteral("name"), row.name },
                             { QStringLiteral("type"), scriptmod::assetTypeName(row.type) } };
    envelope.items.append(item);

    // THE MANIFEST — the readable header, v2, in the format every other
    // export in this app writes.
    exportformat::ExportManifest manifest;
    manifest.version = 2;
    manifest.kind = scriptmod::assetTypeName(row.type);
    manifest.generator = QStringLiteral("Jahshaka");
    manifest.created = envelope.created;
    for (auto it = assets.constBegin(); it != assets.constEnd(); ++it) {
        exportformat::ManifestAsset entry;
        entry.guid = it.key();
        entry.name = it->name;
        entry.type = it->type;
        entry.typeId = it->typeId;
        entry.dependencies = it->dependencies;
        for (const clipboardformat::ClipFile &file : it->files) {
            exportformat::ManifestFile mf;
            mf.role = file.role;
            mf.name = file.name;
            mf.size = file.size;
            mf.oid = file.oid;
            entry.files.append(mf);
        }
        manifest.assets.append(entry);
    }

    QTemporaryDir staging;
    if (!staging.isValid()) return fail(QStringLiteral("cannot create a staging directory"));
    QString manifestError;
    if (!manifest.write(QDir(staging.path()).filePath(manifestName()), &manifestError))
        return fail(manifestError.isEmpty() ? QStringLiteral("could not write the manifest")
                                            : manifestError);
    {
        QFile payload(QDir(staging.path()).filePath(payloadName()));
        if (!payload.open(QIODevice::WriteOnly))
            return fail(QStringLiteral("could not write the payload"));
        const QByteArray bytes = envelope.toText();
        if (payload.write(bytes) != bytes.size())
            return fail(QStringLiteral("a short write"));
    }

    QString zipError;
    if (!ZipHelper::zipDirectory(staging.path(), destPath, &zipError))
        return fail(zipError.isEmpty() ? QStringLiteral("could not write the archive") : zipError);

    result.path = destPath;
    result.kind = manifest.kind;
    result.assets = assets.size();
    result.bytes = QFileInfo(destPath).size();
    return result;
}

bool looksLikeBundle(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) return false;
    if (QFileInfo(path).suffix().compare(QLatin1String(extension()), Qt::CaseInsensitive) == 0)
        return true;
    // A zip that carries our manifest IS one, whatever it is called — the
    // same tolerance every other reader in this app has for a renamed file.
    QTemporaryDir peek;
    if (!peek.isValid()) return false;
    if (!ZipHelper::extract(path, peek.path())) return false;
    if (!QFile::exists(QDir(peek.path()).filePath(payloadName()))) return false;
    QString error;
    const auto manifest =
        exportformat::ExportManifest::fromFile(QDir(peek.path()).filePath(manifestName()), &error);
    return manifest.version >= 2 && !manifest.assets.isEmpty();
}

ImportResult importBundle(Database *db, Project *project, const QString &path)
{
    ImportResult result;
    const auto fail = [&result](const QString &why) { result.error = why; return result; };
    if (!db) return fail(QStringLiteral("no library is open"));
    if (!QFileInfo::exists(path)) return fail(QStringLiteral("no such file '%1'").arg(path));

    QTemporaryDir staging;
    if (!staging.isValid()) return fail(QStringLiteral("cannot create a staging directory"));
    QString zipError;
    if (!ZipHelper::extract(path, staging.path(), &zipError))
        return fail(zipError.isEmpty() ? QStringLiteral("this file is not a readable archive")
                                       : zipError);

    QFile payloadFile(QDir(staging.path()).filePath(payloadName()));
    if (!payloadFile.exists() || !payloadFile.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("this archive carries no asset payload"));
    QString envelopeError;
    const clipboardformat::Envelope envelope =
        clipboardformat::Envelope::fromText(payloadFile.readAll(), &envelopeError);
    if (envelope.items.isEmpty())
        return fail(envelopeError.isEmpty() ? QStringLiteral("the payload names no asset")
                                            : envelopeError);

    // THE EXISTING IMPORT (spec §5): ingest by content, register the rows
    // with their blobs, write the intrinsic edges, pin into the open project.
    ClipboardResolver resolver(db, project);
    const ClipboardResolveReport report = resolver.apply(envelope);
    if (!report.error.isEmpty()) return fail(report.error);

    const QString wanted = envelope.items.first().data.value(QStringLiteral("guid")).toString();
    const QString landed = report.guidMap.value(wanted, wanted);
    if (landed.isEmpty() || db->fetchAsset(landed).guid.isEmpty()) {
        QString why = QStringLiteral("the asset could not be landed");
        if (!report.missing.isEmpty())
            why = QStringLiteral("the archive is missing content: %1")
                      .arg(report.missing.first().name);
        return fail(why);
    }

    result.guid = landed;
    result.imported = report.imported;
    result.known = report.known;
    return result;
}

}   // namespace assetshare
