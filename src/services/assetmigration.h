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
//   - migration is hardlink/copy and RETAINS the legacy per-guid tree until
//     an explicit owner-gated purge; a missing legacy folder is zero files;
//   - migration REFUSES while another process holds the library lock
//     ("close Jahshaka first", preflight §6.2);
//   - migration scans LIBRARY rows: view_filter IN (2,3) — Effects rows ARE
//     library tiles (preflight §1.6);
//   - everything is idempotent: run twice = same store, zero new objects.

#include <QString>
#include <QStringList>
#include <QVariantMap>

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
    int skipped = 0;            // tombstones: sidecars whose objects are all gone
    qint64 elapsedMs = 0;

    QVariantMap toMap() const;
};


/// Re-hash every catalogued object against its oid (Perforce p4 verify):
/// bit-rot and missing objects, with counts and bytes.
VerifyReport verify(const QString &dbPath, const QString &storeRoot);

/// Reconstruct catalog rows (assets + files + asset_files) from
/// sidecar/*.json into dbPath — the honest I2 test and the
/// Unity-Library-delete recovery story. Existing rows with the same guid are
/// left untouched (INSERT OR IGNORE); thumbnails are not recoverable from
/// sidecars (they are regenerable).
RebuildReport rebuildCatalog(const QString &dbPath, const QString &storeRoot);
} // namespace AssetMigration

#endif // ASSETMIGRATION_H
