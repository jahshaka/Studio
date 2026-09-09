/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETDELETE_H
#define ASSETDELETE_H

// LIBRARY DELETE (owner, 2026-09-09: "deleting an asset from the LIBRARY
// should not delete it from a project").
//
// THE one implementation behind `assets.remove` and the Assets page's
// "Delete From Library" button — the page calls what the verb calls
// (API-first rule, SCRIPTING_SPEC §2.3), so the pin law is decided in one
// place instead of twice.
//
// The law:
//   * pins exist, no force  -> UNLIST. The row keeps every asset_files row,
//     every project_assets pin, its dependency edges, its sidecar and its
//     content; it stops being a library tile and nothing else changes. Every
//     project that pinned it keeps opening, rendering, thumbnailing and
//     exporting exactly as before, because all of that resolves BY GUID.
//   * no pins                -> the full delete, unchanged.
//   * force                  -> the full delete regardless (pins dropped).
// The CONTENT is never unlinked here: only assets.gc can tell a shared
// object from an exclusive one.

#include <QString>
#include <QVector>

#include "data/project.h"   // AssetPinRecord

class Database;

namespace assetdelete
{

/// Which projects pin this asset (guid + display name). Empty for an asset
/// no project uses — and that is exactly the case a delete deletes.
QVector<AssetPinRecord> pins(Database *db, const QString &guid);

struct Outcome
{
    bool    ok = false;        ///< the write ran (an unlist that ran is a success)
    bool    unlisted = false;  ///< kept for its pins instead of deleted
    int     pinCount = 0;      ///< projects pinning it at the time of the call
    QString error;             ///< empty when ok
};

/// Remove `guid` from the library under the law above.
/// `keepShared` false takes the dependency closure too (each member judged by
/// its OWN pins); `force` is the hard delete.
Outcome remove(Database *db, const QString &guid, bool keepShared = true, bool force = false);

} // namespace assetdelete

#endif // ASSETDELETE_H
