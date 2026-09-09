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

} // namespace assetdelete
