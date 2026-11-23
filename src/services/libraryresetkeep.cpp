/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// THE RESET's STEP 4b AFTER ITS REBUILD, in a file of its own so db.failed_opens links the real decision and
// records what it DID (the sweep, the counts, the error) for every rebuild answer - not a copy of its condition.

#include "services/libraryreset.h"

#include "data/database/database.h"
#include "services/assetgc.h"

namespace libraryreset {

void keepAfterRebuild(Database *db, const QString &storeRoot, const AssetMigration::RebuildReport &rebuilt,
                      const AssetMigration::RebuildOptions &options, Result &result)
{
    // ONLY AFTER A WHOLE REBUILD (sweepAllowedAfter): else the reset fails by name, the store as it is.
    if (!sweepAllowedAfter(rebuilt.ok, rebuilt.failedRows, rebuilt.committed)) {
        if (result.error.isEmpty())
            result.error = QStringLiteral("the kept storages could not be rebuilt: %1 - the store was left as "
                                          "it is (nothing collected)").arg(rebuilt.error);
        return;
    }
    result.kept.rows = rebuilt.assets;
    result.kept.edges = rebuilt.edges;
    result.kept.unreadable = rebuilt.unreadable;
    result.kept.objects = AssetMigration::keptObjects(storeRoot, options).size();
    const AssetGc::Report collected = AssetGc::sweep(db->connection(), storeRoot, /*dryRun*/ false, /*force*/ true);
    if (!collected.ok && result.error.isEmpty() && !collected.error.isEmpty())
        result.error = QStringLiteral("the store could not be collected: %1").arg(collected.error);
}

}   // namespace libraryreset
