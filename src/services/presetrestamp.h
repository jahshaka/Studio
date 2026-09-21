/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PRESETRESTAMP_H
#define PRESETRESTAMP_H

// THE SHIPPED PRESETS' MAPS GET THEIR MEMBER STAMP IN A LIBRARY THAT ALREADY
// EXISTS (SEED-RESTAMP-1; the owner's smoke finding on build #56).
//
// SEED-STAMP-1 taught the boot seed to stamp every map it MINTS
// (services/materialpresetseeder.h): a preset's picture arrived inside a
// material, so it folds into the bundle's tile instead of standing in the tray
// as one of the user's own (MATERIAL_BUNDLE_SPEC V-2). That fix is written at
// the moment of minting, and a library whose rows were minted BEFORE it exists
// keeps them unstamped for ever — the seed is idempotent, so it never looks at
// them again. The owner's library is exactly that: reset on build #53, stamped
// seed shipped in #54, and thirty-five preset maps standing loose in his
// project tray beside the seven preset bundles they belong to.
//
// So the seeder runs this pass beside its own, once per launch: a REPAIR of
// the rows it (or an older build of it) minted, and of nothing else.
//
// MATCHED BY CONTENT, NEVER BY NAME. A texture row is one of a preset's maps
// when its stored SOURCE OBJECT — the sha256 the whole store is keyed on — is
// the object a shipped preset's own definition names. Nothing here reads a
// file name, a folder or a display name: those are the user's to change, and
// two different pictures may share one. Two consequences fall out of that
// choice, both wanted:
//
//   * A DUPLICATE ROW OVER THE SAME BYTES IS REPAIRED TOO. The pre-PATHCLEAN-1
//     seeds minted a second row for three of the maps (two spellings of one
//     path, each asking "does the library have these bytes" before either had
//     imported anything — materialpresetseeder.cpp says it in full). No
//     definition names those rows, so nothing could ever stamp them and they
//     stood in the tray for ever. Their CONTENT is a preset map, so they fold.
//   * A PICTURE THE USER IMPORTED THEMSELVES IS UNTOUCHED, because its bytes
//     are not a shipped map's. That is the whole of V-2's other half that a
//     library can still answer.
//
// THE GATE, AND THE ONE CASE IT CANNOT SEE. A library records the bytes, not
// who asked for them, so "this map is unstamped because the seed that minted
// it was too old" and "this map is unstamped because the USER imported that
// picture before any seed ran" (V-2's arm 1, scripting.e2e.preset_seed_boot)
// look identical on the row. The evidence that does exist is the BATCH: a
// stamping seed stamps every map it mints, so a preset with at least ONE map
// carrying a shipped-preset origin was seeded by a stamping seed, and any
// UNSTAMPED map of that preset is a picture the user brought themselves. That
// is the gate, asked per preset: repair only the presets that show no stamp at
// all. What it cannot answer is a user who imported EVERY map of one preset
// themselves before that preset was ever seeded — a repair claims those. The
// pictures are not lost (the bundle's Members panel and 'Show member textures'
// list them, and the moment anything but a material uses one it is a tile
// again); it is the price of a library that remembers content and not intent.
//
// WHAT IT COSTS, MEASURED at boot on a twenty-preset library (Debug, this box):
// 16 ms on a quiet box and 27 ms with a gate running beside it for the repair
// itself — 32 texture rows read, 31 stamped across 13 bundles, one small UPDATE
// each in one transaction (the sidecar each of those refreshes is a DERIVED
// write: an atomic rename, no fsync) — and 3-6 ms on every launch afterwards,
// which is the twenty definitions being read off the store to ask the question. The cheap pre-gate above them (one query: "is any texture
// row unstamped?") skips even that, but only for a library whose textures are
// ALL stamped — a real one rarely is, because the default ground's checker is a
// PLATFORM row and carries no member stamp. 3 ms of the thread that draws, once
// per launch, is the price of the answer; if it ever has to come down, the shape
// is a cached digest of the shipped set, not a name test.
//
// NOT A MIGRATION (the CRUD law): these rows are SHIPPED CONTENT the app
// itself minted, and the pass writes the two keys the current seed would have
// written. Nothing of the user's is converted, moved or deleted — no pin is
// touched (a pinned map stays pinned; the fold simply hides it under the
// bundle's tile), no row is renamed, no bytes move.

#include <QString>
#include <QStringList>
#include <QtGlobal>

class Database;

namespace presetrestamp
{

struct Report
{
    int stamped = 0;    ///< texture rows given the member stamp by this pass
    int presets = 0;    ///< shipped presets whose maps it repaired
    int skipped = 0;    ///< …and presets left alone: a stamping seed was here
    int scanned = 0;    ///< texture rows looked at
    qint64 ms = 0;      ///< what the pass cost the thread that called it
    /// False when the cheap pre-gate answered (every texture row in the
    /// library already carries a stamp, so there is nothing a repair could
    /// reach) — told apart from `stamped == 0` so a caller can say which.
    bool ran = false;
    QString error;      ///< non-empty when the library could not be read
};

/// Repair the member stamp of every shipped preset map in `db`'s library.
/// `presetGuids` is the SHIPPED SET — the read-only bundles whose maps are the
/// app's own content — in the table order the seed uses, so the first preset
/// that names a picture owns it (the rule `MaterialPresetAssets::Prepared::
/// mapOwners` and `memberstamp::stamp` already follow: an origin never moves).
/// Idempotent, and safe to call on any library: a second run writes nothing.
Report restamp(Database *db, const QStringList &presetGuids);

}   // namespace presetrestamp

#endif // PRESETRESTAMP_H
