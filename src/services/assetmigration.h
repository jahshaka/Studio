/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#ifndef ASSETMIGRATION_H
#define ASSETMIGRATION_H

// Store migration, verification and catalog rebuild (ASSET_PIPELINE_SPEC
// §3.1.3, phase 2 — with the preflight amendments):
//   - every tool takes an EXPLICIT db path + store root, opening its own
//     named connection, so a rehearsal against a copied library never goes
//     near the live one (preflight §3.2);
//   - migration REFUSES while another process holds the library lock
//     ("close Jahshaka first", preflight §6.2);
//   - the user's STORAGE rows are view_filter IN (2,5,6) — Assets, the
//     Materials and the Avatar storage (ASSETS-HOME-1, data/assethomekind.h);
//   - everything is idempotent: run twice = same store, zero new objects.

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "data/assethomekind.h"

namespace AssetMigration
{
struct VerifyReport
{
    bool ok = false;            // true = every object present and bit-identical
    QString error;
    int objects = 0;
    qint64 bytes = 0;
    QStringList corrupt;        // oid: bytes no longer hash to the oid
    QStringList missing;        // oid: object file absent
    QStringList missingSidecars;  // guid: a storage row with no sidecar (a rebuild would lose it)
    qint64 elapsedMs = 0;

    QVariantMap toMap() const;
};

struct RebuildReport
{
    bool ok = false;
    QString error;
    int assets = 0;             // asset rows written from sidecars
    int files = 0;              // files rows
    int links = 0;              // asset_files rows
    int pins = 0;               // project_assets rows (pin-only objects, item 1c')
    int edges = 0;              // intrinsic dependency edges restored
    int skipped = 0;            // tombstones: sidecars whose objects are all gone
    int otherHomes = 0;         // sidecars of a home the caller did not ask for
    int unlistedDropped = 0;    // unlisted rows with no project left to pin them
    int unreadable = 0;         // sidecars not read: unparseable, or no recorded home
    QStringList unreadableFiles;  // ...and which (also named in the log)
    qint64 elapsedMs = 0;

    QVariantMap toMap() const;
};


/// Re-hash every catalogued object against its oid (Perforce p4 verify):
/// bit-rot and missing objects, with counts and bytes.
VerifyReport verify(const QString &dbPath, const QString &storeRoot);

/// What a rebuild restores.
struct RebuildOptions
{
    /// Only sidecars of these homes (empty = every home). A format bump and a
    /// Clear Database rebuild the user's KEPT storages this way.
    QSet<assethome::Kind> homes;
    /// Drop the DERIVED files (role "bake": mesh and clip bakes) instead of
    /// restoring them — a format bump re-derives them from the stored sources
    /// (MeshBakeStore's background rebuild), never reads an old build's.
    bool dropDerived = false;
};

/// Reconstruct catalog rows (assets + files + asset_files + project_assets
/// pins + the intrinsic dependency edges) from sidecar/*.json into dbPath — the
/// honest I2 test, the Unity-Library-delete recovery story, and what a format
/// bump and a Clear Database run to keep the user's storages. Every row comes
/// back as it was: its HOME and origin, its parent, its definition blob, its
/// creation time and use count. Existing rows with the same guid are left
/// untouched (INSERT OR IGNORE). Thumbnails are not in a sidecar: they are
/// regenerated (assets.rebuildThumbnails). FORWARD-ONLY: a sidecar of another
/// format — one that records no home — is not read (`unreadable`).
RebuildReport rebuildCatalog(const QString &dbPath, const QString &storeRoot,
                             const RebuildOptions &options = RebuildOptions());

/// The oids a rebuild with `options` keeps alive (every file and pin of every
/// sidecar it would restore) — what a selective reset must not delete.
QSet<QString> keptObjects(const QString &storeRoot, const RebuildOptions &options);
} // namespace AssetMigration

#endif // ASSETMIGRATION_H
