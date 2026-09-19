/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/materialmembers.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>

#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "services/assetcas.h"
#include "services/assetdelete.h"
#include "services/assetstorepaths.h"
#include "services/materialbundle.h"

namespace {

const QLatin1String kMemberKey("member");
const QLatin1String kOriginKey("memberOf");

QJsonObject propertiesOf(Database *db, const QString &guid)
{
    if (!db) return QJsonObject();
    return QJsonDocument::fromJson(db->fetchAsset(guid).properties).object();
}

qint64 storedBytes(Database *db, Project *project, const QString &guid)
{
    Q_UNUSED(db);
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    QString path;
    if (project && !project->getProjectGuid().isEmpty())
        path = AssetCas::resolvePinned(conn, root, project->getProjectGuid(), guid);
    if (path.isEmpty()) path = AssetCas::resolveSource(conn, root, guid);
    if (path.isEmpty()) return -1;
    const QFileInfo info(path);
    return info.exists() ? info.size() : -1;
}

}   // namespace

namespace materialmembers {

int usedBy(Database *db, const QString &guid)
{
    if (!db || guid.isEmpty()) return 0;
    // THE UNSCOPED LIST. `fetchDependers` is project-scoped and answers empty
    // outside a project, which is how the Members panel's one number read 0
    // for everything before phase 1's fix.
    return db->hasMultipleDependers(guid).size();
}

bool isStampedMember(Database *db, const QString &guid)
{
    return propertiesOf(db, guid).value(kMemberKey).toBool();
}

bool stampMember(Database *db, const QString &textureGuid, const QString &materialGuid)
{
    if (!db || textureGuid.isEmpty()) return false;
    QJsonObject props = propertiesOf(db, textureGuid);
    if (props.value(kMemberKey).toBool()
        && !props.value(kOriginKey).toString().isEmpty())
        return true;   // already somebody's member; an origin never moves
    props.insert(kMemberKey, true);
    if (!materialGuid.isEmpty() && props.value(kOriginKey).toString().isEmpty())
        props.insert(kOriginKey, materialGuid);
    return db->updateAssetProperties(textureGuid, QJsonDocument(props).toJson());
}

bool hiddenAsMember(Database *db, const QString &guid)
{
    if (!db || guid.isEmpty()) return false;
    if (!isStampedMember(db, guid)) return false;   // the user's own image is a tile, always

    const QStringList dependers = db->hasMultipleDependers(guid);
    // Used by nothing: a tile, so the user can SEE it and clean it up. This is
    // the row `unused` reports, and a row nobody can see is a row nobody can
    // delete.
    if (dependers.isEmpty()) return false;

    for (const QString &depender : dependers) {
        const AssetRecord row = db->fetchAsset(depender);
        // A depender the catalog does not know is a SCENE NODE's use (the
        // editor records use as a dependency with the node's id): the picture
        // is on something the user can see, so it is a thing of its own.
        if (row.guid.isEmpty()) return false;
        if (row.type != static_cast<int>(ModelTypes::Material)) return false;
    }
    return true;
}

QVector<Member> describe(Database *db, Project *project, const QString &materialGuid)
{
    QVector<Member> out;
    if (!db || materialGuid.isEmpty()) return out;

    const QJsonObject definition = MaterialBundle::read(db, materialGuid, project);
    if (definition.isEmpty()) return out;

    // slot -> guid, and the reverse, from the definition's own values.
    const QJsonObject values = definition.value(QStringLiteral("values")).toObject();
    QHash<QString, QString> slotOf;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        if (MaterialBundle::textureSlots().contains(it.key()))
            slotOf.insert(it.value().toString(), it.key());

    // Which slots a BAKE produced (a map the graph baked, not a picture the
    // user picked) — the panel's "baked or source" column.
    const QJsonObject bakedMaps = definition.value(QStringLiteral("bake")).toObject()
                                            .value(QStringLiteral("maps")).toObject();
    QSet<QString> bakedGuids;
    for (auto it = bakedMaps.constBegin(); it != bakedMaps.constEnd(); ++it)
        bakedGuids.insert(it.value().toString());

    // And which GRAPH NODE holds it, for a graph material whose picture has
    // not reached a master slot (the node IS where the user put it).
    QHash<QString, QString> nodeOf;
    const QJsonArray nodes = definition.value(QStringLiteral("shadergraph")).toObject()
                                       .value(QStringLiteral("nodes")).toArray();
    for (const auto &entry : nodes) {
        const QJsonObject node = entry.toObject();
        if (node.value(QStringLiteral("type")).toString() != QLatin1String("texture")) continue;
        const QString value = node.value(QStringLiteral("value")).toString();
        if (value.isEmpty()) continue;
        if (!nodeOf.contains(value))
            nodeOf.insert(value, node.value(QStringLiteral("id")).toString());
    }

    const QString projectGuid = project ? project->getProjectGuid() : QString();
    for (const QString &guid : MaterialBundle::memberGuids(definition)) {
        const AssetRecord row = db->fetchAsset(guid);
        if (row.guid.isEmpty()) continue;
        Member member;
        member.guid = guid;
        member.name = row.name;
        member.slot = slotOf.value(guid);
        member.node = nodeOf.value(guid);
        member.role = bakedGuids.contains(guid) ? QStringLiteral("baked")
                                                : QStringLiteral("source");
        member.bytes = storedBytes(db, project, guid);
        member.usedBy = usedBy(db, guid);
        member.pinned = !projectGuid.isEmpty() && db->isAssetPinnedBy(projectGuid, guid);
        member.member = isStampedMember(db, guid);
        member.hidden = hiddenAsMember(db, guid);
        out.append(member);
    }
    return out;
}

QVector<Unused> unused(Database *db, Project *project, const QString &materialGuid)
{
    QVector<Unused> out;
    if (!db) return out;

    const auto describeRow = [&](const AssetRecord &row, const QString &scope) {
        Unused entry;
        entry.guid = row.guid;
        entry.name = row.name;
        entry.bytes = storedBytes(db, project, row.guid);
        entry.scope = scope;
        return entry;
    };

    if (!materialGuid.isEmpty()) {
        // LIBRARY SCOPE: the rows that came in through THIS bundle and are
        // referenced by nothing. The origin stamp is the only link left once
        // the definition stopped naming them — the edges are derived from the
        // definition, so they went with the slot.
        for (const auto &row : db->fetchAssetsForAssetView()) {
            if (row.type != static_cast<int>(ModelTypes::Texture)) continue;
            const QJsonObject props = QJsonDocument::fromJson(row.properties).object();
            if (!props.value(kMemberKey).toBool()) continue;
            if (props.value(kOriginKey).toString() != materialGuid) continue;
            if (!db->hasMultipleDependers(row.guid).isEmpty()) continue;   // somebody uses it
            if (!assetdelete::livePins(db, row.guid).isEmpty()) continue;  // a project holds it
            out.append(describeRow(row, QStringLiteral("library")));
        }
        return out;
    }

    // PROJECT SCOPE: the member pins this project holds that nothing in it
    // uses. The PIN goes, never the library row — another project may be
    // built on the same bundle.
    if (!project || project->getProjectGuid().isEmpty()) return out;
    const QString projectGuid = project->getProjectGuid();
    for (const auto &row : db->fetchProjectPinnedAssets(projectGuid)) {
        if (row.type != static_cast<int>(ModelTypes::Texture)) continue;
        const QJsonObject props = QJsonDocument::fromJson(row.properties).object();
        if (!props.value(kMemberKey).toBool()) continue;
        if (!db->hasMultipleDependers(row.guid).isEmpty()) continue;
        out.append(describeRow(row, QStringLiteral("project")));
    }
    return out;
}

QVector<Unused> cleanUnused(Database *db, Project *project, const QString &materialGuid,
                            QString *errorOut)
{
    QVector<Unused> removed;
    if (!db) {
        if (errorOut) *errorOut = QStringLiteral("no library");
        return removed;
    }
    if (materialGuid.isEmpty() && (!project || project->getProjectGuid().isEmpty())) {
        if (errorOut) *errorOut = QStringLiteral("a project-scope clean needs an open project");
        return removed;
    }

    const QVector<Unused> candidates = unused(db, project, materialGuid);
    DbBatch batch(db);
    for (const Unused &entry : candidates) {
        if (entry.scope == QLatin1String("library")) {
            // No pins by construction (`unused` checked), so this is the full
            // delete — of the ROW. The bytes stay for `assets.gc`, which is
            // the only thing in this app allowed to decide a shared object is
            // dead.
            if (assetdelete::remove(db, entry.guid).ok) removed.append(entry);
        } else {
            if (db->unpinAsset(project->getProjectGuid(), entry.guid)) removed.append(entry);
        }
    }
    return removed;
}

QString makeUnique(Database *db, Project *project, const QString &materialGuid,
                   const QString &textureGuid, QString *errorOut)
{
    const auto fail = [errorOut](const QString &why) {
        if (errorOut) *errorOut = why;
        return QString();
    };
    if (!db) return fail(QStringLiteral("no library"));
    if (materialGuid.isEmpty() || textureGuid.isEmpty())
        return fail(QStringLiteral("a material and a texture are required"));

    const AssetRecord texture = db->fetchAsset(textureGuid);
    if (texture.guid.isEmpty() || texture.type != static_cast<int>(ModelTypes::Texture))
        return fail(QStringLiteral("'%1' is not a texture").arg(textureGuid));

    QJsonObject definition = MaterialBundle::read(db, materialGuid, project);
    if (definition.isEmpty())
        return fail(QStringLiteral("no material '%1'").arg(materialGuid));
    if (!MaterialBundle::memberGuids(definition).contains(textureGuid))
        return fail(QStringLiteral("'%1' is not a member of this material").arg(texture.name));

    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    const QString sourcePath = AssetCas::resolveSource(conn, root, textureGuid);
    if (sourcePath.isEmpty())
        return fail(QStringLiteral("'%1' has no stored bytes to copy").arg(texture.name));

    // THE NEW ROW OVER THE SAME BYTES. `ingestFile` is content-addressed and
    // idempotent, so re-ingesting the resolved object costs one asset_files
    // row and NO disk: the object is already there under its own hash, and
    // both rows now name it. That is the whole trick — "a second row on the
    // same bytes, zero disk" (spec §4).
    const QString newGuid = GUIDManager::generateGUID();
    QJsonObject props = QJsonDocument::fromJson(texture.properties).object();
    props.insert(kMemberKey, true);
    props.insert(kOriginKey, materialGuid);

    DbBatch batch(db);
    db->createAssetEntry(newGuid, texture.name, static_cast<int>(ModelTypes::Texture),
                         texture.parent, QString(),
                         texture.license, texture.author, texture.thumbnail,
                         QJsonDocument(props).toJson(), texture.tags, QByteArray(),
                         AssetViewFilter::AssetsView);

    QString error;
    QString oid;
    if (!AssetCas::ingestFile(conn, root, sourcePath, newGuid, QStringLiteral("source"),
                              texture.name, &oid, &error)) {
        db->deleteAsset(newGuid, /*force=*/true);
        return fail(error.isEmpty() ? QStringLiteral("the store refused the copy") : error);
    }

    // THIS MATERIAL'S DEFINITION NOW NAMES THE NEW ROW — every place the old
    // guid appears in it: a master slot, a bake record, a graph texture node.
    QJsonObject values = definition.value(QStringLiteral("values")).toObject();
    for (const QString &key : values.keys())
        if (values.value(key).toString() == textureGuid) values[key] = newGuid;
    if (!values.isEmpty()) definition[QStringLiteral("values")] = values;

    QJsonObject bake = definition.value(QStringLiteral("bake")).toObject();
    QJsonObject maps = bake.value(QStringLiteral("maps")).toObject();
    bool movedBake = false;
    for (const QString &key : maps.keys())
        if (maps.value(key).toString() == textureGuid) { maps[key] = newGuid; movedBake = true; }
    if (movedBake) {
        bake[QStringLiteral("maps")] = maps;
        definition[QStringLiteral("bake")] = bake;
    }

    QJsonObject graph = definition.value(QStringLiteral("shadergraph")).toObject();
    if (!graph.isEmpty()) {
        QJsonArray nodes = graph.value(QStringLiteral("nodes")).toArray();
        bool movedNode = false;
        for (int i = 0; i < nodes.size(); ++i) {
            QJsonObject node = nodes.at(i).toObject();
            if (node.value(QStringLiteral("type")).toString() != QLatin1String("texture")) continue;
            if (node.value(QStringLiteral("value")).toString() != textureGuid) continue;
            node[QStringLiteral("value")] = newGuid;
            nodes.replace(i, node);
            movedNode = true;
        }
        if (movedNode) {
            graph[QStringLiteral("nodes")] = nodes;
            definition[QStringLiteral("shadergraph")] = graph;
        }
    }

    // A PROJECT'S EDIT STAYS THE PROJECT'S (the owner's model, §12 Q2): when
    // the open project holds this material, the swap is a copy-on-write and
    // the library original keeps the shared texture.
    const bool projectOwns = project && !project->getProjectGuid().isEmpty()
                             && db->isAssetPinnedBy(project->getProjectGuid(), materialGuid);
    const auto written = MaterialBundle::write(
        db, project, materialGuid, definition,
        projectOwns ? MaterialBundle::Scope::Project : MaterialBundle::Scope::Library);
    if (!written.ok) {
        db->deleteAsset(newGuid, /*force=*/true);
        return fail(written.error);
    }
    return newGuid;
}

}   // namespace materialmembers
