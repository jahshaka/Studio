/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#ifndef ASSETCAS_H
#define ASSETCAS_H

// Content-addressed store primitives (ASSET_PIPELINE_SPEC §3.1.3, phase 2).
//
// Everything here is parameterized by an explicit QSqlDatabase connection and
// store root, so the same code serves the live library (default connection +
// active root) AND the migration rehearsal against a copied library
// (preflight §3.2). Object content is immutable (invariant I3): storeObject
// hardlinks when it can (same filesystem — ~0 bytes), copies otherwise, and
// an object that already exists is simply reused — the dedup.

#include <QSqlDatabase>
#include <QString>

namespace AssetCas
{
/// sha256 hex (lowercase) of a file's bytes, streamed; empty on I/O error.
QString hashFile(const QString &path);

/// Put one file's bytes into <root>/objects/ under its oid (hardlink, then
/// copy on failure/EXDEV). Idempotent: an existing object of the right size
/// is left alone. The SOURCE file is never touched (legacy tree retained).
bool storeObject(const QString &srcPath, const QString &root,
                 const QString &oid, const QString &ext, QString *errorOut);

/// Ensure the CAS tables/triggers/user_version exist on this connection.
void ensureCasSchema(QSqlDatabase conn);

/// CAS-FIRST ingest of ONE file (phase 3 — the import pipeline's store
/// primitive): hash srcPath (wherever it lives — the import source, a
/// staging dir), store the object, record the files row and an asset_files
/// row {guid, role, name}. The recorded extension of already-known content
/// wins (jpeg/jpg aliasing, as in ingestLegacyFolder). Idempotent.
/// `oidOut` (optional) receives the content id. `knownOid` (optional) is a
/// precomputed sha256 of srcPath — the import pipeline hashes on a worker
/// thread and passes it here so the DB-thread store stage never re-hashes.
bool ingestFile(QSqlDatabase conn, const QString &root, const QString &srcPath,
                const QString &guid, const QString &role, const QString &name,
                QString *oidOut, QString *errorOut,
                const QString &knownOid = QString());

/// Materialize the legacy per-guid folder <root>/<guid>/ as a HARDLINK VIEW
/// of the asset's objects (copy on filesystems without links): one entry per
/// asset_files row, named by its recorded display name. ~0 bytes. This keeps
/// the not-yet-rerouted legacy readers (library preview, materials-module
/// texture manager, thumbnails) working while the CAS rows are authoritative;
/// it disappears with the last legacy read site. Existing files are left
/// alone. Idempotent.
bool materializeLegacyView(QSqlDatabase conn, const QString &root,
                           const QString &guid, QString *errorOut);

/// Resolve an asset's PRIMARY ('source'-role) file to an absolute path;
/// `nameOut` (optional) receives its display name. Falls back to the single
/// file when no row carries the source role. Empty when the asset has no
/// stored bytes.
QString resolveSource(QSqlDatabase conn, const QString &root,
                      const QString &guid, QString *nameOut = nullptr);

// --- Reference-with-pin (phase 4, spec §3.1.5) -----------------------------

/// Record (or move) a project's pin of an asset: the content the project
/// renders with, frozen at add time (or at an explicit update). Idempotent
/// upsert; empty oid = a DB-only asset (no stored bytes).
bool writePin(QSqlDatabase conn, const QString &projectGuid,
              const QString &assetGuid, const QString &oid);

/// The pinned oid for (project, asset); empty when no pin row exists.
QString pinnedOid(QSqlDatabase conn, const QString &projectGuid,
                  const QString &assetGuid);

/// Project-context resolution: the PINNED bytes when a pin exists and the
/// object is present, else the asset's current source (a project asset
/// created before pinning, or a pin whose object was purged). `nameOut`
/// receives the display name. Empty when the asset has no bytes anywhere.
QString resolvePinned(QSqlDatabase conn, const QString &root,
                      const QString &projectGuid, const QString &guid,
                      QString *nameOut = nullptr);

/// The INVERSE of resolvePinned/resolveSource: the asset guid whose stored
/// bytes live at `path`, or empty when the path is not a store object. Since
/// the CAS an object's file NAME is its sha256, so a writer that recovers a
/// guid from a resolved path must go through the oid — matching by display
/// name finds nothing and silently loses the reference (the particle/material
/// texture-erasing save, 2026-09-03). `projectGuid` breaks ties when one
/// object backs several assets: the asset this project pins wins.
QString guidForStorePath(QSqlDatabase conn, const QString &root, const QString &path,
                         const QString &projectGuid);

/// Write <root>/sidecar/<guid>.json — the catalog-rebuild record (invariant
/// I2): identity, organization, metadata and the file manifest.
bool writeSidecar(QSqlDatabase conn, const QString &root, const QString &guid,
                  QString *errorOut);

/// Resolve an asset's file to an absolute path: asset_files → objects/ when
/// the object exists, else the legacy folder+name fallback (one release,
/// spec §3.1.3). Empty when neither exists.
QString resolveFile(QSqlDatabase conn, const QString &root,
                    const QString &guid, const QString &name);

/// Write/refresh <root>/store.json (store id + format version — the sanity
/// anchor for Use Existing Store). Keeps an existing store id stable.
bool writeStoreInfo(const QString &root, QString *errorOut);
} // namespace AssetCas

#endif // ASSETCAS_H
