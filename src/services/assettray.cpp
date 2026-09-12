/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/assettray.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

#include "data/database/database.h"
#include "services/imagematerial.h"

namespace assettray {

namespace {

bool isType(const AssetRecord &record, ModelTypes type)
{
    return record.type == static_cast<int>(type);
}

/// The AVATAR rows this project owns or pins — the claim source for rule 3.
/// Resolved from the PROJECT rather than from the listing, so a type-filtered
/// tray (and a subfolder) answers the same question the full root does; and
/// still only this project's avatars, so a model whose avatar the project
/// never added keeps its own tile. The project's pinned rows are passed IN:
/// every caller has already read them, and reading them twice per listing was
/// half the cost of the tray (small-items round B).
QVector<AssetRecord> projectAvatars(Database *db, const QString &projectGuid,
                                    const QVector<AssetRecord> &pinned)
{
    QVector<AssetRecord> avatars = db->fetchFilteredAssets(projectGuid, static_cast<int>(ModelTypes::Avatar));
    for (auto &record : avatars) record.type = static_cast<int>(ModelTypes::Avatar);  // name+guid query
    for (const auto &record : pinned) {
        if (!isType(record, ModelTypes::Avatar)) continue;
        if (std::any_of(avatars.begin(), avatars.end(),
                        [&](const AssetRecord &r) { return r.guid == record.guid; }))
            continue;
        avatars.append(record);
    }
    return avatars;
}

/// Every guid those avatars are built from or reference: their models and
/// their clips (AvatarAssets::reconcileDependencies writes the edges).
QSet<QString> avatarClosure(Database *db, const QVector<AssetRecord> &avatars)
{
    QSet<QString> out;
    for (const AssetRecord &avatar : avatars)
        for (const QString &guid : db->fetchAssetGUIDAndDependencies(avatar.guid, false))
            out.insert(guid);
    return out;
}

/// EVERYTHING THE RULES ASK THE CATALOG, for a whole listing, in a fixed
/// number of queries (small-items round B). The tray runs on every edge write
/// and — through AssetWidget::searchAssets, which lists every folder — on every
/// SEARCH KEYSTROKE, so a query per row was a query per keypress per row.
struct Batch
{
    QHash<QString, QString> owners;             // guid -> project_guid, for the rows that EXIST
    QHash<QString, QByteArray> definitions;     // ParticleSystem guid -> stored asset blob
    QHash<QString, QStringList> companions;     // texture guid -> its companion materials
    QHash<QString, QStringList> dependers;      // dependee -> this project's dependers
    QSet<QString> pinned;                       // the project's pinned guids
};

Batch gather(Database *db, const QString &projectGuid, const QVector<AssetRecord> &records)
{
    Batch batch;
    QStringList parents, particles, textures;
    for (const AssetRecord &record : records) {
        if (!record.parent.isEmpty() && record.parent != projectGuid) parents << record.parent;
        if (isType(record, ModelTypes::ParticleSystem)) particles << record.guid;
        if (isType(record, ModelTypes::Texture)) textures << record.guid;
    }
    batch.definitions = db->fetchAssetDataFor(particles);
    if (!textures.isEmpty()) {
        batch.companions = ImageMaterial::companionMaterials(textures);
        batch.dependers = db->fetchProjectDependers(projectGuid);
        batch.pinned = db->fetchProjectPinnedGuids(projectGuid);
        // Rule 5 asks which project owns each companion, rule 1 asks whether a
        // parent names an asset at all: one query answers both.
        for (const QStringList &list : batch.companions) parents << list;
    }
    batch.owners = db->fetchAssetOwners(parents);
    return batch;
}

/// Rule 4: a row the editor minted FOR a scene node. The built-in marker
/// (`{"type": "builtin"}` in the properties) is written for the primitives,
/// the default Ground, image planes and decals; an emitter's row carries no
/// definition, because the emitter's recipe is the scene node itself — a
/// library particle system (a .jaf ingest) stores its definition in the asset
/// column, and that one is a tile.
bool isNodeOwnRow(const Batch &batch, const AssetRecord &record)
{
    const QJsonObject props = QJsonDocument::fromJson(record.properties).object();
    if (!props.value(QStringLiteral("type")).toString().isEmpty()) return true;
    if (isType(record, ModelTypes::ParticleSystem)) {
        const QByteArray definition = batch.definitions.value(record.guid).trimmed();
        return definition.isEmpty() || QJsonDocument::fromJson(definition).object().isEmpty();
    }
    return false;
}

/// Rule 5: a DIRECTLY-added image stays folded into the companion material
/// minted for it — while that companion is in this project and is the only
/// thing in this project that uses the image. Any other user (a scene node's
/// material slot, the ground, a decal, a particle emitter, a light, a material
/// the user authored) makes the image a tile of its own.
bool foldedIntoCompanion(const Batch &batch, const QString &projectGuid, const QString &textureGuid)
{
    QStringList companions;
    for (const QString &companion : batch.companions.value(textureGuid)) {
        if (batch.pinned.contains(companion)
            || batch.owners.value(companion) == projectGuid)
            companions << companion;
    }
    if (companions.isEmpty()) return false;
    for (const QString &depender : batch.dependers.value(textureGuid))
        if (!companions.contains(depender)) return false;
    return true;
}

}   // namespace

QStringList hidden(Database *db, const QString &projectGuid, const QVector<AssetRecord> &records)
{
    return hidden(db, projectGuid, records, db ? db->fetchProjectPinnedAssets(projectGuid)
                                               : QVector<AssetRecord>());
}

QStringList hidden(Database *db, const QString &projectGuid, const QVector<AssetRecord> &records,
                   const QVector<AssetRecord> &pinned)
{
    QStringList out;
    if (!db) return out;
    const QSet<QString> claimed = avatarClosure(db, projectAvatars(db, projectGuid, pinned));
    const Batch batch = gather(db, projectGuid, records);
    for (const AssetRecord &record : records) {
        // 1. import MEMBERS. The `parent` column carries BOTH kinds of parent —
        // a folder for a filed asset, the owning ASSET for an import member
        // (assetimporters writes meshRow.parent = the Object's guid) — so
        // membership is "my parent is an asset", not "I have a parent". A
        // folder guid is not in the assets table, which is exactly what tells
        // the two apart. (Database::memberSubquery asks the same question of
        // the Assets page's library grid, so the two listings agree.)
        if (!record.parent.isEmpty() && record.parent != projectGuid
            && batch.owners.contains(record.parent)) {
            out.append(record.guid);
            continue;
        }
        // 2. a mesh is the inside of a model.
        if (isType(record, ModelTypes::Mesh)) { out.append(record.guid); continue; }
        // 3. the model and the clips an avatar in this project owns.
        if (claimed.contains(record.guid)
            && (isType(record, ModelTypes::Object) || isType(record, ModelTypes::Animation))) {
            out.append(record.guid);
            continue;
        }
        // 4. a scene node's own row.
        if (isNodeOwnRow(batch, record)) { out.append(record.guid); continue; }
        // 5. an image whose tile is its companion material.
        if (isType(record, ModelTypes::Texture) && foldedIntoCompanion(batch, projectGuid, record.guid))
            out.append(record.guid);
    }
    return out;
}

QVector<AssetRecord> collapse(Database *db, const QString &projectGuid,
                              const QVector<AssetRecord> &records)
{
    return collapse(db, projectGuid, records,
                    db ? db->fetchProjectPinnedAssets(projectGuid) : QVector<AssetRecord>());
}

QVector<AssetRecord> collapse(Database *db, const QString &projectGuid,
                              const QVector<AssetRecord> &records,
                              const QVector<AssetRecord> &pinned)
{
    const QStringList drop = hidden(db, projectGuid, records, pinned);
    const QSet<QString> dropSet(drop.begin(), drop.end());
    QVector<AssetRecord> out;
    out.reserve(records.size());
    QSet<QString> seen;
    for (const AssetRecord &record : records) {
        if (dropSet.contains(record.guid) || seen.contains(record.guid)) continue;
        seen.insert(record.guid);
        out.append(record);
    }
    return out;
}

QVector<AssetRecord> list(Database *db, const QString &projectGuid, const QString &folderGuid,
                          int typeFilter)
{
    if (!db || projectGuid.isEmpty()) return {};
    const QString folder = folderGuid.isEmpty() ? projectGuid : folderGuid;
    QVector<AssetRecord> records =
        db->fetchChildAssets(folder, projectGuid, typeFilter > 0 ? typeFilter : -1);

    // Reference-with-pin (phase 4): project membership is a project_assets ROW
    // pinning a LIBRARY asset — there is no project-guid clone row for
    // fetchChildAssets to find — so pinned members list at the project ROOT,
    // from the same source as assets.list({scope: 'project'}).
    // ONE read of the project's pins per listing: the root listing unions them
    // in, and rule 3 (the avatar claim) needs them too — reading them twice was
    // half this function's cost (small-items round B).
    const QVector<AssetRecord> pinned = db->fetchProjectPinnedAssets(projectGuid);
    if (folder == projectGuid) {
        // A PIN LISTS AT THE ROOT ONLY IF IT IS NOT FILED (small-items round
        // B). A project archive's import pins every row it brings in, the
        // project's OWN rows included — the material presets it filed under
        // Presets/, the emitters under Systems/ — so after a round trip the
        // editor's hidden folders emptied themselves onto the root and the
        // same project listed differently before and after an export. A row
        // whose parent is one of this project's folders is listed BY that
        // folder; only unfiled pins (a library asset, which has no folder
        // here) are root tiles.
        const QSet<QString> folders = db->fetchProjectFolderGuids(projectGuid);
        for (const auto &record : pinned) {
            if (typeFilter > 0 && record.type != typeFilter) continue;
            if (folders.contains(record.parent)) continue;
            records.append(record);
        }
    }
    return collapse(db, projectGuid, records, pinned);
}

}   // namespace assettray
