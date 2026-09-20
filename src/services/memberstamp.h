/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MEMBERSTAMP_H
#define MEMBERSTAMP_H

// THE MEMBER STAMP — where a picture CAME FROM (MATERIAL_BUNDLE_SPEC §3.3
// V-2), and the one place its two keys are spelled.
//
//     { "member": true, "memberOf": "<the material it came in through>" }
//
// A texture that arrived INSIDE a material — picked through a material's
// texture picker, or shipped with a preset and imported by the first-run seed
// — carries the stamp, and `materialmembers::hiddenAsMember` is what that
// stamp means to a listing: the picture folds into the bundle's tile while
// ONLY materials use it. A texture the USER imported carries no stamp and is
// always a tile.
//
// WHY THIS IS ITS OWN LEAF and not part of services/materialmembers.h (which
// is where it lived until IMPORT-INTENT-1): the stamp is now written from BOTH
// ends of the same question — the material side stamps (the picker, the preset
// seed) and the IMPORT SPINE clears it (an import the user asked for that
// lands on a stamped row: `AssetImportService::commit`). The bundle layer's
// projection pulls in MaterialBundle, ProjectAssets and the delete service;
// the import pipeline is deliberately a leaf, and linking the bundle layer
// into it would have cost seven source files in each of seven test targets for
// two property keys. So the STAMP is a leaf over Database + the catalog, and
// the MEMBERS PROJECTION above it keeps its own file.
//
// THE RULE, both directions (the origin is an origin; intent outranks it):
//   stamp   — a material asked for this import and it minted the row.
//   unstamp — the USER asked for an import and it landed on this row, so the
//             picture is theirs from now on (V-2 read from the import side:
//             "a user who picks it into their own material or drops it on a
//             plane makes it a tile again" — importing it themselves is the
//             same act). The material keeps it as a member either way:
//             membership is the definition's edges, never this stamp.

#include <QByteArray>
#include <QString>
#include <QStringList>

class Database;

namespace memberstamp
{

/// Does this row carry the stamp?
bool isStamped(Database *db, const QString &guid);

/// The same question over a row's `properties` JSON the caller already holds
/// — the listing form: a tray or "clean unused" walk reads hundreds of rows
/// and must not pay a query each to ask two keys.
bool isStamped(const QByteArray &properties);

/// The material this picture came in through, empty when it carries none.
QString originOf(const QByteArray &properties);

/// `properties` carrying the stamp for `materialGuid`, REPLACING whatever
/// stamp it held — the form a row being MINTED needs (materialmembers'
/// "make unique" copies a member row's properties and the copy belongs to the
/// material that asked for it). Writing rows is the caller's business.
QByteArray stamped(const QByteArray &properties, const QString &materialGuid);

/// Mark `textureGuid` as a picture that arrived INSIDE `materialGuid`.
/// Idempotent, and it never overwrites an existing origin: a texture first
/// picked into material A and later used by B belongs to A's clean-up, and
/// re-stamping it would move a row the user cannot see between owners.
bool stamp(Database *db, const QString &textureGuid, const QString &materialGuid);

/// Take the stamp off — the reverse edit of exactly the two keys `stamp`
/// writes, so the row becomes indistinguishable from one the user imported
/// themselves. Idempotent: true for a row that carries no stamp (nothing to
/// do), true when the keys came off, false only when the write failed.
bool unstamp(Database *db, const QString &guid);

/// THE STAMPED TEXTURE ROW whose stored source bytes are `oid`, or empty.
/// Top-level rows only (never an import's member row, which is hidden from
/// every listing and would answer an import with a tile nobody can see) and
/// listed rows only (an unlisted row is the re-listing path's business —
/// AssetImportService::relistUnlistedMatch runs first). Ordered by guid so two
/// equal candidates always answer the same way.
///
/// This is the question the import spine asks of a User-intent import: "are
/// these bytes already in the library as somebody's member texture?" — because
/// a stamped row is INVISIBLE to the user, and minting a second row over the
/// same object for content they cannot see is exactly the duplicate the bundle
/// design exists to prevent.
QString stampedTextureFor(Database *db, const QString &oid);

}   // namespace memberstamp

#endif // MEMBERSTAMP_H
