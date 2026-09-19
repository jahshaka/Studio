/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/materialbundle.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"

namespace {

/// The definition's file name inside the store. One name per asset, so a
/// re-save of the SAME material rewrites its own asset_files row instead of
/// accumulating one row per version (the PK is (guid, role, name)).
QString definitionFileName()
{
    return QStringLiteral("material.json");
}

QJsonObject parseDefinition(const QByteArray &bytes)
{
    if (bytes.isEmpty()) return QJsonObject();
    return QJsonDocument::fromJson(bytes).object();
}

} // namespace

namespace MaterialBundle {

const QSet<QString> &textureSlots()
{
    // Built ONCE from the material's own property list — including the twelve
    // generated detail-layer rows, which is exactly what a hand-written list
    // would have missed.
    static const QSet<QString> slotNames = [] {
        QSet<QString> out;
        auto material = iris::PbrMaterial::create();
        for (auto *prop : material->properties)
            if (prop && prop->type == iris::PropertyType::Texture)
                out.insert(prop->name);
        return out;
    }();
    return slotNames;
}

bool looksLikePath(const QString &value)
{
    if (value.isEmpty()) return false;
    if (value.contains(QLatin1Char('/')) || value.contains(QLatin1Char('\\')))
        return true;
    // An asset guid has no extension; every image file does.
    return !QFileInfo(value).suffix().isEmpty();
}

QString offendingPath(const QJsonObject &definition, QString *slotOut)
{
    const auto report = [slotOut](const QString &slot, const QString &value) {
        if (slotOut) *slotOut = slot;
        return value;
    };

    const QJsonObject values = definition.value(QStringLiteral("values")).toObject();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (!textureSlots().contains(it.key())) continue;
        const QString value = it.value().toString();
        if (looksLikePath(value)) return report(it.key(), value);
    }

    // THE GRAPH IS WHERE THE PATH GOT IN (F3, §1.3): the evaluator is handed a
    // resolver that turns a guid into a path, and for a moment the RESOLVED
    // value was what got serialized. A texture node carries a guid or nothing.
    const QJsonArray nodes =
        definition.value(QStringLiteral("shadergraph")).toObject()
                  .value(QStringLiteral("nodes")).toArray();
    for (const auto &entry : nodes) {
        const QJsonObject node = entry.toObject();
        if (node.value(QStringLiteral("type")).toString() != QLatin1String("texture"))
            continue;
        const QString value = node.value(QStringLiteral("value")).toString();
        if (looksLikePath(value))
            return report(QStringLiteral("shadergraph.%1").arg(
                              node.value(QStringLiteral("id")).toString()),
                          value);
    }
    return QString();
}

QStringList memberGuids(const QJsonObject &definition)
{
    QStringList out;
    const auto add = [&out](const QString &guid) {
        if (guid.isEmpty() || looksLikePath(guid)) return;
        if (!out.contains(guid)) out.append(guid);
    };

    const QJsonObject values = definition.value(QStringLiteral("values")).toObject();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        if (textureSlots().contains(it.key())) add(it.value().toString());

    // Baked maps are member textures whose bytes live in the store; the slot
    // value in `values` already names them, so this only catches a bake record
    // for a slot the values no longer carry.
    const QJsonObject baked = definition.value(QStringLiteral("bake")).toObject()
                                        .value(QStringLiteral("maps")).toObject();
    for (auto it = baked.constBegin(); it != baked.constEnd(); ++it)
        add(it.value().toString());

    // A graph's texture nodes: an image a user picked inside the module is a
    // member of the material even before it reaches a master slot.
    const QJsonArray nodes =
        definition.value(QStringLiteral("shadergraph")).toObject()
                  .value(QStringLiteral("nodes")).toArray();
    for (const auto &entry : nodes) {
        const QJsonObject node = entry.toObject();
        if (node.value(QStringLiteral("type")).toString() != QLatin1String("texture"))
            continue;
        add(node.value(QStringLiteral("value")).toString());
    }
    return out;
}

bool reconcileEdges(Database *db, const QString &guid, const QJsonObject &definition)
{
    if (!db || guid.isEmpty()) return false;
    QSqlDatabase conn = QSqlDatabase::database();
    if (!conn.isOpen()) return false;

    // INTRINSIC edges only (audit G2/G3): the ones with no project stamp. A
    // project-stamped edge to this material is somebody's USE of it and is not
    // ours to rewrite; a library save that rewrote those is exactly how one
    // edge set came to serve N pinned versions.
    QSqlQuery del(conn);
    del.prepare("DELETE FROM dependencies WHERE depender = ? AND project_guid IS NULL");
    del.addBindValue(guid);
    if (!del.exec()) return false;

    bool ok = true;
    for (const QString &member : memberGuids(definition)) {
        const auto record = db->fetchAsset(member);
        // A guid the catalog does not know names nothing to depend on.
        if (record.guid.isEmpty()) continue;
        ok = db->createDependency(static_cast<int>(ModelTypes::Material),
                                  record.type, guid, member, QString())
             && ok;
    }
    return ok;
}

QJsonObject read(Database *db, const QString &guid, Project *project)
{
    if (!db || guid.isEmpty()) return QJsonObject();

    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();

    // D-2: the definition is a store object. PIN-FIRST in a project, so a
    // project renders the version it was built with (F11).
    QString path;
    if (project && !project->getProjectGuid().isEmpty())
        path = AssetCas::resolvePinned(conn, root, project->getProjectGuid(), guid);
    else
        path = AssetCas::resolveSource(conn, root, guid);

    if (!path.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonObject definition = parseDefinition(file.readAll());
            if (!definition.isEmpty()) return definition;
        }
    }

    // No stored definition: the row's blob. An image companion minted before
    // this lane, and any row a reader reaches before its first save.
    return parseDefinition(db->fetchAssetData(guid));
}

WriteResult write(Database *db, Project *project, const QString &guid,
                  const QJsonObject &definition, Scope scope)
{
    WriteResult result;
    const auto fail = [&result](const QString &message) {
        result.ok = false;
        result.error = message;
        return result;
    };
    if (!db || guid.isEmpty()) return fail(QStringLiteral("no material"));
    if (definition.isEmpty()) return fail(QStringLiteral("an empty definition"));

    // LOCK 3 (F3). A path in a definition is a reference that exists on one
    // machine; it is refused at the one place definitions are written, so no
    // caller can be the exception.
    QString slot;
    const QString path = offendingPath(definition, &slot);
    if (!path.isEmpty())
        return fail(QStringLiteral("'%1' holds a file path ('%2'), not an asset guid")
                        .arg(slot, path));

    QJsonObject stored = definition;
    stored[QStringLiteral("version")] = kDefinitionVersion;
    if (!stored.contains(QStringLiteral("materialType")))
        stored[QStringLiteral("materialType")] = QStringLiteral("pbr");

    const QByteArray bytes = QJsonDocument(stored).toJson(QJsonDocument::Compact);

    QTemporaryDir staging;
    if (!staging.isValid()) return fail(QStringLiteral("no staging directory"));
    const QString tmpPath = staging.filePath(definitionFileName());
    {
        QFile file(tmpPath);
        if (!file.open(QIODevice::WriteOnly)) return fail(QStringLiteral("could not stage the definition"));
        if (file.write(bytes) != bytes.size()) return fail(QStringLiteral("a short write"));
    }

    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    QString error;
    QString oid;
    if (!AssetCas::ingestFile(conn, root, tmpPath, guid, QStringLiteral("source"),
                              definitionFileName(), &oid, &error))
        return fail(error.isEmpty() ? QStringLiteral("the store refused the definition") : error);
    result.oid = oid;

    if (scope == Scope::Project) {
        // Only THIS project's pin moves — the library original and every other
        // project keep theirs (the owner's model, §12 Q2).
        if (!project || project->getProjectGuid().isEmpty())
            return fail(QStringLiteral("a project-scope save needs an open project"));
        if (!AssetCas::writePin(conn, project->getProjectGuid(), guid, oid))
            return fail(QStringLiteral("could not move the project pin"));
    } else {
        // The LIBRARY publishes: the row's own source pointer moves. ingestFile
        // is an INSERT OR IGNORE on (guid, role, name), so on a re-save it left
        // the mapping on the previous oid — moveSourcePointer is the write that
        // publishes (assetcas.h says so in as many words).
        if (!AssetCas::moveSourcePointer(conn, guid, oid, definitionFileName(), &error))
            return fail(error.isEmpty() ? QStringLiteral("could not publish the definition") : error);
        // A project with no pin yet renders the library version; one that has a
        // pin keeps it. A library save is not an update of anybody's project.
    }

    // The row's `asset` blob is a CACHE of the LIBRARY's current definition —
    // what every listing, thumbnailer and search reads without touching disk.
    // A project-scope save must not move it: the library's version did not
    // change.
    if (scope == Scope::Library) db->updateAssetAsset(guid, bytes);

    reconcileEdges(db, guid, stored);

    QString sidecarError;
    if (!AssetCas::writeSidecar(conn, root, guid, &sidecarError))
        qWarning("MaterialBundle::write: could not refresh the sidecar for %s (%s)",
                 qUtf8Printable(guid), qUtf8Printable(sidecarError));

    result.ok = true;
    return result;
}

QString create(Database *db, const QString &name, const QJsonObject &definition,
               const QByteArray &thumbnail, QString *errorOut)
{
    if (!db) {
        if (errorOut) *errorOut = QStringLiteral("no database");
        return QString();
    }
    QJsonObject stored = definition;
    stored[QStringLiteral("name")] = name;

    const QString guid = GUIDManager::generateGUID();
    db->createAssetEntry(guid, name, static_cast<int>(ModelTypes::Material),
                         QString(),          // a library row: no parent folder
                         QString(),          // a library row: no project guid
                         QString(), QString(), thumbnail,
                         QByteArray(), QByteArray(), QByteArray(),
                         AssetViewFilter::AssetsView);

    const WriteResult written = write(db, nullptr, guid, stored, Scope::Library);
    if (!written.ok) {
        if (errorOut) *errorOut = written.error;
        db->deleteAsset(guid);
        return QString();
    }
    return guid;
}

} // namespace MaterialBundle
