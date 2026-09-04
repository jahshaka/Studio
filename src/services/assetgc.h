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
// The five classes (the audit's four, plus the legacy-view reclaim that makes
// retiring the view worth the trouble — Windows sees the view as a SECOND full
// copy of every file, 152MB → 438MB):
//
//   unreferencedObjects  files row + object with no asset_files row and no pin
//   strayObjects         objects/** with no files row; stale *.tmp-* stagings
//   straySidecars        sidecar/<guid>.json naming no assets row
//   legacyFolders        <root>/<guid>/ naming no assets row
//   redundantLegacyFiles <root>/<guid>/<name> whose bytes are proven present
//                        in the CAS (same size, object exists) — the view
//                        materializeLegacyView used to build

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

class QSqlDatabase;

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
    ClassReport legacyFolders;
    ClassReport redundantLegacyFiles;

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

/// Find every reapable artifact WITHOUT touching the store. `conn` is the
/// catalog, `root` the store root (explicit, so a rehearsal against a copied
/// library never goes near the live one — same contract as AssetMigration).
Report collect(QSqlDatabase conn, const QString &root, bool force = false);

/// collect(), then — when `dryRun` is false — unlink exactly what it found.
/// The database rows for reaped objects (`files`) go with them, inside one
/// transaction that is committed only if the row deletes succeed.
Report sweep(QSqlDatabase conn, const QString &root, bool dryRun, bool force = false);

/// Stale-staging cut-off: a `.tmp-<pid>-<n>` sibling younger than this may
/// belong to an import running right now.
inline constexpr int kStaleTempSeconds = 3600;
} // namespace AssetGc

#endif // ASSETGC_H
