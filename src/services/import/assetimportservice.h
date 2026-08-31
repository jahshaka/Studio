/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETIMPORTSERVICE_H
#define ASSETIMPORTSERVICE_H

// AssetImportService — THE import pipeline spine (ASSET_PIPELINE_SPEC §3.2,
// phase 3). Every import in the product goes through import():
//
//   sniff      pick the per-type importer (extension registry + magic bytes)
//   validate   cheap structural checks with a *message*, never a crash
//   convert    per-type normalize into a private staging dir (embedded
//              textures decoded there — never beside a read-only source)
//   store      CAS-FIRST: every content file hashes into objects/, files/
//              asset_files rows + the rebuild sidecar are written, and the
//              legacy per-guid folder is materialized as a HARDLINK VIEW for
//              the read sites not yet on the resolver (~0 bytes)
//   register   catalog rows + dependencies (one transaction with store) +
//              session AssetManager entries + drawer filing
//
// Deterministic by construction: object ids are content hashes, and the
// import record {sourceOid, settings, importer, importerVersion, assimp}
// lands in the row's properties (assets.importSettings reads it back;
// assets.checkConsistency re-runs convert and diffs the object set).
//
// Progress/cancel: the caller's ImportProgressFn sees every stage and every
// file; returning false cancels — the transaction rolls back and objects
// this import created (refcount 0) are removed. Headless-safe throughout;
// viewer thumbnails stay the UI caller's job.

#include <QString>
#include <QVector>

#include "services/import/importtypes.h"

class Database;
class Project;
class AssetImporterBase;

class AssetImportService
{
public:
    AssetImportService(Database *db, Project *project);
    ~AssetImportService();

    /// The one entry point: run the full pipeline for one source file.
    ImportResult import(const ImportRequest &request,
                        const ImportProgressFn &progress = ImportProgressFn());

    /// Re-run the convert stage on an asset's stored source and diff the
    /// produced object set against the catalog's (Unity -consistencyCheck).
    /// Returns {ok, consistent, expected: [...], produced: [...], error}.
    QJsonObject checkConsistency(const QString &guid);

    /// The recorded determinism block for an asset ("import" in properties).
    QJsonObject importSettings(const QString &guid) const;

    /// The registered importers, in sniff order (mesh, image, audio, video,
    /// shader, material, jaf, file).
    const QVector<AssetImporterBase *> &importers() const { return mImporters; }

private:
    AssetImporterBase *pickImporter(const ImportRequest &request, QString *error) const;
    bool commitStagedAsset(const ImportRequest &request, StagedAsset &staged,
                           ImportResult &result, const ImportProgressFn &progress);

    Database *db;
    Project *project;
    QVector<AssetImporterBase *> mImporters;
};

/// Per-type importer contract. Importers are STATELESS between imports; the
/// spine owns staging dirs, transactions and rollback.
class AssetImporterBase
{
public:
    virtual ~AssetImporterBase() = default;

    virtual QString name() const = 0;
    virtual int version() const = 0;            // bump on behavior change — part of the cache key
    virtual int modelType() const = 0;          // ModelTypes value this importer produces

    /// Does this importer accept the file? Extension registry + magic bytes.
    virtual bool sniff(const QString &path) const = 0;

    /// Cheap structural validation (sidecar completeness, readable header).
    /// False ⇒ errorOut carries a user-facing message.
    virtual bool validate(const QString &path, QString *errorOut) const { Q_UNUSED(path); Q_UNUSED(errorOut); return true; }

    /// Produce the full StagedAsset plan. `stagingDir` is writable and private
    /// to this import; extraction output goes THERE. No DB writes, no store
    /// writes — the spine commits.
    virtual bool convert(const ImportRequest &request, const QString &stagingDir,
                         Database *db, Project *project, StagedAsset &out,
                         QString *errorOut, const ImportProgressFn &progress) = 0;
};

#endif // ASSETIMPORTSERVICE_H
