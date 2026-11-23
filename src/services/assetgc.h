/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#ifndef ASSETGC_H
#define ASSETGC_H

// THE STORE GARBAGE COLLECTOR (deep audit 2026-09, area 6: "no GC exists at
// any level while five artifact classes leak forever").
//
// THE ONE LAW: the collector must never delete live data. Everything here is
// written to that law rather than to completeness — a class of garbage this
// sweep declines to reap costs disk space; a single live object it reaps
// costs the owner their work. Where the two pull against each other, the
// sweep keeps the bytes.
//
// Three properties encode the law:
//
//  1. REACHABILITY IS READ FROM THE ROWS, NOT FROM THE COUNTER. files.refcount
//     is a cache maintained by two triggers. A counter that drifted HIGH would
//     merely hide garbage; a counter that drifted LOW over a live asset_files
//     row would hand the object to the collector. So an object is live when an
//     asset_files row names it OR a project_assets.oid_pin names it — the pin
//     half is not optional: ProjectAssets::copyOnWrite ingests edited bytes
//     under the asset's EXISTING (guid, role, name) key, the INSERT OR IGNORE
//     is ignored, and the new object's ONLY reference in the whole catalog is
//     the project's pin (refcount 0 on live bytes, verified by the assets.cas
//     suite's copy-on-write section). A refcount-only GC eats the owner's
//     edits.
//
//  2. THE COLLECTOR REFUSES A STORE THE CATALOG DOES NOT RECOGNIZE. Pointing a
//     fresh/empty database at a populated store makes every object, sidecar
//     and folder look like garbage. When the catalog knows nothing and the
//     store holds something, collect() reports `ok:false` and reaps nothing
//     (an explicit `force` is the only way past it).
//
//  3. DRY RUN IS THE DEFAULT, at every layer: the verb defaults `dryRun:true`,
//     the Preferences button shows the dry run first and asks, and `sweep()`
//     takes the flag explicitly.
//
// The four classes:
//
//   unreferencedObjects  files row + object with no asset_files row and no pin
//   strayObjects         objects/** with no files row; stale *.tmp-* stagings
//   straySidecars        sidecar/<guid>.json naming no assets row
//   deadPins             project_assets rows naming a project that no longer
//                        exists (code review 2026-09-10; 32 of 129 on the
//                        owner's store). ROWS, not files: they free no bytes
//                        directly, they stop keeping objects alive — the
//                        object a dead pin was the last reference to becomes
//                        an unreferencedObjects item on the NEXT sweep, never
//                        this one, because reachability was read before the
//                        rows went. Deliberate: the sweep never reasons about
//                        bytes it did not measure.

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace db { class Connection; }

namespace AssetGc
{
/// One reapable artifact. `bytes` is what unlinking it frees (0 for a
/// hardlinked view entry that shares its inode — reported as the file size
/// regardless; the numbers are an upper bound by design).
struct Item
{
    QString id;      ///< oid, guid, or the file name — whatever identifies it
    QString path;    ///< absolute path that would be unlinked
    qint64  bytes = 0;
    QString reason;  ///< why it is garbage, in one phrase

    QVariantMap toMap() const;
};

struct ClassReport
{
    QVector<Item> items;
    qint64 bytes = 0;
    int    removed = 0;      ///< 0 on a dry run
    qint64 removedBytes = 0;

    QVariantMap toMap() const;
};

struct Report
{
    bool    ok = false;
    bool    dryRun = true;
    QString error;
    QString root;
    qint64  elapsedMs = 0;

    ClassReport unreferencedObjects;
    ClassReport strayObjects;
    ClassReport straySidecars;
    ClassReport deadPins;
    /// KEPT, never swept (NOQT-DATABASE-1 stage 2, the lead's ruling (A)): sidecars with no assets row that cannot
    /// be READ (not JSON, not an object, no guid, a read error). Their own objects cannot be told apart, so while
    /// any is listed the sweep keeps it AND every object of the two object classes (which are then empty).
    ClassReport unreadableSidecars;
    /// KEPT, never swept (the confirm's F6): sidecars with no assets row that parse but that a rebuild cannot PLACE
    /// (AssetMigration::unplacedReason: no home in this format, no origin, the door's refusal), each named with that
    /// reason. Every object one of them lists (its files and pins) is kept out of the two object classes.
    ClassReport unplacedSidecars;

    /// Informational only, never acted on: files rows whose refcount does not
    /// match the asset_files rows that name them. A non-zero count means the
    /// triggers and the rows have drifted — the reason property (1) above
    /// exists.
    int refcountDrift = 0;
    QStringList failures;    ///< artifacts a real run could not unlink

    int totalCount() const;
    qint64 totalBytes() const;
    int removedCount() const;
    qint64 removedBytes() const;

    QVariantMap toMap() const;
};

/// A CATALOG WHOSE GENERATION IS 0 IS A RESET NOT YET WHOLE (NOQT-DATABASE-1 stage 2, the second confirm's finding 1:
/// the reset clears it first and stamps it last; the next boot finishes it). Every sweep, FORCED OR NOT, refuses it by
/// name - over the catalog a refused reset left, the kept store would be swept as garbage - except the one sweep that
/// reset makes itself after a whole rebuild (libraryreset::keepAfterRebuild: `ThisReset`). A generation that cannot be
/// read refuses the same way.
enum class PendingReset { Refuse, ThisReset };

/// Find every reapable artifact WITHOUT touching the store. `conn` is the
/// catalog, `root` the store root (explicit, so a rehearsal against a copied
/// library never goes near the live one — same contract as AssetMigration).
Report collect(db::Connection &conn, const QString &root, bool force = false,
               PendingReset pending = PendingReset::Refuse);

/// collect(), then — when `dryRun` is false — unlink exactly what it found.
/// The database rows for reaped objects (`files`) go with them, inside one
/// transaction that is committed only if the row deletes succeed.
Report sweep(db::Connection &conn, const QString &root, bool dryRun, bool force = false,
             PendingReset pending = PendingReset::Refuse);

/// Stale-staging cut-off: a `.tmp-<pid>-<n>` sibling younger than this may
/// belong to an import running right now.
inline constexpr int kStaleTempSeconds = 3600;
} // namespace AssetGc

#endif // ASSETGC_H
