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
#include "services/materialmembers.h"
#include "services/memberstamp.h"

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

/// Rule 4: a row the EDITOR minted, not the user. Two markers, one test — a
/// non-empty `type` in the properties:
///   "builtin"  a scene node's own row: the primitives, the default Ground,
///              image planes, decals;
///   "platform" a file the app ships that a platform-owned node needs and the
///              editor pinned by itself — the default floor's checker
///              (services/shippedassets.h Ownership::Platform).
/// An emitter's row carries no definition, because the emitter's recipe is the
/// scene node itself — a library particle system (a .jaf ingest) stores its
/// definition in the asset column, and that one is a tile.
bool isEditorOwnRow(const Batch &batch, const AssetRecord &record)
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

/// Rule 6: a picture that came in THROUGH a material's picker folds into its
/// bundle (MATERIAL_BUNDLE_SPEC V-2, owner Q4). The stamp is the origin — "it
/// arrived inside a material" — and the fold holds only while materials are
/// the only things using it; a scene node, a decal or an emitter that names
/// the image makes it a tile again. A texture the USER imported carries no
/// stamp and is never folded, which is the difference between this rule and
/// the dependency-hiding that was tried and removed in 2026-09-12.
bool foldedIntoBundle(Database *db, const AssetRecord &record)
{
    if (!isType(record, ModelTypes::Texture)) return false;
    // The cheap half first, off the record we already hold: almost no texture
    // carries the stamp, and only a stamped one costs a query.
    if (!memberstamp::isStamped(record.properties))
        return false;
    return materialmembers::hiddenAsMember(db, record.guid);
}

}   // namespace

QStringList hidden(Database *db, const QString &projectGuid, const QVector<AssetRecord> &records)
{
    return hidden(db, projectGuid, records, db ? db->fetchProjectPinnedAssets(projectGuid)
                                               : QVector<AssetRecord>());
}

QStringList hidden(Database *db, const QString &projectGuid, const QVector<AssetRecord> &records,
                   const QVector<AssetRecord> &pinned, bool showMembers)
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
        // 2b. a ModelTypes::SHADER row: the Materials module's old separate
        //     graph asset (MATERIAL_BUNDLE_SPEC phase 2, owner Q3 "only
        //     materials"). Nothing in the app can mint one any more and
        //     nothing can read one — the readers went with the two minting
        //     sites — so a library that predates the bundle model may still
        //     hold such a row, and a tile for it would be a tile that cannot
        //     be opened, applied or previewed. Not a data change: the row is
        //     untouched and goes with the next data wipe (spec 7, no
        //     migration is owed).
        if (isType(record, ModelTypes::Shader)) { out.append(record.guid); continue; }
        // 3. the model and the clips an avatar in this project owns.
        if (claimed.contains(record.guid)
            && (isType(record, ModelTypes::Object) || isType(record, ModelTypes::Animation))) {
            out.append(record.guid);
            continue;
        }
        // 4. a row the editor minted: a scene node's own, or the platform's
        //    own furniture (the floor's checker).
        if (isEditorOwnRow(batch, record)) { out.append(record.guid); continue; }
        // 5. an image whose tile is its companion material.
        if (isType(record, ModelTypes::Texture) && foldedIntoCompanion(batch, projectGuid, record.guid)) {
            out.append(record.guid);
            continue;
        }
        // 6. a picture that arrived inside a material bundle (V-2). The
        //    "Show member textures" switch turns exactly this rule off.
        if (!showMembers && foldedIntoBundle(db, record)) out.append(record.guid);
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
                              const QVector<AssetRecord> &pinned, bool showMembers)
{
    const QStringList drop = hidden(db, projectGuid, records, pinned, showMembers);
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
                          int typeFilter, bool showMembers)
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
    // WHERE THIS PROJECT FILES ITS PINS (DRAWERS-1): a pinned row is a LIBRARY
    // row shared by every project, so its folder cannot ride `assets.parent`
    // and rides the PIN instead (services/projectfolders.h). One query, like
    // every other read here.
    const QHash<QString, QString> pinFolders = db->fetchProjectPinFolders(projectGuid);
    const QSet<QString> folders = db->fetchProjectFolderGuids(projectGuid);
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
        for (const auto &record : pinned) {
            if (typeFilter > 0 && record.type != typeFilter) continue;
            if (folders.contains(record.parent)) continue;
            // A pin filed in a folder that still EXISTS lists there, not here;
            // one whose folder is gone comes home to the root rather than
            // disappearing (nothing deletes a folder without re-filing its
            // rows, so this is the belt to that brace).
            if (folders.contains(pinFolders.value(record.guid))) continue;
            records.append(record);
        }
    } else {
        for (const auto &record : pinned) {
            if (typeFilter > 0 && record.type != typeFilter) continue;
            if (pinFolders.value(record.guid) == folder) records.append(record);
        }
    }
    return collapse(db, projectGuid, records, pinned, showMembers);
}

QVector<AssetRecord> listAll(Database *db, const QString &projectGuid, int typeFilter,
                             bool showMembers)
{
    if (!db || projectGuid.isEmpty()) return {};
    // THE WHOLE PROJECT, FLAT — the same rule, every folder (DRAWERS-1). The
    // Materials module's project drawer reads this: it is one column of tiles
    // with no breadcrumb, so a folder-filed material would otherwise be
    // invisible in the module while the editor tray shows it.
    QStringList folders;
    folders << projectGuid;
    for (const QString &guid : db->fetchProjectFolderGuids(projectGuid)) folders << guid;

    QVector<AssetRecord> out;
    QSet<QString> seen;
    for (const QString &folder : folders) {
        for (const AssetRecord &record : list(db, projectGuid, folder, typeFilter, showMembers)) {
            if (seen.contains(record.guid)) continue;
            seen.insert(record.guid);
            out.append(record);
        }
    }
    return out;
}

QStringList libraryHidden(Database *db, const QVector<AssetRecord> &records, bool showMembers)
{
    QStringList out;
    if (!db) return out;
    for (const AssetRecord &record : records) {
        // 2b. a legacy Shader row (the Assets page used to skip it inline).
        if (isType(record, ModelTypes::Shader)) { out.append(record.guid); continue; }
        // 6. a picture that arrived inside a material bundle (V-2). The cheap
        //    half of the test reads the record's own properties; only a
        //    stamped row costs a query, and a library holds few of those
        //    (the preset seed's maps, a material's picked textures).
        if (!showMembers && foldedIntoBundle(db, record)) out.append(record.guid);
    }
    return out;
}

QVector<AssetRecord> libraryList(Database *db, bool showMembers)
{
    QVector<AssetRecord> out;
    if (!db) return out;
    const QVector<AssetRecord> records = db->fetchAssetsForAssetView();
    const QStringList drop = libraryHidden(db, records, showMembers);
    if (drop.isEmpty()) return records;
    const QSet<QString> dropped(drop.begin(), drop.end());
    out.reserve(records.size());
    for (const AssetRecord &record : records)
        if (!dropped.contains(record.guid)) out.append(record);
    return out;
}

}   // namespace assettray
