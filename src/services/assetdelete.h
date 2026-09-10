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

/// Which projects pin this asset (guid + display name), INCLUDING dead pins
/// (`live == false`: a project_assets row whose project no longer exists —
/// still a catalog reference, still holding content alive, but nobody's).
QVector<AssetPinRecord> pins(Database *db, const QString &guid);

/// The pins that represent a living project — what the delete law actually
/// weighs (it agrees with Database::countAssetPins by construction). Empty
/// means a delete really deletes, however many dead rows survive.
QVector<AssetPinRecord> livePins(Database *db, const QString &guid);

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

/// THE PROJECT-SIDE REMOVE (code review 2026-09-10): take `guid` OUT OF ONE
/// PROJECT and touch the library not at all. The project panel's Delete used
/// to call the library delete, which under the pin law UNLISTED the library
/// tile and left the project exactly as it was — the opposite of what the
/// user asked. This drops the project's pin on the asset and on the closure
/// members only this asset depends on (a texture two pinned models share keeps
/// its pin), scrubs the session records, and reaps a row that was UNLISTED
/// and has just lost its last pin (otherwise it would be invisible, unpinned
/// and undeletable). `pinCount` reports the pins dropped; `unlisted` reports a
/// reap. A listed row is never deleted here — it is still a library asset.
/// Removing an IMAGE also drops the pin on the companion PBR material the add
/// minted for it (2026-09-10), when that material's only dependency is this
/// image, nothing depends on it and the project's scene does not name it —
/// the add created it, so the remove takes it back out.
Outcome removeFromProject(Database *db, const QString &guid, const QString &projectGuid);

} // namespace assetdelete

#endif // ASSETDELETE_H
