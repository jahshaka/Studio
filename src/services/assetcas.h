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
///
/// ATOMIC (STABILITY_PROGRAM_SPEC.md Lane 2). The content-addressed name is a
/// claim about the bytes under it, so a half-written object is worse than a
/// missing one: readers do not re-hash, they just read a short file and draw
/// white. The bytes are therefore always staged into a sibling temp in the
/// SAME directory and moved into place with a single rename — the object at
/// its final path either does not exist or is complete, at every instant, in
/// every process, including one that is SIGKILLed mid-store.
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

// THE LEGACY VIEW IS RETIRED (deep audit 2026-09, area 6).
//
// materializeLegacyView used to hardlink every asset's objects into
// <root>/<guid>/ so the readers that had not moved to the resolver kept
// working. All of them have moved (resolveFile / resolveSource /
// resolvePinned), so the view is gone: it was a SECOND full copy of the store
// on any filesystem without hardlinks — Windows, where 152MB became 438MB and
// every import paid a second full write. Existing stores keep their folders
// until `assets.gc` reclaims them, and resolveFile still READS them (below),
// so nothing breaks on the way through.

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

/// What a caller means the guid to BE, when one object backs several assets.
///
/// An imported model records its textures TWICE (assetimporters.cpp): the
/// bytes hang off the Object under role 'texture' AND off the member Texture
/// asset under role 'source'. Both are pinned by the project, so a lookup by
/// oid alone has a real tie to break, and the answer differs by caller: a
/// material's baseColorMap wants the TEXTURE, a skeletal clip's source wants
/// whatever asset owns the model file.
enum class GuidPreference
{
    Any,        ///< any asset backed by these bytes (source-role rows first)
    Texture     ///< a Texture asset if one is backed by these bytes
};

/// The INVERSE of resolvePinned/resolveSource: the asset guid whose stored
/// bytes live at `path`, or empty when the path is not a store object. Since
/// the CAS an object's file NAME is its sha256, so a writer that recovers a
/// guid from a resolved path must go through the oid — matching by display
/// name finds nothing and silently loses the reference (the particle/material
/// texture-erasing save, 2026-09-03). `projectGuid` breaks ties when one
/// object backs several assets: the asset this project pins wins.
///
/// TIE-BREAK, and why it is spelled out (the GLB texture-loss defect,
/// 2026-09-09 — every imported model lost its maps on save+reopen). Pinnedness
/// alone did NOT decide between the .glb Object's role='texture' row and the
/// member Texture's role='source' row over the same oid: an imported object
/// pins its whole dependency closure, so BOTH rows are pinned, and the
/// remaining order was SQLite's index order — the Object's row, written first.
/// The writer therefore stored the .glb's own guid in baseColorMap, and the
/// reader (resolveSource, role='source' first) resolved it back to the .glb:
/// every map on every imported model came back empty. The order is now fully
/// determined: pinned, then the requested KIND, then role='source', then the
/// guid itself so two equal candidates always answer the same way.
QString guidForStorePath(QSqlDatabase conn, const QString &root, const QString &path,
                         const QString &projectGuid,
                         GuidPreference prefer = GuidPreference::Any);

/// REPAIR for texture slots saved with the broken tie-break above: given the
/// guid a scene stores in a material's texture slot, the Texture asset it
/// should have named — or empty when nothing needs repairing (the guid
/// already names a Texture, or names nothing this catalog knows).
///
/// `storedGuid` names an Object (the .glb/.fbx the texture arrived in); the
/// slot's property name (`baseColorMap`, `normalMap`, …) is what says WHICH of
/// the object's textures it was, because the guid no longer does — every slot
/// on the model collapsed onto the one object guid. Two routes, in order:
/// the object's own serialized blob (the import wrote the correct per-slot
/// member guids into it), then the member texture whose FILE NAME matches the
/// slot's role words. A tolerant reader, not a migration: nothing is written
/// to the catalog and an unrecognisable slot simply stays empty.
QString textureGuidForSlot(QSqlDatabase conn, const QString &storedGuid,
                           const QString &slotName);

/// textureGuidForSlot with the tolerant-read POLICY around it: returns
/// `storedGuid` untouched when there is nothing to repair, and logs one line
/// when there is. This is the form every reader wants — SceneReader,
/// MaterialReader and AssetHelper all resolve texture slots and all three had
/// (or, for AssetHelper, could not have) their own copy of the same five lines.
/// `who` names the caller in the log ("material reader", "asset helper").
QString repairTextureSlot(const QString &storedGuid, const QString &slotName,
                          const char *who);

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
/// Called from store bootstrap and from every successful AssetStoreService
/// root change, so every root Jahshaka owns identifies itself.
bool writeStoreInfo(const QString &root, QString *errorOut);

/// The store format this build writes and can read. A root whose store.json
/// claims a HIGHER version was written by a newer Jahshaka; adopting it would
/// mean reading a layout we do not know (deep audit 2026-09, area 6: the
/// sanity anchor writeStoreInfo documented but no caller ever consulted).
inline constexpr int kStoreFormatVersion = 1;

/// Read <root>/store.json. Returns false when the file is absent or
/// unparseable (a pre-store.json store — legitimate, the caller falls back to
/// its content check). `formatVersionOut` is 0 when the field is missing.
bool readStoreInfo(const QString &root, QString *storeIdOut, int *formatVersionOut);
} // namespace AssetCas

#endif // ASSETCAS_H
