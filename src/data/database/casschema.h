/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#ifndef CASSCHEMA_H
#define CASSCHEMA_H

// Content-addressed-store schema (ASSET_PIPELINE_SPEC §3.1.3, phase 2) —
// shared between the live Database (createAllTables) and the migration/
// verify/rebuild tools, which operate on EXPLICIT db paths over their own
// connections (rehearsal support, preflight §3.2). Additive only: old
// binaries ignore these tables entirely, so restoring a DB backup alone
// fully reverts a migration (the legacy tree is retained).

namespace CasSchema
{
// 1 = the CAS tables; 2 = project_assets (reference-with-pin, phase 4).
// Fresh databases bootstrap the FULL final schema directly — there are no
// user-data migrations (the app ships new; the owner's library is wiped).
inline constexpr int kUserVersion = 2;

// Reference-with-pin (ASSET_PIPELINE_SPEC §3.1.5, phase 4): a project "use"
// of an asset is a row here, pinning the source oid AT ADD TIME. Content is
// immutable (I3), so the pin gives copy semantics without copying: a library
// re-import moves the LIBRARY pointer; the project keeps rendering the exact
// bytes it was built with until the pin is explicitly updated. Per-project
// edits are copy-on-write: new oid, pin moves.
inline constexpr const char *kProjectAssetsTable =
    "CREATE TABLE IF NOT EXISTS project_assets ("
    "    project_guid TEXT,"
    "    asset_guid   TEXT,"
    "    oid_pin      TEXT,"                 // '' = asset has no stored bytes
    "    added        DATETIME DEFAULT CURRENT_TIMESTAMP,"
    "    PRIMARY KEY (project_guid, asset_guid)"
    ")";

inline constexpr const char *kFilesTable =
    "CREATE TABLE IF NOT EXISTS files ("
    "    oid       TEXT PRIMARY KEY,"       // sha256 hex, lowercase
    "    size      INTEGER,"
    "    ext       TEXT,"                   // display/export extension, lowercase
    "    refcount  INTEGER DEFAULT 0"       // maintained by the asset_files triggers; GC input
    ")";

inline constexpr const char *kAssetFilesTable =
    "CREATE TABLE IF NOT EXISTS asset_files ("
    "    asset_guid TEXT,"
    "    role       TEXT,"                  // 'source' | 'file' (finer roles arrive with phase 3)
    "    oid        TEXT,"
    "    name       TEXT,"                  // original filename — display + export naming only
    "    PRIMARY KEY (asset_guid, role, name)"
    ")";

inline constexpr const char *kAssetFilesOidIndex =
    "CREATE INDEX IF NOT EXISTS idx_asset_files_oid ON asset_files (oid)";

inline constexpr const char *kRefcountInsertTrigger =
    "CREATE TRIGGER IF NOT EXISTS trg_asset_files_refcount_inc "
    "AFTER INSERT ON asset_files BEGIN "
    "UPDATE files SET refcount = refcount + 1 WHERE oid = NEW.oid; "
    "END";

inline constexpr const char *kRefcountDeleteTrigger =
    "CREATE TRIGGER IF NOT EXISTS trg_asset_files_refcount_dec "
    "AFTER DELETE ON asset_files BEGIN "
    "UPDATE files SET refcount = refcount - 1 WHERE oid = OLD.oid; "
    "END";

// Minimal assets table for rebuildCatalog into a FRESH database (the
// Unity-Library-delete recovery story). KEEP IN SYNC with Database's
// assetsTableSchema (database.cpp ctor) — same columns, same order.
inline constexpr const char *kAssetsTableForRebuild =
    "CREATE TABLE IF NOT EXISTS assets ("
    "    guid              VARCHAR(32) PRIMARY KEY,"
    "    type              INTEGER,"
    "    name              VARCHAR(128),"
    "    collection        INTEGER,"
    "    times_used        INTEGER,"
    "    project_guid      VARCHAR(32),"
    "    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
    "    last_updated      DATETIME,"
    "    author            VARCHAR(128),"
    "    license           VARCHAR(64),"
    "    hash              VARCHAR(16),"
    "    version           VARCHAR(8),"
    "    parent            VARCHAR(32),"
    "    thumbnail         BLOB,"
    "    asset             BLOB,"
    "    tags              BLOB,"
    "    properties        BLOB,"
    "    view_filter       INTEGER"
    ")";
} // namespace CasSchema

#endif // CASSCHEMA_H
