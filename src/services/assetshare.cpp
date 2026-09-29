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
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <limits>

#include "data/constants.h"
#include "data/database/database.h"
#include "data/project.h"
#include "io/clipboardformat.h"
#include "io/ziphelper.h"
#include "zip.h"
#include "scripting/modules/moduleshared.h"
#include "services/assetcas.h"
#include "services/assetclosure.h"
#include "io/assetrefs.h"
#include "irisgl/core/irisutils.h"
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

namespace {

/// The envelope's header, the same for both exports.
clipboardformat::Envelope newEnvelope(Project *project)
{
    clipboardformat::Envelope envelope;
    envelope.version = clipboardformat::kVersion;
    envelope.app = Constants::CONTENT_VERSION;
    envelope.created = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    envelope.source.storeRoot = AssetStorePaths::root();
    AssetCas::readStoreInfo(AssetStorePaths::root(), &envelope.source.storeId, nullptr);
    if (project) envelope.source.projectGuid = project->getProjectGuid();
    return envelope;
}

/// THE WHOLE CLOSURE, WITH ITS BYTES. The budget is unbounded on purpose:
/// this is the owner's portability rule, not the clipboard's size policy — a
/// share file that referenced content by oid alone would open to nothing on a
/// machine that has never seen it (§5, "Import on an EMPTY library").
assetclosure::Options shareOptions(Project *project)
{
    assetclosure::Options options;
    options.inlineLimitBytes = std::numeric_limits<qint64>::max();
    options.includeRowBlobs = true;
    options.storeRoot = AssetStorePaths::root();
    if (project) options.projectGuid = project->getProjectGuid();
    return options;
}

}   // namespace

BundleStage stageBundle(Database *db, Project *project, const QStringList &guids)
{
    BundleStage stage;
    const auto fail = [&stage](const QString &why) { stage.error = why; return stage; };
    if (!db) return fail(QStringLiteral("no library is open"));

    // THE ENTRY SET, in the caller's order, each once.
    QStringList entries;
    for (const QString &guid : guids)
        if (!guid.isEmpty() && !entries.contains(guid)) entries.append(guid);
    if (entries.isEmpty()) return fail(QStringLiteral("an asset is required"));

    clipboardformat::Envelope envelope = newEnvelope(project);
    QString singleKind;
    for (const QString &guid : entries) {
        const AssetRecord row = db->fetchAsset(guid);
        if (row.guid.isEmpty()) return fail(QStringLiteral("no asset '%1'").arg(guid));
        // ONE item per entry: the assets the file is ABOUT. The rest of the
        // map is what they are made of; the resolver reads the items to know
        // which guids the import answers with.
        clipboardformat::ClipItem item;
        item.kind = QLatin1String(clipboardformat::kind::asset());
        item.data = QJsonObject{ { QStringLiteral("guid"), guid },
                                 { QStringLiteral("name"), row.name },
                                 { QStringLiteral("type"), scriptmod::assetTypeName(row.type) } };
        envelope.items.append(item);
        if (entries.size() == 1) singleKind = scriptmod::assetTypeName(row.type);
    }

    // The rows, the edges and the store paths — queries, on this thread. The
    // BYTES are owed (`reads`) and read by the writer.
    const QStringList closure = assetclosure::expand(entries, db);
    envelope.assets = assetclosure::describe(closure, db, shareOptions(project), nullptr, nullptr,
                                             &stage.reads);
    if (envelope.assets.isEmpty()) return fail(QStringLiteral("nothing to carry"));

    stage.envelope = envelope;
    // A single asset's manifest kind is its own type word; a set of them is a
    // PACK (the Assets tray's multi-selection export).
    stage.kind = entries.size() == 1 ? singleKind : QStringLiteral("pack");
    return stage;
}

BundleStage stageNode(Database *db, Project *project, const QJsonObject &nodeObject,
                      const QString &name, int typeId)
{
    BundleStage stage;
    const auto fail = [&stage](const QString &why) { stage.error = why; return stage; };
    if (!db) return fail(QStringLiteral("no library is open"));
    if (nodeObject.isEmpty()) return fail(QStringLiteral("a node is required"));

    // What the node is made of: the key-aware walk of its references (the
    // clipboard's own table, io/assetrefs.h) and everything THOSE depend on.
    const QStringList closure = assetclosure::forNodes({ nodeObject }, db);
    QMap<QString, clipboardformat::ClipAsset> assets =
        assetclosure::describe(closure, db, shareOptions(project), nullptr, nullptr, &stage.reads);

    // THE NODE BECOMES THE ROW THE FILE IS ABOUT: an Object (or ParticleSystem)
    // row whose `asset` blob is the node — the shape every Object row has and
    // `assets.addToScene` instantiates — minted here, under a fresh guid, so an
    // import is one new library tile however many times the file is opened.
    const QString rowGuid = IrisUtils::generateGUID();
    clipboardformat::ClipAsset row;
    row.guid = rowGuid;
    row.name = name.isEmpty() ? QStringLiteral("Node") : name;
    row.typeId = typeId;
    row.type = scriptmod::assetTypeName(typeId);
    row.viewFilter = static_cast<int>(AssetViewFilter::AssetsView);
    row.blob = QJsonDocument(nodeObject).toJson(QJsonDocument::Compact);
    for (const QString &ref : assetrefs::collectAssetGuids(nodeObject))
        if (assets.contains(ref) && !row.dependencies.contains(ref)) row.dependencies.append(ref);
    // THE CLOSURE HANGS UNDER THE NEW ROW. A member row's `parent` names the
    // asset it belongs to — for a placed model, the library Object it was
    // imported as, which is not in the file. Left as it was, every member
    // would land under a guid the receiving library does not have; under the
    // new row it is that row's bundle, which is what it is.
    for (auto it = assets.begin(); it != assets.end(); ++it)
        if (!it->parent.isEmpty() && !assets.contains(it->parent)) it->parent = rowGuid;
    assets.insert(rowGuid, row);

    clipboardformat::Envelope envelope = newEnvelope(project);
    envelope.assets = assets;
    clipboardformat::ClipItem item;
    item.kind = QLatin1String(clipboardformat::kind::asset());
    item.data = QJsonObject{ { QStringLiteral("guid"), rowGuid },
                             { QStringLiteral("name"), row.name },
                             { QStringLiteral("type"), row.type } };
    envelope.items.append(item);
    stage.envelope = envelope;
    stage.kind = row.type;
    return stage;
}

ExportResult exportBundle(Database *db, Project *project, const QStringList &guids,
                          const QString &destPath)
{
    return writeBundle(stageBundle(db, project, guids), destPath);
}

ExportResult exportNode(Database *db, Project *project, const QJsonObject &nodeObject,
                        const QString &name, int typeId, const QString &destPath)
{
    return writeBundle(stageNode(db, project, nodeObject, name, typeId), destPath);
}

bool looksLikeBundle(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) return false;
    if (QFileInfo(path).suffix().compare(QLatin1String(extension()), Qt::CaseInsensitive) == 0)
        return true;

    // A zip that carries our two files IS one, whatever it is called — the
    // same tolerance every other reader here has for a renamed file. IT IS
    // READ FROM THE CENTRAL DIRECTORY, never extracted (fix round F9): this
    // runs on EVERY `assets.import`, including one aimed at a 2 GB model, and
    // it used to unpack whatever it was given into a QTemporaryDir — which on
    // this box is RAM (the 2026-09-08 crash was 26 GB of scratch in tmpfs).
    // The importers' size caps are downstream of this call and could not
    // help. Names and sizes only: nothing is decompressed.
    struct zip_t *zip = zip_open(path.toUtf8().constData(), 0, 'r');
    if (!zip) return false;
    bool haveManifest = false, havePayload = false;
    const ssize_t total = zip_entries_total(zip);
    for (ssize_t i = 0; i < total && !(haveManifest && havePayload); ++i) {
        if (zip_entry_openbyindex(zip, static_cast<size_t>(i)) != 0) continue;
        const char *name = zip_entry_name(zip);
        if (name) {
            const QString entry = QString::fromUtf8(name);
            if (entry == manifestName()) haveManifest = true;
            else if (entry == payloadName()) havePayload = true;
        }
        zip_entry_close(zip);
    }
    zip_close(zip);
    return haveManifest && havePayload;
}

UnpackedBundle unpackBundle(const QString &path)
{
    UnpackedBundle out;
    if (!QFileInfo::exists(path)) {
        out.error = QStringLiteral("no such file '%1'").arg(path);
        return out;
    }
    QTemporaryDir staging;
    if (!staging.isValid()) {
        out.error = QStringLiteral("cannot create a staging directory");
        return out;
    }
    QString zipError;
    if (!ZipHelper::extract(path, staging.path(), &zipError)) {
        out.error = zipError.isEmpty() ? QStringLiteral("this file is not a readable archive")
                                       : zipError;
        return out;
    }
    QFile payloadFile(QDir(staging.path()).filePath(payloadName()));
    if (!payloadFile.exists() || !payloadFile.open(QIODevice::ReadOnly)) {
        out.error = QStringLiteral("this archive carries no asset payload");
        return out;
    }
    QString envelopeError;
    out.envelope = clipboardformat::Envelope::fromText(payloadFile.readAll(), &envelopeError);
    if (out.envelope.items.isEmpty())
        out.error = envelopeError.isEmpty() ? QStringLiteral("the payload names no asset")
                                            : envelopeError;
    return out;
}

ImportResult importBundle(Database *db, Project *project, const QString &path)
{
    ImportResult result;
    const auto fail = [&result](const QString &why) { result.error = why; return result; };
    if (!db) return fail(QStringLiteral("no library is open"));
    if (!QFileInfo::exists(path)) return fail(QStringLiteral("no such file '%1'").arg(path));

    const UnpackedBundle unpacked = unpackBundle(path);
    if (!unpacked.error.isEmpty()) return fail(unpacked.error);
    const clipboardformat::Envelope &envelope = unpacked.envelope;

    // THE EXISTING IMPORT (spec §5): ingest by content, register the rows
    // with their blobs, write the intrinsic edges, pin into the open project.
    ClipboardResolver resolver(db, project);
    const ClipboardResolveReport report = resolver.apply(envelope);
    if (!report.error.isEmpty()) return fail(report.error);

    // EVERY ENTRY the file is about (a pack names several), in its order.
    for (const clipboardformat::ClipItem &item : envelope.items) {
        const QString wanted = item.data.value(QStringLiteral("guid")).toString();
        const QString landed = report.guidMap.value(wanted, wanted);
        if (landed.isEmpty() || db->fetchAsset(landed).guid.isEmpty()) {
            QString why = QStringLiteral("the asset could not be landed");
            if (!report.missing.isEmpty())
                why = QStringLiteral("the archive is missing content: %1")
                          .arg(report.missing.first().name);
            return fail(why);
        }
        result.guids.append(landed);
    }

    result.guid = result.guids.first();
    result.imported = report.imported;
    result.known = report.known;
    result.alreadyHad = report.known.contains(result.guid) && !report.imported.contains(result.guid);
    return result;
}

}   // namespace assetshare
