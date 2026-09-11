/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/assettray.h"

#include <QSet>

#include "data/database/database.h"

namespace assettray {

namespace {

/// Every guid the AVATAR rows in this listing are built from or reference —
/// their models and their clips. One query per avatar row, and a listing has
/// at most a handful.
QSet<QString> avatarClosure(Database *db, const QVector<AssetRecord> &records)
{
    QSet<QString> out;
    if (!db) return out;
    for (const AssetRecord &record : records) {
        if (record.type != static_cast<int>(ModelTypes::Avatar)) continue;
        // The definition's dependencies: the model Object and every clip the
        // avatar names (AvatarAssets::reconcileDependencies writes them).
        for (const QString &guid : db->fetchAssetGUIDAndDependencies(record.guid, false))
            out.insert(guid);
    }
    return out;
}

}   // namespace

QStringList hidden(Database *db, const QVector<AssetRecord> &records,
                   const QVector<AssetRecord> &claimants)
{
    QStringList out;
    const QSet<QString> claimed =
        avatarClosure(db, claimants.isEmpty() ? records : claimants);
    for (const AssetRecord &record : records) {
        // 1. import MEMBERS (Mesh, member Textures) are parts, never tiles.
        //
        // The `parent` column carries BOTH kinds of parent — a folder for a
        // filed asset, the owning ASSET for an import member (assetimporters
        // writes meshRow.parent = the Object's guid) — so membership is
        // "my parent is an asset", not "I have a parent". A folder guid is not
        // in the assets table, which is exactly what tells the two apart.
        if (!record.parent.isEmpty() && !db->fetchAsset(record.parent).guid.isEmpty()) {
            out.append(record.guid);
            continue;
        }
        // 2 + 3. the model and the clips an avatar in this listing owns.
        if (claimed.contains(record.guid)
            && (record.type == static_cast<int>(ModelTypes::Object)
                || record.type == static_cast<int>(ModelTypes::Animation)))
            out.append(record.guid);
    }
    return out;
}

QVector<AssetRecord> collapse(Database *db, const QVector<AssetRecord> &records,
                              const QVector<AssetRecord> &claimants)
{
    const QStringList drop = hidden(db, records, claimants);
    const QSet<QString> dropSet(drop.begin(), drop.end());
    QVector<AssetRecord> out;
    out.reserve(records.size());
    for (const AssetRecord &record : records)
        if (!dropSet.contains(record.guid)) out.append(record);
    return out;
}

}   // namespace assettray
