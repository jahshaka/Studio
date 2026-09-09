/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/assetdelete.h"

#include <QDir>

#include "data/database/database.h"
#include "services/assetstorepaths.h"
#include "io/assetmanager.h"

namespace assetdelete
{

QVector<AssetPinRecord> pins(Database *db, const QString &guid)
{
    if (!db) return {};
    return db->fetchAssetPins(guid);
}

Outcome remove(Database *db, const QString &guid, bool keepShared, bool force)
{
    Outcome out;
    if (!db) {
        out.error = QStringLiteral("no library database in this session");
        return out;
    }
    if (db->fetchAsset(guid).guid.isEmpty()) {
        out.error = QStringLiteral("no asset with guid '%1'").arg(guid);
        return out;
    }

    out.pinCount = db->countAssetPins(guid);

    // ---- the pinned case: unlist, and touch NOTHING else ------------------
    if (!force && out.pinCount > 0) {
        out.unlisted = true;
        out.ok = db->setAssetListed(guid, false);
        if (!out.ok)
            out.error = QStringLiteral("the database refused to unlist '%1' — the asset is "
                                       "still in the library").arg(guid);
        return out;
    }

    // ---- the real delete --------------------------------------------------
    bool ok = true;
    if (keepShared) {
        // Conservative: only the asset row and its dependency links go;
        // dependee assets (possibly shared) stay.
        ok = db->deleteAsset(guid, force) && ok;
        ok = db->deleteDependency(guid) && ok;
        for (const auto &dep : db->fetchAssetGUIDAndDependencies(guid, false))
            ok = db->deleteDependency(guid, dep) && ok;
    } else {
        db->deleteAssetAndDependencies(guid, &ok, force);
    }

    out.ok = ok;
    if (!ok) {
        out.error = QStringLiteral("the database refused the delete of '%1' — the asset is "
                                   "still in the library").arg(guid);
        return out;
    }

    // Whatever the retired legacy per-guid view left behind goes with the row;
    // the CONTENT under objects/ is assets.gc's to reclaim — only it can tell a
    // shared object from an exclusive one.
    QDir legacy(AssetStorePaths::legacyFolder(guid));
    if (legacy.exists()) legacy.removeRecursively();
    return out;
}


Outcome removeFromProject(Database *db, const QString &guid, const QString &projectGuid)
{
    Outcome out;
    if (!db) { out.error = QStringLiteral("no library database in this session"); return out; }
    if (projectGuid.isEmpty()) { out.error = QStringLiteral("no project is open"); return out; }
    const AssetRecord record = db->fetchAsset(guid);
    if (record.guid.isEmpty()) {
        out.error = QStringLiteral("no asset with guid '%1'").arg(guid);
        return out;
    }
    // The asset plus the closure members nothing else depends on: what
    // addToProject pinned, minus what another pinned asset still shares.
    QStringList members = db->fetchAssetGUIDAndDependencies(guid, true);
    QStringList toUnpin;
    for (const QString &member : members) {
        if (member != guid && db->hasMultipleDependers(member).count() > 1) continue;
        toUnpin.append(member);
    }
    bool ok = true;
    for (const QString &member : toUnpin) {
        if (!db->unpinAsset(projectGuid, member)) { ok = false; continue; }
        ++out.pinCount;
        // Session records belong to the OPEN project only, so a scrub is
        // never wrong here (the library delete scrubs the same way).
        auto &assets = AssetManager::getAssets();
        for (int i = assets.count() - 1; i >= 0; --i) {
            if (assets[i]->assetGuid != member) continue;
            delete assets[i];
            assets.remove(i);
        }
        // An UNLISTED row that just lost its last pin exists for nobody: reap
        // it the way deleteProject reaps its orphans.
        const AssetRecord m = member == guid ? record : db->fetchAsset(member);
        if (!m.guid.isEmpty() && !m.listed && db->countAssetPins(member) == 0) {
            if (db->deleteAsset(member, /*force*/ true)) out.unlisted = true;
            else ok = false;
        }
    }
    out.ok = ok;
    if (!ok) out.error = QStringLiteral("the database refused to drop a pin on '%1'").arg(guid);
    return out;
}

} // namespace assetdelete
